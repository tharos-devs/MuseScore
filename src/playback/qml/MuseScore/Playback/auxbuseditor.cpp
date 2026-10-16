/*
 * SPDX-License-Identifier: GPL-3.0-only
 * MuseScore-Studio-CLA-applies
 *
 * MuseScore Studio
 * Music Composition & Notation
 *
 * Copyright (C) 2026 MuseScore Limited and others
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 3 as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <https://www.gnu.org/licenses/>.
 */
#include "auxbuseditor.h"

#include <algorithm>
#include <map>

#include <QPointer>

#include "translation.h"

#include "project/inotationproject.h"

#include "mixerchannelitem.h"

using namespace muse;
using namespace mu::playback;
using namespace muse::audio;
using namespace mu::project;

std::shared_ptr<AuxBusEditor> AuxBusEditor::shared(const muse::modularity::ContextPtr& iocCtx)
{
    static std::map<const muse::modularity::Context*, std::weak_ptr<AuxBusEditor> > editors;

    std::weak_ptr<AuxBusEditor>& weakEditor = editors[iocCtx.get()];
    std::shared_ptr<AuxBusEditor> editor = weakEditor.lock();
    if (!editor) {
        editor = std::make_shared<AuxBusEditor>(iocCtx);
        weakEditor = editor;
    }

    return editor;
}

AuxBusEditor::AuxBusEditor(const muse::modularity::ContextPtr& iocCtx)
    : QObject(), muse::Contextable(iocCtx)
{
    controller()->trackAdded().onReceive(this, [this](const TrackId trackId) {
        onTrackAdded(trackId);
    });

    //! NOTE: a request the engine refused never comes back
    context()->currentProjectChanged().onNotify(this, [this]() {
        m_requestedAuxBusCount[0] = m_requestedAuxBusCount[1] = 0;
        m_pendingAuxAssignTrackIds.clear();
        m_pendingAuxRedo.reset();
    });
}

IProjectAudioSettingsPtr AuxBusEditor::audioSettings() const
{
    return context()->currentProject() ? context()->currentProject()->audioSettings() : nullptr;
}

IProjectUndoStackPtr AuxBusEditor::projectUndoStack() const
{
    return context()->currentProject() ? context()->currentProject()->undoStack() : nullptr;
}

muse::async::Channel<aux_channel_idx_t, TrackId> AuxBusEditor::auxBusAdded() const
{
    return m_auxBusAdded;
}

bool AuxBusEditor::canAddAuxBus() const
{
    return controller()->canAddAuxBus();
}

void AuxBusEditor::requestAuxBus(bool isGroupBus)
{
    if (!controller()->canAddAuxBus()) {
        return;
    }

    ++m_requestedAuxBusCount[isGroupBus];

    if (isGroupBus) {
        controller()->addNewGroupBus();
    } else {
        controller()->addNewAuxBus();
    }
}

void AuxBusEditor::addAuxBus(bool isGroupBus)
{
    requestAuxBus(isGroupBus);
}

void AuxBusEditor::addAuxBusForTracks(bool isGroupBus, const QList<TrackId>& trackIds)
{
    //! NOTE: refuses a second, overlapping request rather than letting it clobber the
    //! first one's pending list - addNewAuxBus()/addNewGroupBus() are async (see
    //! m_pendingAuxAssignTrackIds' own NOTE), so without this, clicking either "for
    //! selected tracks" action twice before the first bus resolves would silently
    //! leave one of the two newly-created buses with no tracks assigned to it. Also
    //! refuses while an undo/redo-driven bus recreation (m_pendingAuxRedo) is in
    //! flight, for the same reason - both resolve via the same onTrackAdded() callback,
    //! matched only by bus type, so two in flight at once could cross-wire.
    if (!m_pendingAuxAssignTrackIds.isEmpty() || m_pendingAuxRedo) {
        return;
    }

    //! NOTE: re-checked synchronously here rather than relied on solely via the
    //! context-menu item's own enabled binding, which can go stale between the menu
    //! opening and the click (e.g. another bus added elsewhere in that window - the
    //! same race class already documented at PlaybackController::addAuxTrack()'s
    //! m_pendingAuxIndices reservation). If the bus pool is already full,
    //! addNewAuxBus()/addNewGroupBus() below silently do nothing and trackAdded()
    //! never fires - committing to a pending list first would leave it stuck forever.
    if (trackIds.isEmpty() || !controller()->canAddAuxBus()) {
        return;
    }

    m_pendingAuxAssignTrackIds = trackIds;
    m_pendingAuxAssignIsGroupBus = isGroupBus;

    requestAuxBus(isGroupBus);
}

std::vector<TrackId> AuxBusEditor::allTrackIds() const
{
    std::vector<TrackId> result;
    for (const auto& [_, trackId] : controller()->instrumentTrackIdMap()) {
        result.push_back(trackId);
    }

    for (const auto& [_, trackId] : controller()->auxTrackIdMap()) {
        result.push_back(trackId);
    }

    return result;
}

