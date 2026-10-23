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
#pragma once

#include <memory>
#include <optional>
#include <functional>

#include <QList>
#include <QObject>

#include "async/asyncable.h"
#include "async/channel.h"
#include "modularity/ioc.h"
#include "project/iprojectsession.h"
#include "project/iprojectaudiosettings.h"
#include "project/iprojectundostack.h"

#include "iplaybackcontroller.h"

namespace mu::playback {
//! NOTE: adds and deletes the Aux FX/Group buses, undoably, for the Mixer and the Track list. One per context, shared by
//! the panels open (see shared()): an undo/redo works whichever of them is open, and a new bus gets the tracks to send
//! to it whichever panel asked for it
class AuxBusEditor : public QObject, public muse::async::Asyncable, public muse::Contextable
{
    Q_OBJECT

    muse::ContextInject<IPlaybackController> controller = { this };
    muse::ContextInject<project::IProjectSession> context = { this };

public:
    //! NOTE: alive while a panel holds it
    static std::shared_ptr<AuxBusEditor> shared(const muse::modularity::ContextPtr& iocCtx);

    explicit AuxBusEditor(const muse::modularity::ContextPtr& iocCtx);

    bool canAddAuxBus() const;
    void addAuxBus(bool isGroupBus);
    //! NOTE: the tracks get a send to the new bus once it's there (see onTrackAdded())
    void addAuxBusForTracks(bool isGroupBus, const QList<muse::audio::TrackId>& trackIds);
    void deleteAuxBus(muse::audio::aux_channel_idx_t index);

    //! NOTE: every bus added (whatever added it), once a redo restored its name, output params and the sends to it
    muse::async::Channel<muse::audio::aux_channel_idx_t, muse::audio::TrackId> auxBusAdded() const;

private:
    project::IProjectAudioSettingsPtr audioSettings() const;
    project::IProjectUndoStackPtr projectUndoStack() const;

    void onTrackAdded(const muse::audio::TrackId& trackId);
    void requestAuxBus(bool isGroupBus);

    //! NOTE: the tracks and buses with a channel item in a panel (see MixerChannelItem::findBoundChannel())
    std::vector<muse::audio::TrackId> allTrackIds() const;
    void clearAuxSendsTargeting(muse::audio::aux_channel_idx_t index, muse::audio::TrackId exceptTrackId = -1);
    void assignAuxSend(const QList<muse::audio::TrackId>& trackIds, muse::audio::aux_channel_idx_t index);

    //! NOTE: an aux bus's (index, trackId) pair is NOT stable across a remove+recreate
    //! cycle - addNewAuxBus()/addNewGroupBus() always hand out a fresh index and the
    //! engine always assigns a fresh trackId, even when "recreating" a bus an undo
    //! command is trying to restore. A command's redo/undo closures share ONE of these
    //! (by shared_ptr), mutated in place by onTrackAdded() whenever a recreate resolves,
    //! so repeated undo/redo/undo/redo cycling on the same add or delete keeps acting on
    //! whichever (index, trackId) that bus MOST RECENTLY got - not the one it had when
    //! the command was first pushed, which a plain captured-by-value pair would.
    struct AuxBusIdentity {
        muse::audio::aux_channel_idx_t index = 0;
        muse::audio::TrackId trackId = -1;
    };

    //! NOTE: the full identity of a bus a redo (see makeRecreateAuxBusClosure()) just
    //! asked PlaybackController to recreate - consumed by onTrackAdded() once that bus
    //! resolves, to restore its original name/display number/sort order/output params,
    //! reassign the same tracks, and update `identity` in place (see AuxBusIdentity
    //! above), since addNewAuxBus()/addNewGroupBus() otherwise only ever hand out fresh
    //! defaults. Also doubles as the signal that a just-resolved bus came from a redo
    //! rather than a fresh user action, so onTrackAdded() doesn't push a second undo
    //! command for the same addition.
    struct PendingAuxRedo {
        bool isGroupBus = false;
        project::AudioOutputParams outParams;
        muse::String name;
        muse::audio::aux_channel_idx_t displayNumber = 0;
        int sortOrder = -1;
        QList<muse::audio::TrackId> assignedTrackIds;
        std::shared_ptr<AuxBusIdentity> identity;
    };

    //! NOTE: captures everything needed to make a just-created aux bus undoable - undo
    //! removes it, redo recreates it and restores its identity via m_pendingAuxRedo
    //! above. Shares its AuxBusIdentity with makeRemoveAuxBusClosure() below so both
    //! ends of the command track the bus's CURRENT (index, trackId), not just the one
    //! captured at push time - see AuxBusIdentity's own NOTE.
    std::function<void()> makeRecreateAuxBusClosure(bool isGroupBus, const project::AudioOutputParams& outParams, const muse::String& name,
                                                    muse::audio::aux_channel_idx_t displayNumber, int sortOrder,
                                                    const QList<muse::audio::TrackId>& assignedTrackIds,
                                                    std::shared_ptr<AuxBusIdentity> identity);
    std::function<void()> makeRemoveAuxBusClosure(std::shared_ptr<AuxBusIdentity> identity);
    void pushAddAuxBusUndoCommand(muse::audio::aux_channel_idx_t index, const muse::audio::TrackId& trackId, bool isGroupBus,
                                  const QList<muse::audio::TrackId>& assignedTrackIds);

    //! NOTE: tracks to assign to the next aux bus of m_pendingAuxAssignIsGroupBus's
    //! type once it actually resolves via onTrackAdded() - addNewAuxBus()/addNewGroupBus()
    //! are fire-and-forget (no return value, no promise), so the new bus's own index is
    //! only knowable once the engine round-trip completes and IPlaybackController::
    //! trackAdded() fires; this can't be done synchronously right after requesting it.
    QList<muse::audio::TrackId> m_pendingAuxAssignTrackIds;
    bool m_pendingAuxAssignIsGroupBus = false;

    std::optional<PendingAuxRedo> m_pendingAuxRedo;

    //! NOTE: the buses asked for and not there yet, per kind (index: isGroupBus): only those are a user's addition to
    //! undo - the ones of a project being loaded come through trackAdded() too
    int m_requestedAuxBusCount[2] = { 0, 0 };

    muse::async::Channel<muse::audio::aux_channel_idx_t, muse::audio::TrackId> m_auxBusAdded;
};
}