void AuxBusEditor::clearAuxSendsTargeting(aux_channel_idx_t index, TrackId exceptTrackId)
{
    for (const TrackId trackId : allTrackIds()) {
        if (trackId == exceptTrackId) {
            continue;
        }

        if (MixerChannelItem* item = MixerChannelItem::findBoundChannel(trackId)) {
            item->clearAuxSendsTargeting(index);
        }
    }
}

void AuxBusEditor::assignAuxSend(const QList<TrackId>& trackIds, aux_channel_idx_t index)
{
    for (const TrackId& trackId : trackIds) {
        if (MixerChannelItem* item = MixerChannelItem::findBoundChannel(trackId)) {
            item->assignAuxSend(index);
        }
    }
}

void AuxBusEditor::deleteAuxBus(aux_channel_idx_t index)
{
    //! NOTE: the Reverb bus isn't removable - see IPlaybackController::removeAuxBus
    const IPlaybackController::AuxTrackIdMap& auxTracks = controller()->auxTrackIdMap();
    auto auxIt = auxTracks.find(index);
    if (index == REVERB_CHANNEL_IDX || auxIt == auxTracks.end()) {
        return;
    }

    const TrackId trackId = auxIt->second;
    const bool isGroupBus = controller()->isAuxBusGroup(index);

    //! NOTE: captured before removal so a manual delete is undoable too, reusing the
    //! same snapshot shape and recreate/remove closures as pushAddAuxBusUndoCommand().
    const IProjectAudioSettingsPtr settings = audioSettings();
    if (!settings) {
        return;
    }

    AudioOutputParams outParams = settings->auxOutputParams(index);
    muse::String name = settings->auxName(index);
    aux_channel_idx_t displayNumber = settings->auxDisplayNumber(index);
    int sortOrder = settings->auxSortOrder(index);

    QList<TrackId> assignedTrackIds;
    for (const TrackId otherTrackId : allTrackIds()) {
        const MixerChannelItem* item = otherTrackId != trackId ? MixerChannelItem::findBoundChannel(otherTrackId) : nullptr;
        if (item && item->auxSendItemForBus(index)) {
            assignedTrackIds.push_back(otherTrackId);
        }
    }

    //! NOTE: must run before removeAuxBus() below - once the bus is gone there is no way
    //! to tell which other tracks' aux-send slots used to target it
    clearAuxSendsTargeting(index, trackId);

    //! NOTE: the panels remove its channel item on the trackRemoved() signal this triggers
    controller()->removeAuxBus(index);

    IProjectUndoStackPtr undoStack = projectUndoStack();
    if (!undoStack) {
        return;
    }

    auto identity = std::make_shared<AuxBusIdentity>(AuxBusIdentity { index, trackId });

    undoStack->push(muse::TranslatableString("undoableAction", isGroupBus ? "Delete Mixer Group channel" : "Delete Mixer FX channel"),
                    makeRemoveAuxBusClosure(identity),
                    makeRecreateAuxBusClosure(isGroupBus, outParams, name, displayNumber, sortOrder, assignedTrackIds, identity));
}

void AuxBusEditor::onTrackAdded(const TrackId& trackId)
{
    const IPlaybackController::AuxTrackIdMap& auxTracks = controller()->auxTrackIdMap();
    auto auxIt = std::find_if(auxTracks.begin(), auxTracks.end(), [trackId](const auto& pair) {
        return pair.second == trackId;
    });

    if (auxIt == auxTracks.end()) {
        return;
    }

    aux_channel_idx_t newIndex = auxIt->first;
    bool isGroupBus = controller()->isAuxBusGroup(newIndex);

    //! NOTE: e.g. a bus of the project being loaded: nothing to undo nor restore
    if (m_requestedAuxBusCount[isGroupBus] == 0) {
        m_auxBusAdded.send(newIndex, trackId);
        return;
    }
    --m_requestedAuxBusCount[isGroupBus];

    //! NOTE: consumes a pending redo (see pushAddAuxBusUndoCommand()) BEFORE the panels
    //! build its channel item (see auxBusAdded()), so it picks up the restored identity
    //! (name/display number/output params/sort order) instead of the fresh defaults
    //! addNewAuxBus()/addNewGroupBus() just assigned this recycled index.
    QList<TrackId> redoAssignedTrackIds;
    bool wasRedo = false;
    if (m_pendingAuxRedo && m_pendingAuxRedo->isGroupBus == isGroupBus) {
        PendingAuxRedo redo = *m_pendingAuxRedo;
        m_pendingAuxRedo.reset();
        wasRedo = true;

        if (const IProjectAudioSettingsPtr settings = audioSettings()) {
            settings->setAuxOutputParams(newIndex, redo.outParams);
            settings->setAuxName(newIndex, redo.name);
            settings->setAuxDisplayNumber(newIndex, redo.displayNumber);
            settings->setAuxSortOrder(newIndex, redo.sortOrder);
        }

        //! NOTE: this bus's (index, trackId) is fresh every time it's recreated -
        //! update the command's shared identity in place so its paired remove
        //! closure (and a LATER redo of this same command, if the user cycles
        //! undo/redo more than once) keeps acting on whichever incarnation of this
        //! bus currently exists, not the one captured when the command was first
        //! pushed. See AuxBusIdentity's own NOTE.
        if (redo.identity) {
            redo.identity->index = newIndex;
            redo.identity->trackId = trackId;
        }

        redoAssignedTrackIds = redo.assignedTrackIds;
    }

    //! NOTE: consumes a pending "add channel for selected tracks" request (see
    //! addAuxBusForTracks()) once its new bus actually resolves here. Gated on
    //! isGroupBus matching too, not just "a pending request exists", since this fires
    //! for ANY newly-added aux bus - including one the user creates via a completely
    //! unrelated "Add FX/Group channel" action while this request is still in flight (a
    //! narrow race, but a real one: bus creation is async, see m_pendingAuxAssignTrackIds'
    //! own NOTE - addAuxBusForTracks() also refuses to start a second overlapping
    //! request, so at most one is ever in flight at a time).
    QList<TrackId> assignedTrackIds;
    if (!m_pendingAuxAssignTrackIds.isEmpty() && isGroupBus == m_pendingAuxAssignIsGroupBus) {
        assignedTrackIds = m_pendingAuxAssignTrackIds;
        m_pendingAuxAssignTrackIds.clear();
        assignAuxSend(assignedTrackIds, newIndex);
    }

    assignAuxSend(redoAssignedTrackIds, newIndex);

    //! NOTE: a redo doesn't push a NEW undo command - it's replaying one already
    //! on the stack (assignedTrackIds and redoAssignedTrackIds are mutually
    //! exclusive - a redo never also has a pending "for selected tracks" request).
    if (!wasRedo) {
        pushAddAuxBusUndoCommand(newIndex, trackId, isGroupBus, assignedTrackIds);
    }

    m_auxBusAdded.send(newIndex, trackId);
}

std::function<void()> AuxBusEditor::makeRecreateAuxBusClosure(bool isGroupBus, const AudioOutputParams& outParams,
                                                              const muse::String& name, aux_channel_idx_t displayNumber,
                                                              int sortOrder, const QList<TrackId>& assignedTrackIds,
                                                              std::shared_ptr<AuxBusIdentity> identity)
{
    QPointer<AuxBusEditor> guard(this);

    return [guard, isGroupBus, outParams, name, displayNumber, sortOrder, assignedTrackIds, identity]() {
        if (!guard || !guard->controller()->canAddAuxBus()) {
            return;
        }

        //! NOTE: refuses to start if another aux-bus add is already in flight - either
        //! this same mechanism (m_pendingAuxRedo) or the separate "add for selected
        //! tracks" flow (m_pendingAuxAssignTrackIds). Both resolve via the same
        //! onTrackAdded() callback, matched only by bus type - two in flight at once
        //! could otherwise consume each other's pending state on the wrong bus.
        if (guard->m_pendingAuxRedo || !guard->m_pendingAuxAssignTrackIds.isEmpty()) {
            return;
        }

        guard->m_pendingAuxRedo = PendingAuxRedo { isGroupBus, outParams, name, displayNumber, sortOrder, assignedTrackIds, identity };
        guard->requestAuxBus(isGroupBus);
    };
}

std::function<void()> AuxBusEditor::makeRemoveAuxBusClosure(std::shared_ptr<AuxBusIdentity> identity)
{
    QPointer<AuxBusEditor> guard(this);

    return [guard, identity]() {
        if (!guard) {
            return;
        }

        const IPlaybackController::AuxTrackIdMap& auxTracks = guard->controller()->auxTrackIdMap();
        auto it = auxTracks.find(identity->index);
        if (it == auxTracks.end() || it->second != identity->trackId) {
            return;
        }

        //! NOTE: must run before removeAuxBus() below - same reasoning as
        //! deleteAuxBus()'s own identical sweep.
        guard->clearAuxSendsTargeting(identity->index, identity->trackId);

        guard->controller()->removeAuxBus(identity->index);
    };
}

void AuxBusEditor::pushAddAuxBusUndoCommand(aux_channel_idx_t index, const TrackId& trackId, bool isGroupBus,
                                            const QList<TrackId>& assignedTrackIds)
{
    IProjectUndoStackPtr undoStack = projectUndoStack();
    const IProjectAudioSettingsPtr settings = audioSettings();
    if (!undoStack || !settings) {
        return;
    }

    AudioOutputParams outParams = settings->auxOutputParams(index);
    muse::String name = settings->auxName(index);
    aux_channel_idx_t displayNumber = settings->auxDisplayNumber(index);
    int sortOrder = settings->auxSortOrder(index);

    auto identity = std::make_shared<AuxBusIdentity>(AuxBusIdentity { index, trackId });

    undoStack->push(muse::TranslatableString("undoableAction", isGroupBus ? "Add Mixer Group channel" : "Add Mixer FX channel"),
                    makeRecreateAuxBusClosure(isGroupBus, outParams, name, displayNumber, sortOrder, assignedTrackIds, identity),
                    makeRemoveAuxBusClosure(identity));
}
