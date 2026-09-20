/*
 * SPDX-License-Identifier: GPL-3.0-only
 * MuseScore-Studio-CLA-applies
 *
 * MuseScore Studio
 * Music Composition & Notation
 *
 * Copyright (C) 2021 MuseScore Limited and others
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

#include "mixerpanelmodel.h"

#include <algorithm>
#include <cmath>

#include <QPointer>

#include "async/notifylist.h"
#include "defer.h"
#include "log.h"
#include "translation.h"

#include "notation/imasternotation.h"
#include "notation/inotationautomation.h"
#include "notation/inotationparts.h"
#include "notation/inotationplayback.h"

#include "project/inotationproject.h"

using namespace muse;
using namespace mu::playback;
using namespace muse::audio;
using namespace mu::engraving;
using namespace mu::notation;
using namespace mu::project;

static constexpr int INVALID_INDEX = -1;
static constexpr TrackId VIDEO_TRACK_ID = -2;

static volume_db_t videoVolumeToDb(float volume)
{
    volume = std::clamp(volume, 0.f, 1.f);
    return volume <= 0.f ? volume_db_t::make(-60.f) : muse::linear_to_db(muse::ratio_t::make(volume));
}

static float videoVolumeFromDb(volume_db_t volume)
{
    float linear = std::clamp(muse::db_to_linear(volume).raw(), 0.f, 1.f);
    return linear <= 0.001f ? 0.f : linear;
}

//! NOTE: an item can read muted() == true purely because some OTHER item's solo force-muted
//! it (see MixerChannelItem::loadMuteForceMuteState()) - that's not a real per-channel mute
//! the user asked for, so the global Mute button must ignore it
static bool isExplicitlyMuted(const MixerChannelItem* item)
{
    return item->muted() && !item->forceMute();
}

MixerPanelModel::MixerPanelModel(QObject* parent)
    : QAbstractListModel(parent), muse::Contextable(muse::iocCtxForQmlObject(this))
{
}

void MixerPanelModel::componentComplete()
{
    init();
}

void MixerPanelModel::init()
{
    //! NOTE Must be set from Qml
    DO_ASSERT(m_navigationSection);

    controller()->playbackInitedChanged().onReceive(this, [this](bool) {
        reload();
    });

    controller()->trackAdded().onReceive(this, [this](const TrackId trackId) {
        onTrackAdded(trackId);
    });

    controller()->trackRemoved().onReceive(this, [this](const TrackId trackId) {
        removeItem(trackId);
    });

    reload();
}

void MixerPanelModel::reload()
{
    TRACEFUNC;

    reloadItems();
}

QVariantMap MixerPanelModel::get(int index)
{
    QVariantMap result;

    QHash<int, QByteArray> names = roleNames();
    QHashIterator<int, QByteArray> i(names);
    while (i.hasNext()) {
        i.next();
        QModelIndex idx = this->index(index, 0);
        QVariant data = idx.data(i.key());
        result[i.value()] = data;
    }

    return result;
}

void MixerPanelModel::selectChannel(MixerChannelItem* item, bool extendSelection, bool rangeSelection)
{
    if (!item) {
        return;
    }

    const int itemIndex = m_mixerChannelList.indexOf(item);

    auto isSelectable = [](const MixerChannelItem* channel) {
        return channel->type() == MixerChannelItem::Type::PrimaryInstrument
               || channel->type() == MixerChannelItem::Type::SecondaryInstrument
               || channel->type() == MixerChannelItem::Type::Aux;
    };

    if (rangeSelection && m_selectionAnchorIndex >= 0 && itemIndex >= 0) {
        const int from = std::min(m_selectionAnchorIndex, itemIndex);
        const int to = std::max(m_selectionAnchorIndex, itemIndex);

        for (int i = 0; i < m_mixerChannelList.size(); ++i) {
            MixerChannelItem* channel = m_mixerChannelList.at(i);
            if (isSelectable(channel)) {
                channel->setSelected(i >= from && i <= to);
            }
        }

        return;
    }

    if (extendSelection) {
        item->setSelected(!item->selected());
        m_selectionAnchorIndex = itemIndex;
        return;
    }

    for (MixerChannelItem* channel : m_mixerChannelList) {
        if (channel != item && channel->selected()) {
            channel->setSelected(false);
        }
    }

    item->setSelected(true);
    m_selectionAnchorIndex = itemIndex;
}

void MixerPanelModel::setColorForSelectedChannels(const QColor& color)
{
    applyColorToSelectedChannels(color, muse::TranslatableString("undoableAction", "Change Mixer channel color"));
}

void MixerPanelModel::resetColorForSelectedChannels()
{
    applyColorToSelectedChannels(QColor(), muse::TranslatableString("undoableAction", "Reset Mixer channel color"));
}

void MixerPanelModel::applyColorToSelectedChannels(const QColor& color, const muse::TranslatableString& actionName)
{
    QList<std::pair<muse::audio::TrackId, QColor> > oldColors;

    for (MixerChannelItem* item : m_mixerChannelList) {
        if (!item->selected() || item->color() == color) {
            continue;
        }

        oldColors.push_back({ item->trackId(), item->color() });
        item->setColor(color);
    }

    if (oldColors.isEmpty()) {
        return;
    }

    IProjectUndoStackPtr undoStack = projectUndoStack();
    if (!undoStack) {
        return;
    }

    QPointer<MixerPanelModel> guard(this);

    undoStack->push(actionName, [guard, oldColors, color]() {
        if (!guard) {
            return;
        }

        for (const auto& pair : oldColors) {
            if (MixerChannelItem* item = guard->findChannelItem(pair.first)) {
                item->setColor(color);
            }
        }
    }, [guard, oldColors]() {
        if (!guard) {
            return;
        }

        for (const auto& pair : oldColors) {
            if (MixerChannelItem* item = guard->findChannelItem(pair.first)) {
                item->setColor(pair.second);
            }
        }
    });
}

template<typename T>
void MixerPanelModel::pushChannelFieldUndoCommand(const TrackId& trackId, const muse::TranslatableString& actionName,
                                                  void (MixerChannelItem::* setter)(T), T oldValue, T newValue)
{
    IProjectUndoStackPtr undoStack = projectUndoStack();
    if (!undoStack) {
        return;
    }

    QPointer<MixerPanelModel> guard(this);

    undoStack->push(actionName, [guard, trackId, setter, newValue]() {
        if (!guard) {
            return;
        }

        if (MixerChannelItem* item = guard->findChannelItem(trackId)) {
            (item->*setter)(newValue);
        }
    }, [guard, trackId, setter, oldValue]() {
        if (!guard) {
            return;
        }

        if (MixerChannelItem* item = guard->findChannelItem(trackId)) {
            (item->*setter)(oldValue);
        }
    });
}

void MixerPanelModel::connectContinuousChangeUndo(MixerChannelItem* item)
{
    connect(item, &MixerChannelItem::volumeChangeCommitted, this, [this, item](float oldValue, float newValue) {
        pushChannelFieldUndoCommand<float>(item->trackId(), muse::TranslatableString("undoableAction", "Change Mixer channel volume"),
                                           &MixerChannelItem::setVolumeLevel, oldValue, newValue);
    });

    connect(item, &MixerChannelItem::balanceChangeCommitted, this, [this, item](int oldValue, int newValue) {
        pushChannelFieldUndoCommand<int>(item->trackId(), muse::TranslatableString("undoableAction", "Change Mixer channel pan"),
                                         &MixerChannelItem::setBalance, oldValue, newValue);
    });

    connect(item, &MixerChannelItem::gainChangeCommitted, this, [this, item](int oldValue, int newValue) {
        pushChannelFieldUndoCommand<int>(item->trackId(), muse::TranslatableString("undoableAction", "Change Mixer channel gain"),
                                         &MixerChannelItem::setGain, oldValue, newValue);
    });

    connect(item, &MixerChannelItem::auxSendLevelChangeCommitted, this,
            [this, item](aux_channel_idx_t busIndex, int oldValue, int newValue) {
        IProjectUndoStackPtr undoStack = projectUndoStack();
        if (!undoStack) {
            return;
        }

        QPointer<MixerPanelModel> guard(this);
        TrackId trackId = item->trackId();

        undoStack->push(muse::TranslatableString("undoableAction", "Change Mixer aux send level"),
                        [guard, trackId, busIndex, newValue]() {
            if (!guard) {
                return;
            }

            if (MixerChannelItem* channel = guard->findChannelItem(trackId)) {
                if (AuxSendItem* send = channel->auxSendItemForBus(busIndex)) {
                    send->setAudioSignalPercentage(newValue);
                }
            }
        }, [guard, trackId, busIndex, oldValue]() {
            if (!guard) {
                return;
            }

            if (MixerChannelItem* channel = guard->findChannelItem(trackId)) {
                if (AuxSendItem* send = channel->auxSendItemForBus(busIndex)) {
                    send->setAudioSignalPercentage(oldValue);
                }
            }
        });
    });
}

void MixerPanelModel::setMutedForSelectedChannels(bool muted)
{
    for (MixerChannelItem* item : m_mixerChannelList) {
        if (!item->selected()) {
            continue;
        }

        //! NOTE: mirrors MixerMuteAndSoloSection.qml's own per-button
        //! `enabled: !(muted && forceMute)` guard - a channel showing muted purely
        //! because ANOTHER channel's solo force-muted it isn't something the user is
        //! directly interacting with right now, so a multi-select fan-out shouldn't
        //! silently overwrite its own persisted manual mute state as a side effect
        //! of muting/unmuting a DIFFERENT selected channel.
        if (item->muted() && item->forceMute()) {
            continue;
        }

        item->setMuted(muted);
    }
}

void MixerPanelModel::setSoloForSelectedChannels(bool solo)
{
    for (MixerChannelItem* item : m_mixerChannelList) {
        if (!item->selected()) {
            continue;
        }

        //! NOTE: mirrors MixerMuteAndSoloSection.qml's own per-button `enabled`/
        //! `visible` guards - solo is only meaningful for a non-Aux channel or a
        //! Group-type Aux bus (see that file's own NOTE on why a plain send/return
        //! bus is excluded). The mute condition is deliberately the OPPOSITE of the
        //! Mute case above: `muted && !forceMute` (manually muted), not `muted &&
        //! forceMute` - a force-muted channel's own Solo button stays enabled by
        //! design (soloing it is exactly how you hand the solo over to it), so
        //! skipping it here would silently block the very channel the user just
        //! clicked Solo on from ever taking the solo while part of a selection.
        bool soloEligible = item->type() != MixerChannelItem::Type::Aux || item->isGroupBus();
        bool manuallyMuted = item->muted() && !item->forceMute();
        if (!soloEligible || manuallyMuted) {
            continue;
        }

        item->setSolo(solo);
    }
}

void MixerPanelModel::clearSelection()
{
    for (MixerChannelItem* item : m_mixerChannelList) {
        if (item->selected()) {
            item->setSelected(false);
        }
    }

    m_selectionAnchorIndex = -1;
}

QVariant MixerPanelModel::data(const QModelIndex& index, int role) const
{
    if (!index.isValid() || index.row() >= rowCount() || role != ChannelItemRole) {
        return QVariant();
    }

    return QVariant::fromValue(m_mixerChannelList.at(index.row()));
}

void MixerPanelModel::renameAuxChannel(MixerChannelItem* channelItem, const QString& name)
{
    IF_ASSERT_FAILED(channelItem && channelItem->type() == MixerChannelItem::Type::Aux) {
        return;
    }

    QString trimmedName = name.trimmed();
    if (trimmedName.isEmpty() || trimmedName == channelItem->title()) {
        return;
    }

    aux_channel_idx_t index = channelItem->auxBusIndex();

    audioSettings()->setAuxName(index, muse::String::fromQString(trimmedName));
    channelItem->setTitle(trimmedName);

    for (MixerChannelItem* item : m_mixerChannelList) {
        if (item != channelItem) {
            item->renameAuxSendsTargeting(index, trimmedName);
        }
    }
}

void MixerPanelModel::deleteAuxChannel(MixerChannelItem* channelItem)
{
    IF_ASSERT_FAILED(channelItem && channelItem->type() == MixerChannelItem::Type::Aux) {
        return;
    }

    //! NOTE: the Reverb bus isn't removable - see IPlaybackController::removeAuxBus
    if (channelItem->isReverbBus()) {
        return;
    }

    aux_channel_idx_t index = channelItem->auxBusIndex();
    bool isGroupBus = channelItem->isGroupBus();
    TrackId trackId = channelItem->trackId();

    //! NOTE: captured before removal so a manual delete is undoable too, reusing the
    //! same snapshot shape and recreate/remove closures as pushAddAuxBusUndoCommand().
    AudioOutputParams outParams = audioSettings()->auxOutputParams(index);
    muse::String name = audioSettings()->auxName(index);
    aux_channel_idx_t displayNumber = audioSettings()->auxDisplayNumber(index);
    int sortOrder = audioSettings()->auxSortOrder(index);

    QList<TrackId> assignedTrackIds;
    for (MixerChannelItem* item : m_mixerChannelList) {
        if (item != channelItem && item->auxSendItemForBus(index)) {
            assignedTrackIds.push_back(item->trackId());
        }
    }

    //! NOTE: must run before removeAuxBus() below - once the bus is gone there is no way
    //! to tell which other tracks' aux-send slots used to target it
    for (MixerChannelItem* item : m_mixerChannelList) {
        if (item != channelItem) {
            item->clearAuxSendsTargeting(index);
        }
    }

    //! NOTE: the channel item itself is removed from m_mixerChannelList via the
    //! trackRemoved() signal this triggers (see init()), not here
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

QVariantList MixerPanelModel::selectedAuxBusIndices(bool isGroupBus) const
{
    QVariantList result;

    for (aux_channel_idx_t index : sortedAuxIndices()) {
        if (controller()->isAuxBusGroup(index) != isGroupBus) {
            continue;
        }

        const MixerChannelItem* item = findChannelItem(controller()->auxTrackIdMap().at(index));
        if (item && item->selected()) {
            result.push_back(static_cast<int>(index));
        }
    }

    return result;
}

QVariantList MixerPanelModel::auxBusIndicesOfType(bool isGroupBus) const
{
    QVariantList result;

    for (aux_channel_idx_t index : sortedAuxIndices()) {
        if (controller()->isAuxBusGroup(index) == isGroupBus) {
            result.push_back(static_cast<int>(index));
        }
    }

    return result;
}

int MixerPanelModel::auxBusModelIndex(int auxBusIndex) const
{
    const IPlaybackController::AuxTrackIdMap& auxTrackIdMap = controller()->auxTrackIdMap();
    auto it = auxTrackIdMap.find(static_cast<aux_channel_idx_t>(auxBusIndex));
    if (it == auxTrackIdMap.end()) {
        return INVALID_INDEX;
    }

    return indexOf(it->second);
}

void MixerPanelModel::reorderAuxChannels(const QVariantList& draggedAuxBusIndices, int dropBeforeAuxBusIndex)
{
    if (draggedAuxBusIndices.isEmpty()) {
        return;
    }

    std::vector<aux_channel_idx_t> dragged;
    dragged.reserve(draggedAuxBusIndices.size());
    for (const QVariant& v : draggedAuxBusIndices) {
        dragged.push_back(static_cast<aux_channel_idx_t>(v.toInt()));
    }

    const IPlaybackController::AuxTrackIdMap& auxTrackIdMap = controller()->auxTrackIdMap();

    //! NOTE: defensive - MixerTitleSection.qml is expected to only ever gather same-type,
    //! currently-existing buses into a single drag targeting a same-type,
    //! currently-existing drop position, but none of that is ever trusted blindly
    //! across the QML/C++ boundary:
    //! - a stray cross-type index would silently corrupt both sections' sort order;
    //! - a bus deleted (e.g. via the context menu, or Undo) in the moment between the
    //!   drag starting and this call - the mouse button can stay held for an arbitrary
    //!   time - would otherwise crash on the unchecked auxTrackIdMap.at() calls below.
    if (!muse::contains(auxTrackIdMap, dragged.front())) {
        return;
    }
    bool isGroupBus = controller()->isAuxBusGroup(dragged.front());

    for (aux_channel_idx_t index : dragged) {
        IF_ASSERT_FAILED(muse::contains(auxTrackIdMap, index)
                         && controller()->isAuxBusGroup(index) == isGroupBus) {
            return;
        }
    }
    if (dropBeforeAuxBusIndex >= 0) {
        auto dropIndex = static_cast<aux_channel_idx_t>(dropBeforeAuxBusIndex);
        IF_ASSERT_FAILED(muse::contains(auxTrackIdMap, dropIndex)
                         && controller()->isAuxBusGroup(dropIndex) == isGroupBus) {
            return;
        }
    }

    //! NOTE: captures every same-type bus's sort order BEFORE the reorder, for undo -
    //! see the push() below for why undo/redo replay this via setAuxSortOrder()+reload()
    //! rather than replaying the remove/insert dance itself.
    QList<std::pair<aux_channel_idx_t, int> > oldSortOrders;
    for (aux_channel_idx_t index : sortedAuxIndices()) {
        if (controller()->isAuxBusGroup(index) == isGroupBus) {
            oldSortOrders.push_back({ index, audioSettings()->auxSortOrder(index) });
        }
    }

    //! NOTE: the row right after the last bus of this type - i.e. right before whatever
    //! immediately follows it (another aux type, video/metronome, or master). Deliberately
    //! NOT resolveAuxInsertIndex(): that resolves where a bus's OWN persisted sort order
    //! currently ranks it among siblings, which is right for an incrementally-added bus
    //! (whose order is either freshly assigned or already correctly reloaded), but wrong
    //! here - a dragged bus's persisted order is stale until the bookkeeping loop below
    //! runs, so ranking it among siblings would put a "drop at the end" back wherever its
    //! OLD order happened to place it instead of genuinely at the end.
    auto endOfSectionRow = [this, isGroupBus]() {
        for (int i = 0; i < m_mixerChannelList.size(); ++i) {
            const MixerChannelItem* item = m_mixerChannelList[i];
            if (item->type() == MixerChannelItem::Type::Master
                || item->type() == MixerChannelItem::Type::Video
                || item->type() == MixerChannelItem::Type::Metronome) {
                return i;
            }
            if (!isGroupBus && item->type() == MixerChannelItem::Type::Aux && item->isGroupBus()) {
                return i;
            }
        }

        return masterChannelIndex();
    };

    //! NOTE: relocates m_mixerChannelList's actual rows directly (via the same
    //! beginRemoveRows()/beginInsertRows() primitives addItem()/removeItem() already use,
    //! as a remove-then-reinsert rather than a genuine beginMoveRows() - its
    //! destinationChild has fiddly, easy-to-get-wrong-in-a-way-that-corrupts-the-view
    //! indexing conventions that differ from QList::move()'s own, and correctness here
    //! matters more than avoiding one extra delegate recreation per dragged channel)
    //! rather than doing a full reload() - this still preserves each MixerChannelItem's
    //! object identity, so runtime UI state that isn't itself persisted (selected() in
    //! particular - the dragged channels should stay visibly selected right after the
    //! drop) survives the reorder.
    //!
    //! Moves one dragged bus at a time, in the given (already-in-display-order)
    //! sequence, to just before the drop target - recomputing both rows fresh each
    //! iteration since every move shifts subsequent rows.
    bool movedAny = false;

    for (aux_channel_idx_t draggedIndex : dragged) {
        int fromRow = indexOf(auxTrackIdMap.at(draggedIndex));

        int toRow = dropBeforeAuxBusIndex >= 0
                    ? indexOf(auxTrackIdMap.at(static_cast<aux_channel_idx_t>(dropBeforeAuxBusIndex)))
                    : endOfSectionRow();

        if (fromRow < 0 || toRow < 0 || fromRow == toRow) {
            continue;
        }

        MixerChannelItem* item = m_mixerChannelList[fromRow];

        beginRemoveRows(QModelIndex(), fromRow, fromRow);
        m_mixerChannelList.removeAt(fromRow);
        endRemoveRows();

        //! NOTE: removing fromRow shifts every row after it down by one - toRow must be
        //! adjusted to still refer to the same logical position now that fromRow is gone
        //! (mirrors beginInsertRows()'s own "insert before whatever currently sits at
        //! this row" convention, applied to the list state right after the removal).
        int adjustedToRow = toRow > fromRow ? toRow - 1 : toRow;

        beginInsertRows(QModelIndex(), adjustedToRow, adjustedToRow);
        m_mixerChannelList.insert(adjustedToRow, item);
        endInsertRows();

        movedAny = true;
    }

    if (!movedAny) {
        return;
    }

    updateItemsPanelsOrder();

    //! NOTE: bookkeeping only from here - the visual reorder already happened above via
    //! the moves; this just makes it survive a project reload (see
    //! IProjectAudioSettings::auxSortOrder()). Walks the list's actual, now-final
    //! physical order rather than re-deriving it, since sortedAuxIndices() would still
    //! read the OLD persisted values this loop is about to replace.
    int order = 0;
    QList<std::pair<aux_channel_idx_t, int> > newSortOrders;
    for (const MixerChannelItem* item : std::as_const(m_mixerChannelList)) {
        if (item->type() == MixerChannelItem::Type::Aux && item->isGroupBus() == isGroupBus) {
            audioSettings()->setAuxSortOrder(item->auxBusIndex(), order);
            newSortOrders.push_back({ item->auxBusIndex(), order });
            ++order;
        }
    }

    IProjectUndoStackPtr undoStack = projectUndoStack();
    if (!undoStack) {
        return;
    }

    QPointer<MixerPanelModel> guard(this);

    //! NOTE: undo/redo just replay the persisted sort order and reload() - much simpler
    //! and safer than reimplementing this function's own delicate remove/insert row
    //! splicing a second time, and reload() is only paid once per explicit Ctrl+Z/
    //! Ctrl+Shift+Z press here, not on every drag-move tick.
    undoStack->push(muse::TranslatableString("undoableAction", "Reorder Mixer channels"),
                    [guard, newSortOrders]() {
        if (!guard) {
            return;
        }

        for (const auto& pair : newSortOrders) {
            guard->audioSettings()->setAuxSortOrder(pair.first, pair.second);
        }
        guard->reload();
    }, [guard, oldSortOrders]() {
        if (!guard) {
            return;
        }

        for (const auto& pair : oldSortOrders) {
            guard->audioSettings()->setAuxSortOrder(pair.first, pair.second);
        }
        guard->reload();
    });
}

bool MixerPanelModel::canAddAuxBus() const
{
    return controller()->canAddAuxBus();
}

void MixerPanelModel::addFxChannel()
{
    controller()->addNewAuxBus();
}

void MixerPanelModel::addGroupChannel()
{
    controller()->addNewGroupBus();
}

void MixerPanelModel::addFxChannelForSelectedTracks()
{
    requestNewAuxBusForSelectedTracks(false);
}

void MixerPanelModel::addGroupChannelForSelectedTracks()
{
    requestNewAuxBusForSelectedTracks(true);
}

void MixerPanelModel::requestNewAuxBusForSelectedTracks(bool isGroupBus)
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
    if (!controller()->canAddAuxBus()) {
        return;
    }

    QList<TrackId> selectedInstrumentTrackIds;
    for (const MixerChannelItem* item : std::as_const(m_mixerChannelList)) {
        bool isInstrument = item->type() == MixerChannelItem::Type::PrimaryInstrument
                            || item->type() == MixerChannelItem::Type::SecondaryInstrument;
        if (isInstrument && item->selected()) {
            selectedInstrumentTrackIds.push_back(item->trackId());
        }
    }

    if (selectedInstrumentTrackIds.isEmpty()) {
        return;
    }

    m_pendingAuxAssignTrackIds = selectedInstrumentTrackIds;
    m_pendingAuxAssignIsGroupBus = isGroupBus;

    if (isGroupBus) {
        controller()->addNewGroupBus();
    } else {
        controller()->addNewAuxBus();
    }
}

//! NOTE: live - true whenever ANY channel (any type except Metronome) is currently muted,
//! not just right after toggleGlobalMute() runs. So muting a single channel via its own
//! per-channel button lights up the global button too (see connectGlobalMuteSoloAggregate()).
bool MixerPanelModel::globalMuteEngaged() const
{
    for (const MixerChannelItem* item : m_mixerChannelList) {
        if (item->type() != MixerChannelItem::Type::Metronome && isExplicitlyMuted(item)) {
            return true;
        }
    }

    return false;
}

bool MixerPanelModel::globalSoloEngaged() const
{
    for (const MixerChannelItem* item : m_mixerChannelList) {
        if (item->type() != MixerChannelItem::Type::Metronome && item->solo()) {
            return true;
        }
    }

    return false;
}

void MixerPanelModel::toggleGlobalMute()
{
    //! NOTE: branches on whether a capture is pending, NOT on globalMuteEngaged() - that
    //! getter is live (see its own doc comment) and can turn back true from a channel muted
    //! manually in between the two clicks (e.g. via its own per-channel button). Branching on
    //! it directly would make THIS click capture that unrelated channel instead of restoring
    //! the originally-captured set, permanently losing the latter. A pending capture always
    //! takes priority: it is only ever cleared by actually restoring it.
    if (!m_mutedTrackIdsBeforeGlobalMute.isEmpty()) {
        for (const TrackId& trackId : std::as_const(m_mutedTrackIdsBeforeGlobalMute)) {
            if (MixerChannelItem* item = findChannelItem(trackId)) {
                item->setMuted(true);
            }
        }
        m_mutedTrackIdsBeforeGlobalMute.clear();
    } else {
        for (MixerChannelItem* item : std::as_const(m_mixerChannelList)) {
            if (item->type() == MixerChannelItem::Type::Metronome || !isExplicitlyMuted(item)) {
                continue;
            }

            m_mutedTrackIdsBeforeGlobalMute.push_back(item->trackId());
            item->setMuted(false);
        }
    }

    //! NOTE: each setMuted() call above already triggers globalMuteEngagedChanged() via
    //! connectGlobalMuteSoloAggregate() - this covers the no-op case (nothing to mute/restore)
    //! where the loop above made no calls at all
    emit globalMuteEngagedChanged();
}

void MixerPanelModel::toggleGlobalSolo()
{
    //! NOTE: see the matching NOTE in toggleGlobalMute() - same pending-capture priority
    if (!m_soloedTrackIdsBeforeGlobalSolo.isEmpty()) {
        for (const TrackId& trackId : std::as_const(m_soloedTrackIdsBeforeGlobalSolo)) {
            if (MixerChannelItem* item = findChannelItem(trackId)) {
                item->setSolo(true);
            }
        }
        m_soloedTrackIdsBeforeGlobalSolo.clear();
    } else {
        for (MixerChannelItem* item : std::as_const(m_mixerChannelList)) {
            if (item->type() == MixerChannelItem::Type::Metronome || !item->solo()) {
                continue;
            }

            m_soloedTrackIdsBeforeGlobalSolo.push_back(item->trackId());
            item->setSolo(false);
        }
    }

    emit globalSoloEngagedChanged();
}

void MixerPanelModel::connectGlobalMuteSoloAggregate(MixerChannelItem* item)
{
    connect(item, &MixerChannelItem::mutedChanged, this, [this]() {
        emit globalMuteEngagedChanged();
    });

    //! NOTE: isExplicitlyMuted() depends on forceMute too - e.g. soloing one channel
    //! force-mutes every other one without changing their own muted flag, which must NOT
    //! light up the global Mute button (see isExplicitlyMuted())
    connect(item, &MixerChannelItem::forceMuteChanged, this, [this]() {
        emit globalMuteEngagedChanged();
    });

    connect(item, &MixerChannelItem::soloChanged, this, [this]() {
        emit globalSoloEngagedChanged();
    });
}

int MixerPanelModel::rowCount(const QModelIndex&) const
{
    return m_mixerChannelList.count();
}

QHash<int, QByteArray> MixerPanelModel::roleNames() const
{
    static const QHash<int, QByteArray> roles = {
        { ChannelItemRole, "channelItem" }
    };

    return roles;
}

void MixerPanelModel::reloadItems()
{
    TRACEFUNC;

    beginResetModel();

    DEFER {
        endResetModel();
        emit rowCountChanged();
    };

    clear();

    if (!controller()->isPlaybackInited()) {
        return;
    }

    const auto& instrumentTrackIdMap = controller()->instrumentTrackIdMap();

    auto addInstrumentTrack = [this, &instrumentTrackIdMap](const InstrumentTrackId& instrumentTrackId, bool isPrimary = true) {
        auto search = instrumentTrackIdMap.find(instrumentTrackId);
        if (search == instrumentTrackIdMap.cend()) {
            return;
        }

        m_mixerChannelList.push_back(buildInstrumentChannelItem(search->second, instrumentTrackId, isPrimary));
    };

    async::NotifyList<const Part*> partList = masterNotationParts()->partList();
    for (const Part* part : partList) {
        std::string primaryInstrId = part->instrument()->id().toStdString();

        for (const InstrumentTrackId& instrumentTrackId : part->instrumentTrackIdList()) {
            bool isPrimary = instrumentTrackId.instrumentId == primaryInstrId;
            addInstrumentTrack(instrumentTrackId, isPrimary);
        }
    }
    for (auto it = instrumentTrackIdMap.cbegin(); it != instrumentTrackIdMap.cend(); ++it) {
        if (notationPlayback()->isChordSymbolsTrack(it->first)) {
            addInstrumentTrack(it->first);
        }
    }

    //! NOTE: aux buses come before video/metronome (both placed immediately left of
    //! master instead) so the aux section stays adjacent to the instrument tracks
    //! it routes from, rather than sitting between two channels (video, metronome)
    //! that have nothing to do with aux routing.
    if (configuration()->areAuxChannelsVisible()) {
        const auto& auxTrackIdMap = controller()->auxTrackIdMap();
        for (aux_channel_idx_t index : sortedAuxIndices()) {
            m_mixerChannelList.push_back(buildAuxChannelItem(index, auxTrackIdMap.at(index)));
        }
    }

    if (videoSettings() && videoSettings()->attachment().isValid()) {
        m_mixerChannelList.push_back(buildVideoChannelItem());
    }

    addInstrumentTrack(notationPlayback()->metronomeTrackId());

    m_masterChannelItem = buildMasterChannelItem();
    m_mixerChannelList.append(m_masterChannelItem);

    updateItemsPanelsOrder();
    setupConnections();
}

void MixerPanelModel::onTrackAdded(const TrackId& trackId)
{
    TRACEFUNC;

    const IPlaybackController::InstrumentTrackIdMap& instrumentTracks = controller()->instrumentTrackIdMap();
    auto instrumentIt = std::find_if(instrumentTracks.cbegin(), instrumentTracks.cend(), [trackId](const auto& pair) {
        return pair.second == trackId;
    });

    if (instrumentIt != instrumentTracks.end()) {
        const InstrumentTrackId& instrumentTrackId = instrumentIt->first;
        const Part* part = masterNotationParts()->part(instrumentTrackId.partId);
        bool isPrimary = part ? part->instrument()->id() == instrumentTrackId.instrumentId : true;
        MixerChannelItem* item = buildInstrumentChannelItem(trackId, instrumentTrackId, isPrimary);
        int index = resolveInsertIndex(instrumentTrackId);

        addItem(item, index);
        return;
    }

    const IPlaybackController::AuxTrackIdMap& auxTracks = controller()->auxTrackIdMap();
    auto auxIt = std::find_if(auxTracks.begin(), auxTracks.end(), [trackId](const auto& pair) {
        return pair.second == trackId;
    });

    if (auxIt != auxTracks.end()) {
        aux_channel_idx_t newIndex = auxIt->first;
        bool isGroupBus = controller()->isAuxBusGroup(newIndex);

        //! NOTE: consumes a pending redo (see pushAddAuxBusUndoCommand()) BEFORE the
        //! channel item is built below, so it picks up the restored identity (name/
        //! display number/output params/sort order) instead of the fresh defaults
        //! addNewAuxBus()/addNewGroupBus() just assigned this recycled index.
        QList<TrackId> redoAssignedTrackIds;
        bool wasRedo = false;
        if (m_pendingAuxRedo && m_pendingAuxRedo->isGroupBus == isGroupBus) {
            PendingAuxRedo redo = *m_pendingAuxRedo;
            m_pendingAuxRedo.reset();
            wasRedo = true;

            audioSettings()->setAuxOutputParams(newIndex, redo.outParams);
            audioSettings()->setAuxName(newIndex, redo.name);
            audioSettings()->setAuxDisplayNumber(newIndex, redo.displayNumber);
            audioSettings()->setAuxSortOrder(newIndex, redo.sortOrder);

            //! NOTE: this bus's (index, trackId) is fresh every time it's recreated -
            //! update the command's shared identity in place so its paired remove
            //! closure (and a LATER redo of this same command, if the user cycles
            //! undo/redo more than once) keeps acting on whichever incarnation of this
            //! bus currently exists, not the one captured when the command was first
            //! pushed. See MixerPanelModel::AuxBusIdentity's own NOTE.
            if (redo.identity) {
                redo.identity->index = newIndex;
                redo.identity->trackId = trackId;
            }

            redoAssignedTrackIds = redo.assignedTrackIds;
        }

        if (configuration()->areAuxChannelsVisible()) {
            addItem(buildAuxChannelItem(newIndex, trackId), resolveAuxInsertIndex(newIndex, isGroupBus));
        }

        //! NOTE: consumes a pending "add channel for selected tracks" request (see
        //! requestNewAuxBusForSelectedTracks()) once its new bus actually resolves
        //! here - deliberately NOT nested inside the areAuxChannelsVisible() check
        //! above, since assigning selected tracks needs only the new bus's index and
        //! their own (independently visible) channel items, not a visible
        //! MixerChannelItem for the bus itself; nesting it there would strand a
        //! pending request forever if Aux channels happened to be hidden at the
        //! moment this resolves. Gated on isGroupBus matching too, not just "a
        //! pending request exists", since this fires for ANY newly-added aux bus -
        //! including one the user creates via a completely unrelated "Add FX/Group
        //! channel" action while this request is still in flight (a narrow race, but
        //! a real one: bus creation is async, see m_pendingAuxAssignTrackIds' own
        //! NOTE - requestNewAuxBusForSelectedTracks() also refuses to start a second
        //! overlapping request, so at most one is ever in flight at a time).
        QList<TrackId> assignedTrackIds;
        if (!m_pendingAuxAssignTrackIds.isEmpty() && isGroupBus == m_pendingAuxAssignIsGroupBus) {
            assignedTrackIds = m_pendingAuxAssignTrackIds;
            m_pendingAuxAssignTrackIds.clear();

            for (const TrackId& pendingTrackId : assignedTrackIds) {
                if (MixerChannelItem* trackItem = findChannelItem(pendingTrackId)) {
                    trackItem->assignAuxSend(newIndex);
                }
            }
        }

        for (const TrackId& pendingTrackId : redoAssignedTrackIds) {
            if (MixerChannelItem* trackItem = findChannelItem(pendingTrackId)) {
                trackItem->assignAuxSend(newIndex);
            }
        }

        //! NOTE: a redo doesn't push a NEW undo command - it's replaying one already
        //! on the stack (assignedTrackIds and redoAssignedTrackIds are mutually
        //! exclusive - a redo never also has a pending "for selected tracks" request).
        if (!wasRedo) {
            pushAddAuxBusUndoCommand(newIndex, trackId, isGroupBus, assignedTrackIds);
        }
    }
}

std::function<void()> MixerPanelModel::makeRecreateAuxBusClosure(bool isGroupBus, const AudioOutputParams& outParams,
                                                                 const muse::String& name, aux_channel_idx_t displayNumber,
                                                                 int sortOrder, const QList<TrackId>& assignedTrackIds,
                                                                 std::shared_ptr<AuxBusIdentity> identity)
{
    QPointer<MixerPanelModel> guard(this);

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

        if (isGroupBus) {
            guard->controller()->addNewGroupBus();
        } else {
            guard->controller()->addNewAuxBus();
        }
    };
}

std::function<void()> MixerPanelModel::makeRemoveAuxBusClosure(std::shared_ptr<AuxBusIdentity> identity)
{
    QPointer<MixerPanelModel> guard(this);

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
        //! deleteAuxChannel()'s own identical sweep.
        for (MixerChannelItem* item : std::as_const(guard->m_mixerChannelList)) {
            item->clearAuxSendsTargeting(identity->index);
        }

        guard->controller()->removeAuxBus(identity->index);
    };
}

void MixerPanelModel::pushAddAuxBusUndoCommand(aux_channel_idx_t index, const TrackId& trackId, bool isGroupBus,
                                               const QList<TrackId>& assignedTrackIds)
{
    IProjectUndoStackPtr undoStack = projectUndoStack();
    if (!undoStack) {
        return;
    }

    AudioOutputParams outParams = audioSettings()->auxOutputParams(index);
    muse::String name = audioSettings()->auxName(index);
    aux_channel_idx_t displayNumber = audioSettings()->auxDisplayNumber(index);
    int sortOrder = audioSettings()->auxSortOrder(index);

    auto identity = std::make_shared<AuxBusIdentity>(AuxBusIdentity { index, trackId });

    undoStack->push(muse::TranslatableString("undoableAction", isGroupBus ? "Add Mixer Group channel" : "Add Mixer FX channel"),
                    makeRecreateAuxBusClosure(isGroupBus, outParams, name, displayNumber, sortOrder, assignedTrackIds, identity),
                    makeRemoveAuxBusClosure(identity));
}

void MixerPanelModel::addItem(MixerChannelItem* item, int index)
{
    TRACEFUNC;

    IF_ASSERT_FAILED(item) {
        return;
    }

    beginInsertRows(QModelIndex(), index, index);
    m_mixerChannelList.insert(index, item);
    updateItemsPanelsOrder();
    endInsertRows();

    //! NOTE Keep the selection anchor pointing at the same channel after an insertion shifts indices.
    if (index <= m_selectionAnchorIndex) {
        ++m_selectionAnchorIndex;
    }

    //! NOTE: aux/master channel items load their initial output params (incl. fx chain and
    //! aux sends) synchronously, before they are inserted here - re-sync now so the new
    //! item gets the same slot counts as every other channel. Instrument channel items
    //! load asynchronously, via their own, separately-resolved loadOutputParams() call
    //! (buildInstrumentChannelItem()'s playback()->params(trackId) promise) - which
    //! performs this exact same sync itself once real data actually arrives, so calling
    //! it here too, before that data exists, is not just redundant but actively harmful
    //! for aux sends specifically: unlike fx slots (positioned by each fx's own fixed
    //! chainOrder key), aux-send slot order is assigned by a "next free slot" allocator,
    //! so padding an empty item with blanks now would let those blanks permanently claim
    //! the front slots ahead of the real entries that only get placed once the async
    //! resolve actually runs
    if (item->type() == MixerChannelItem::Type::Aux || item->type() == MixerChannelItem::Type::Master) {
        updateOutputResourceItemCount();
        updateAuxSendItemCount();
    }

    emit rowCountChanged();

    //! NOTE: covers a newly-added item whose muted/solo state was already set (e.g. loaded
    //! from the project) before connectGlobalMuteSoloAggregate() was wired up to it in its
    //! build*ChannelItem() factory - that initial state change happened too early to notify
    //! globalMuteEngaged()/globalSoloEngaged()'s listeners itself
    emit globalMuteEngagedChanged();
    emit globalSoloEngagedChanged();
}

void MixerPanelModel::removeItem(const TrackId trackId)
{
    TRACEFUNC;

    int index = indexOf(trackId);
    if (index == INVALID_INDEX) {
        return;
    }

    beginRemoveRows(QModelIndex(), index, index);

    m_mixerChannelList.removeAt(index);
    updateItemsPanelsOrder();

    endRemoveRows();

    //! NOTE Keep the selection anchor pointing at the same channel after a removal shifts indices,
    //! or invalidate it if the anchor channel itself was removed.
    if (index == m_selectionAnchorIndex) {
        m_selectionAnchorIndex = INVALID_INDEX;
    } else if (index < m_selectionAnchorIndex) {
        --m_selectionAnchorIndex;
    }

    updateOutputResourceItemCount();
    updateAuxSendItemCount();

    emit rowCountChanged();

    //! NOTE: the removed channel may have been the only one contributing to
    //! globalMuteEngaged()/globalSoloEngaged() - its own mutedChanged/soloChanged
    //! connections go away with it, so nothing else would otherwise prompt QML to
    //! re-evaluate those aggregates
    emit globalMuteEngagedChanged();
    emit globalSoloEngagedChanged();
}

void MixerPanelModel::updateItemsPanelsOrder()
{
    TRACEFUNC;

    for (int i = 0; i < m_mixerChannelList.size(); i++) {
        m_mixerChannelList[i]->setPanelOrder(m_navigationOrderStart + i);
    }
}

void MixerPanelModel::clear()
{
    TRACEFUNC;

    m_masterChannelItem = nullptr;
    for (MixerChannelItem* item : m_mixerChannelList) {
        //! NOTE Disconnect immediately so a not-yet-destroyed item can't fire stale
        //! controlParamsChanged/soloMuteStateChanged signals (e.g. if reloadItems()
        //! runs again before this item's deleteLater() is processed).
        item->disconnect();
        item->deleteLater();
    }
    m_mixerChannelList.clear();

    //! NOTE The channel list is being fully rebuilt, so any stored index would point at the wrong
    //! (or a stale/deleted) channel.
    m_selectionAnchorIndex = INVALID_INDEX;

    //! NOTE: same reasoning - a remembered mute/solo snapshot would otherwise reference
    //! trackIds from the channel list that no longer exists
    m_mutedTrackIdsBeforeGlobalMute.clear();
    m_soloedTrackIdsBeforeGlobalSolo.clear();
    emit globalMuteEngagedChanged();
    emit globalSoloEngagedChanged();
}

void MixerPanelModel::setupConnections()
{
    controller()->isPlayingChanged().onReceive(this, [this](bool playing) {
        if (playing) {
            return;
        }

        //! NOTE: some channels (notably aux buses) don't reliably receive a final
        //! silence value from the engine once playback stops, leaving their meters
        //! stuck at their last non-zero reading - force them all back to silence here
        for (MixerChannelItem* item : m_mixerChannelList) {
            item->resetAudioChannelsVolumePressure();
        }
    });

    audioSettings()->auxSoloMuteStateChanged().onReceive(
        this, [this](const aux_channel_idx_t index,
                     notation::INotationSoloMuteState::SoloMuteState newSoloMuteState) {
        const IPlaybackController::AuxTrackIdMap& auxTrackIdMap = controller()->auxTrackIdMap();
        TrackId trackId = muse::value(auxTrackIdMap, index);

        if (MixerChannelItem* item = findChannelItem(trackId)) {
            item->loadSoloMuteState(newSoloMuteState);
        }
    });

    //! NOTE: refreshes a channel item's LIVE effective mute/forceMute (e.g. every sibling
    //! that becomes/stops being force-muted as a side effect of some OTHER track's solo
    //! changing) - see IPlaybackController::trackMuteStateChanged()'s doc comment
    controller()->trackMuteStateChanged().onReceive(
        this, [this](const engraving::InstrumentTrackId& instrumentTrackId, bool muted, bool forceMute) {
        TrackId trackId = muse::value(controller()->instrumentTrackIdMap(), instrumentTrackId);

        if (MixerChannelItem* item = findChannelItem(trackId)) {
            item->loadMuteForceMuteState(muted, forceMute);
        }
    });

    controller()->auxMuteStateChanged().onReceive(
        this, [this](aux_channel_idx_t index, bool muted, bool forceMute) {
        const IPlaybackController::AuxTrackIdMap& auxTrackIdMap = controller()->auxTrackIdMap();
        TrackId trackId = muse::value(auxTrackIdMap, index);

        if (MixerChannelItem* item = findChannelItem(trackId)) {
            item->loadMuteForceMuteState(muted, forceMute);
        }
    });

    playback()->sourceParamsChanged().onReceive(this, [this](const TrackId trackId, const AudioSourceParams& params) {
        if (MixerChannelItem* item = findChannelItem(trackId)) {
            item->loadInputParams(params);
        }
    });

    playback()->fxChainParamsChanged().onReceive(this, [this](const TrackId trackId, const AudioFxChain& params) {
        if (MixerChannelItem* item = findChannelItem(trackId)) {
            AudioOutputParams outParams = audioSettings()->trackOutputParams(item->instrumentTrackId());
            outParams.fxChain = params;
            loadOutputParams(item, outParams);
        }
    });

    playback()->masterFxChainParamsChanged().onReceive(this, [this](const AudioFxChain& params) {
        if (m_masterChannelItem) {
            AudioOutputParams outParams = audioSettings()->masterAudioOutputParams();
            outParams.fxChain = params;
            loadOutputParams(m_masterChannelItem, outParams);
        }
    }, Asyncable::Mode::SetReplace);

    controller()->auxChannelNameChanged().onReceive(this, [this](aux_channel_idx_t index, const std::string& name) {
        for (MixerChannelItem* item : m_mixerChannelList) {
            //! NOTE: auxSendItems() is keyed by stable slot order, not bus index - find the
            //! slot(s) actually targeting this bus by value instead of by key
            for (AuxSendItem* auxSendItem : item->auxSendItems()) {
                if (auxSendItem->auxIndex() == index) {
                    auxSendItem->setTitle(QString::fromStdString(name));
                }
            }
        }
    });

    if (videoSettings()) {
        videoSettings()->settingsChanged().onNotify(this, [this]() {
            onVideoAttachmentChanged();
        });
    }

    controller()->masterOutputForceMuteChanged().onNotify(this, [this]() {
        if (!m_masterChannelItem) {
            return;
        }

        loadOutputParams(m_masterChannelItem, effectiveMasterOutputParams());
    });

    configuration()->areAuxChannelsVisibleChanged().onReceive(this, [this](bool visible) {
        const auto& auxMap = controller()->auxTrackIdMap();

        if (visible) {
            //! NOTE: same FX-then-Group ordering as reloadItems() - see sortedAuxIndices().
            //! Inserted via resolveAuxInsertIndex() (not a raw masterChannelIndex()) so
            //! these land before any existing video/metronome channel rather than
            //! between it and master - see resolveAuxInsertIndex()'s own NOTE.
            for (aux_channel_idx_t index : sortedAuxIndices()) {
                TrackId trackId = auxMap.at(index);
                if (!findChannelItem(trackId)) {
                    bool isGroupBus = controller()->isAuxBusGroup(index);
                    addItem(buildAuxChannelItem(index, trackId), resolveAuxInsertIndex(index, isGroupBus));
                }
            }
        } else {
            for (auto it = auxMap.cbegin(); it != auxMap.cend(); ++it) {
                removeItem(it->second);
            }
        }
    });

    subscribeOnAutomationChanges();
}

void MixerPanelModel::subscribeOnAutomationChanges()
{
    if (!currentProject()) {
        return;
    }

    AutomationDataConstPtr automation = currentProject()->masterNotation()->automation()->automationData();
    if (!automation) {
        return;
    }

    automation->changed().onReceive(this, [this](const AutomationChanges& changes) {
        if (changes.isFullReset) {
            for (MixerChannelItem* item : m_mixerChannelList) {
                item->updateHasAutomationFlags();
            }
            return;
        }

        InstrumentTrackIdSet affectedTrackIds;
        for (const AutomationCurveKey& key : changes.affectedKeys) {
            if (key.type != AutomationType::Volume && key.type != AutomationType::Pan) {
                continue;
            }

            if (const std::optional<InstrumentTrackId> trackId = key.trackId()) {
                affectedTrackIds.insert(*trackId);
            }
        }

        if (affectedTrackIds.empty()) {
            return;
        }

        for (MixerChannelItem* item : m_mixerChannelList) {
            if (muse::contains(affectedTrackIds, item->instrumentTrackId())) {
                item->updateHasAutomationFlags();
            }
        }
    });
}

void MixerPanelModel::onVideoAttachmentChanged()
{
    TRACEFUNC;

    bool hasVideo = videoSettings() && videoSettings()->attachment().isValid();
    bool hadVideo = indexOf(VIDEO_TRACK_ID) != INVALID_INDEX;

    if (hasVideo == hadVideo) {
        //! NOTE The video attachment is still (not) present, but its volume/
        //! balance/mute/solo may have changed from outside the mixer (e.g. the
        //! Video panel's own volume slider) -- refresh the existing channel
        //! item's output params so the mixer stays in sync.
        if (hasVideo) {
            if (MixerChannelItem* item = findChannelItem(VIDEO_TRACK_ID)) {
                const project::VideoAttachmentSettings& attachment = videoSettings()->attachment();

                AudioOutputParams outParams;
                outParams.volume = videoVolumeToDb(attachment.volume);
                outParams.balance = attachment.balance;
                outParams.muted = attachment.muted;
                outParams.solo = attachment.solo;
                loadOutputParams(item, std::move(outParams));
            }
        }

        return;
    }

    if (hasVideo) {
        addItem(buildVideoChannelItem(), resolveVideoInsertIndex());
    } else {
        removeItem(VIDEO_TRACK_ID);
    }
}

int MixerPanelModel::resolveVideoInsertIndex() const
{
    for (int i = 0; i < m_mixerChannelList.size(); ++i) {
        if (m_mixerChannelList[i]->type() == MixerChannelItem::Type::Metronome) {
            return i;
        }
    }

    return masterChannelIndex();
}

int MixerPanelModel::resolveInsertIndex(const engraving::InstrumentTrackId& newInstrumentTrackId) const
{
    const InstrumentTrackId& metronomeTrackId = notationPlayback()->metronomeTrackId();
    if (newInstrumentTrackId == metronomeTrackId) {
        return masterChannelIndex();
    }

    // Assumptions:
    // - the last channel is always the master channel
    // - metronome channel is placed to the immediate left of the master (video, if
    //   present, goes immediately left of metronome in turn - see
    //   resolveVideoInsertIndex()); aux buses, if visible, sit further left still,
    //   adjacent to the instrument tracks they route from (see resolveAuxInsertIndex())
    // - the InstrumentTrackIds from the mixer channel items are always a correctly
    //   sorted subset of the InstrumentTrackIds from NotationParts
    if (notationPlayback()->isChordSymbolsTrack(newInstrumentTrackId)) {
        int metronomeIdx = 0;
        for (const MixerChannelItem* channelItem : m_mixerChannelList) {
            const engraving::InstrumentTrackId& instrumentTrackId = channelItem->instrumentTrackId();
            if (instrumentTrackId.isValid() && instrumentTrackId != metronomeTrackId) {
                metronomeIdx++;
            }
        }
        return metronomeIdx;
    }

    int mixerChannelListIdx = 0;

    async::NotifyList<const Part*> partList = masterNotationParts()->partList();
    for (const Part* part : partList) {
        for (const InstrumentTrackId& instrumentTrackId : part->instrumentTrackIdList()) {
            if (instrumentTrackId == newInstrumentTrackId) {
                return mixerChannelListIdx;
            }

            const MixerChannelItem* mixerChannelItem = m_mixerChannelList[mixerChannelListIdx];
            MixerChannelItem::Type itemType = mixerChannelItem->type();

            if (itemType == MixerChannelItem::Type::Master) {
                return mixerChannelListIdx;
            }

            const InstrumentTrackId& itemInstrumentTrackId = mixerChannelItem->instrumentTrackId();

            if (itemInstrumentTrackId == metronomeTrackId) {
                return mixerChannelListIdx;
            }

            if (notationPlayback()->isChordSymbolsTrack(itemInstrumentTrackId)) {
                return mixerChannelListIdx;
            }

            if (itemInstrumentTrackId == instrumentTrackId) {
                if (instrumentTrackId == newInstrumentTrackId) {
                    return INVALID_INDEX;
                }

                ++mixerChannelListIdx;
            }
        }
    }

    return INVALID_INDEX;
}

int MixerPanelModel::auxSortOrderOrIndex(aux_channel_idx_t index) const
{
    //! NOTE: sort order is the user's drag-and-drop reorder position (see
    //! reorderAuxChannels()), not the bus's own stable index - falling back to the
    //! index for a bus that's never had one assigned is only a defensive fallback in
    //! practice, since PlaybackController::ensureAuxSortOrderAssigned() gives every bus
    //! a real one (initially matching its ascending-index position) as soon as it's
    //! loaded. Shared by sortedAuxIndices() and resolveAuxInsertIndex() below, which
    //! must stay consistent with each other.
    int order = audioSettings()->auxSortOrder(index);
    return order >= 0 ? order : static_cast<int>(index);
}

std::vector<aux_channel_idx_t> MixerPanelModel::sortedAuxIndices() const
{
    //! NOTE: single source of truth for "FX-type buses first (by sort order), then
    //! Group-type buses (by sort order)" - shared by reloadItems() and the
    //! areAuxChannelsVisibleChanged handler in setupConnections(), which both need to
    //! (re)build the aux section of the channel list in this same order. Must stay
    //! consistent with resolveAuxInsertIndex() below, which resolves the equivalent
    //! position for a single newly-added bus rather than the whole list at once.
    std::vector<aux_channel_idx_t> indices;
    const auto& auxTrackIdMap = controller()->auxTrackIdMap();
    indices.reserve(auxTrackIdMap.size());

    std::vector<aux_channel_idx_t> fxIndices;
    std::vector<aux_channel_idx_t> groupIndices;
    for (const auto& pair : auxTrackIdMap) {
        if (controller()->isAuxBusGroup(pair.first)) {
            groupIndices.push_back(pair.first);
        } else {
            fxIndices.push_back(pair.first);
        }
    }

    auto bySortOrder = [this](aux_channel_idx_t a, aux_channel_idx_t b) {
        return auxSortOrderOrIndex(a) < auxSortOrderOrIndex(b);
    };
    std::stable_sort(fxIndices.begin(), fxIndices.end(), bySortOrder);
    std::stable_sort(groupIndices.begin(), groupIndices.end(), bySortOrder);

    indices.insert(indices.end(), fxIndices.begin(), fxIndices.end());
    indices.insert(indices.end(), groupIndices.begin(), groupIndices.end());

    return indices;
}

int MixerPanelModel::resolveAuxInsertIndex(aux_channel_idx_t index, bool isGroupBus) const
{
    //! NOTE: FX-type aux buses are grouped together (by sort order), followed by all
    //! Group-type buses (also by sort order), then video/metronome (if present), then
    //! the master channel. Mirrors sortedAuxIndices()'s ordering, resolving the
    //! equivalent insert position for a single newly-added bus rather than the whole
    //! list at once - without the sort-order comparison below, a bus recreated at a
    //! lower, freed index (e.g. "FX3" after deleting the old FX3 and re-adding), or one
    //! reloading from a saved project with a persisted (possibly drag-and-drop
    //! reordered) position, would always land after every existing same-type sibling
    //! instead of in its correct sorted position, making the on-screen order depend on
    //! WHETHER a bus arrived via this incremental path or a full reload, rather than
    //! being a stable function of the current bus set and its persisted order. Stopping
    //! at video/metronome too (not just master) keeps aux buses added after those
    //! already exist from landing on the wrong side of them.
    int ownOrder = auxSortOrderOrIndex(index);

    if (isGroupBus) {
        for (int i = 0; i < m_mixerChannelList.size(); ++i) {
            const MixerChannelItem* item = m_mixerChannelList[i];
            if (item->type() == MixerChannelItem::Type::Master
                || item->type() == MixerChannelItem::Type::Video
                || item->type() == MixerChannelItem::Type::Metronome) {
                return i;
            }
            if (item->type() == MixerChannelItem::Type::Aux && item->isGroupBus()
                && auxSortOrderOrIndex(item->auxBusIndex()) > ownOrder) {
                return i;
            }
        }

        return masterChannelIndex();
    }

    for (int i = 0; i < m_mixerChannelList.size(); ++i) {
        const MixerChannelItem* item = m_mixerChannelList[i];
        if (item->type() == MixerChannelItem::Type::Master
            || item->type() == MixerChannelItem::Type::Video
            || item->type() == MixerChannelItem::Type::Metronome
            || (item->type() == MixerChannelItem::Type::Aux && item->isGroupBus())) {
            return i;
        }
        if (item->type() == MixerChannelItem::Type::Aux && auxSortOrderOrIndex(item->auxBusIndex()) > ownOrder) {
            return i;
        }
    }

    return masterChannelIndex();
}

int MixerPanelModel::indexOf(const TrackId trackId) const
{
    for (int i = 0; i < m_mixerChannelList.size(); ++i) {
        if (trackId == m_mixerChannelList[i]->trackId()) {
            return i;
        }
    }

    return INVALID_INDEX;
}

MixerChannelItem* MixerPanelModel::buildInstrumentChannelItem(const TrackId trackId,
                                                              const engraving::InstrumentTrackId& instrumentTrackId,
                                                              bool isPrimary)
{
    MixerChannelItem::Type type = isPrimary ? MixerChannelItem::Type::PrimaryInstrument
                                  : MixerChannelItem::Type::SecondaryInstrument;

    const InstrumentTrackId& metronomeTrackId = notationPlayback()->metronomeTrackId();
    if (instrumentTrackId == metronomeTrackId) {
        type = MixerChannelItem::Type::Metronome;
    }

    MixerChannelItem* item = new MixerChannelItem(this, type, false /*outputOnly*/, trackId);
    item->setInstrumentTrackId(instrumentTrackId);
    item->setPanelSection(m_navigationSection);
    item->loadSoloMuteState(controller()->trackSoloMuteState(instrumentTrackId));

    playback()->params(trackId)
    .onResolve(this, [this, trackId, instrumentTrackId](const TrackParams& params) {
        if (MixerChannelItem* item = findChannelItem(trackId)) {
            item->loadInputParams(params.source);

            AudioOutputParams outParams = audioSettings()->trackOutputParams(instrumentTrackId);
            outParams.fxChain = params.fxChain;
            outParams.auxSends = params.auxSends;
            outParams.setControl(params.control);

            //! NOTE: unlike muted (set above via setControl(), which reflects the engine's
            //! own control params - already correctly pushed by
            //! PlaybackController::updateSoloMuteStates() before this resolves), solo has no
            //! engine-side representation at all (see ControlParams - no solo field) and
            //! IProjectAudioSettings::trackOutputParams() never tracks it either, so
            //! outParams.solo would otherwise always be left at its default false here. Its
            //! one real source of truth is INotationSoloMuteState - without this, every
            //! Mixer-panel rebuild (e.g. right after reopening a saved project) would
            //! silently clobber the correctly-restored Solo button back to unchecked.
            outParams.solo = controller()->trackSoloMuteState(instrumentTrackId).solo;

            //! NOTE: forceMute isn't tracked by IProjectAudioSettings either (it's a purely
            //! live, computed value - see updateSoloMuteStates()), so it would otherwise
            //! always be left at its default false here too. Without this, right after
            //! reopening a project with another track soloed, this track's Mute button would
            //! show checked (muted=true, correctly reflecting the engine's already-applied
            //! force-mute via setControl() above) but ENABLED instead of disabled - looking
            //! and behaving like a real manual mute the user must click off themselves,
            //! instead of the intended "greyed out because something else is soloed" look
            //! that clears itself once the solo is lifted (see MixerMuteAndSoloSection.qml).
            outParams.forceMute = controller()->isTrackForceMuted(instrumentTrackId);

            loadOutputParams(item, outParams);
        }
    })
    .onReject(this, [](int errCode, std::string text) {
        LOGE() << "unable to get track output parameters, error code: " << errCode
               << ", " << text;
    });

    playback()->trackName(trackId)
    .onResolve(this, [this, trackId](const RetVal<TrackName>& trackName) {
        if (trackName.ret) {
            if (MixerChannelItem* item = findChannelItem(trackId)) {
                item->setTitle(QString::fromStdString(trackName.val));
            }
        } else {
            LOGE() << "unable to get track name, error: " << trackName.ret.toString();
        }
    });

    playback()->signalChanges(trackId)
    .onResolve(this, [this, trackId](AudioSignalChanges signalChanges) {
        if (MixerChannelItem* item = findChannelItem(trackId)) {
            item->subscribeOnAudioSignalChanges(signalChanges);
        }
    })
    .onReject(this, [](int errCode, std::string text) {
        LOGE() << "unable to subscribe on audio signal changes from mixer channel, error code: " << errCode
               << ", " << text;
    });

    playback()->automatedControlParamsChanges(trackId)
    .onResolve(this, [this, trackId](AutomatedControlParamsChanges changes) {
        if (MixerChannelItem* item = findChannelItem(trackId)) {
            item->subscribeOnAutomatedControlParamsChanges(changes);
        }
    })
    .onReject(this, [](int errCode, std::string text) {
        LOGE() << "unable to subscribe on automated control params changes from mixer channel, error code: " << errCode
               << ", " << text;
    });

    connect(item, &MixerChannelItem::inputParamsChanged, this, [this, trackId](const AudioInputParams& params) {
        playback()->setSourceParams(trackId, params);
    });

    connect(item, &MixerChannelItem::controlParamsChanged, this, [this, trackId, instrumentTrackId](const AudioOutputParams& params) {
        playback()->setControlParams(trackId, params.control());

        //! NOTE Only persist volume/balance/gain here; solo/mute/forceMute are owned by
        //! INotationSoloMuteState and must not be echoed back into the saved output params.
        AudioOutputParams outParams = audioSettings()->trackOutputParams(instrumentTrackId);
        outParams.volume = params.volume;
        outParams.balance = params.balance;
        outParams.gain = params.gain;
        audioSettings()->setTrackOutputParams(instrumentTrackId, outParams);
    });

    connect(item, &MixerChannelItem::fxChainParamsChanged, this, [this, trackId](const AudioOutputParams& params) {
        updateOutputResourceItemCount();
        playback()->setFxChainParams(trackId, params.fxChain);
    });

    connect(item, &MixerChannelItem::auxSendsParamsChanged, this, [this, trackId](const AudioOutputParams& params) {
        playback()->setAuxSendsParams(trackId, params.auxSends);
        updateAuxSendItemCount();
    });

    connect(item, &MixerChannelItem::soloMuteStateChanged, this,
            [this, instrumentTrackId](const notation::INotationSoloMuteState::SoloMuteState& state) {
        controller()->setTrackSoloMuteState(instrumentTrackId, state);
    });

    connect(item, &MixerChannelItem::colorChanged, this, [this, item, instrumentTrackId]() {
        AudioOutputParams outParams = audioSettings()->trackOutputParams(instrumentTrackId);
        outParams.color = item->color();
        audioSettings()->setTrackOutputParams(instrumentTrackId, outParams);
    });

    //! NOTE: fans a user-picked aux-send bus out to every OTHER selected instrument
    //! track, mirroring the multi-select behavior color/drag-reorder/mute/solo
    //! already have - a no-op unless this item itself is currently selected (picking
    //! a bus on an unselected channel affects only that one channel, same as
    //! clicking its own Mute/Solo button would).
    connect(item, &MixerChannelItem::auxSendReassignedByUser, this,
            [this, item](aux_channel_idx_t oldBusIndex, aux_channel_idx_t newBusIndex) {
        QList<muse::audio::TrackId> fanOutTrackIds;

        if (item->selected()) {
            for (MixerChannelItem* other : std::as_const(m_mixerChannelList)) {
                if (other == item || !other->selected()) {
                    continue;
                }

                bool isInstrument = other->type() == MixerChannelItem::Type::PrimaryInstrument
                                    || other->type() == MixerChannelItem::Type::SecondaryInstrument;
                if (isInstrument) {
                    other->assignAuxSend(newBusIndex);
                    fanOutTrackIds.push_back(other->trackId());
                }
            }
        }

        IProjectUndoStackPtr undoStack = projectUndoStack();
        if (!undoStack) {
            return;
        }

        QPointer<MixerPanelModel> guard(this);
        muse::audio::TrackId primaryTrackId = item->trackId();

        //! NOTE: undo/redo work purely at the semantic level (clearAuxSendsTargeting/
        //! assignAuxSend), never on a captured AuxSendItem* slot pointer - a track's
        //! aux-send slot list can be rebuilt (compactAuxSendItemKeys() etc.) by unrelated
        //! changes before undo fires, which would leave a raw slot pointer dangling.
        undoStack->push(muse::TranslatableString("undoableAction", "Assign Mixer aux send"),
                        [guard, primaryTrackId, oldBusIndex, newBusIndex, fanOutTrackIds]() {
            if (!guard) {
                return;
            }

            if (MixerChannelItem* primary = guard->findChannelItem(primaryTrackId)) {
                if (oldBusIndex != AuxSendItem::NO_BUS) {
                    primary->clearAuxSendsTargeting(oldBusIndex);
                }
                primary->assignAuxSend(newBusIndex);
            }

            for (const muse::audio::TrackId& trackId : fanOutTrackIds) {
                if (MixerChannelItem* other = guard->findChannelItem(trackId)) {
                    other->assignAuxSend(newBusIndex);
                }
            }
        }, [guard, primaryTrackId, oldBusIndex, newBusIndex, fanOutTrackIds]() {
            if (!guard) {
                return;
            }

            if (MixerChannelItem* primary = guard->findChannelItem(primaryTrackId)) {
                primary->clearAuxSendsTargeting(newBusIndex);
                if (oldBusIndex != AuxSendItem::NO_BUS) {
                    primary->assignAuxSend(oldBusIndex);
                }
            }

            for (const muse::audio::TrackId& trackId : fanOutTrackIds) {
                if (MixerChannelItem* other = guard->findChannelItem(trackId)) {
                    other->clearAuxSendsTargeting(newBusIndex);
                }
            }
        });
    });

    connectGlobalMuteSoloAggregate(item);
    connectContinuousChangeUndo(item);

    return item;
}

MixerChannelItem* MixerPanelModel::buildAuxChannelItem(aux_channel_idx_t index, const TrackId trackId)
{
    MixerChannelItem* item = new MixerChannelItem(this, MixerChannelItem::Type::Aux, true /*outputOnly*/, trackId);
    item->setPanelSection(m_navigationSection);
    item->setAuxBusIndex(index);
    item->loadSoloMuteState(audioSettings()->auxSoloMuteState(index));

    //! NOTE: set synchronously up front to avoid a flash of the positional/engine name
    //! before playback()->trackName() below resolves
    QString customName = audioSettings()->auxName(index).toQString();
    if (!customName.isEmpty()) {
        item->setTitle(customName);
    }

    playback()->trackName(trackId)
    .onResolve(this, [this, trackId, index](const RetVal<TrackName>& trackName) {
        if (trackName.ret) {
            if (MixerChannelItem* item = findChannelItem(trackId)) {
                //! NOTE: a persisted custom name always wins over the engine's track name
                QString customName = audioSettings()->auxName(index).toQString();
                item->setTitle(!customName.isEmpty() ? customName : QString::fromStdString(trackName.val));
            }
        } else {
            LOGE() << "unable to get track name, error: " << trackName.ret.toString();
        }
    });

    AudioOutputParams outParams = audioSettings()->auxOutputParams(index);

    //! NOTE: auxOutputParams() never tracks solo/muted/forceMute - they're deliberately
    //! excluded from persistence (see updateAuxMuteStates()'s own comment: "muted" isn't
    //! even written to the project file). Without seeding them here from their real sources,
    //! this loadOutputParams() call would immediately clobber the correct solo/mute state
    //! already applied a few lines up via loadSoloMuteState() back to its default false -
    //! e.g. a Group bus that was actually soloed would show its Solo button unchecked right
    //! after the Mixer panel is (re)built (mirrors the analogous fix in
    //! buildInstrumentChannelItem() for instrument tracks' solo/forceMute).
    const notation::INotationSoloMuteState::SoloMuteState& soloMuteState = audioSettings()->auxSoloMuteState(index);
    bool forceMute = controller()->isAuxForceMuted(index);
    outParams.solo = soloMuteState.solo;
    outParams.muted = soloMuteState.mute || forceMute;
    outParams.forceMute = forceMute;

    loadOutputParams(item, outParams);

    playback()->signalChanges(trackId)
    .onResolve(this, [this, trackId](AudioSignalChanges signalChanges) {
        if (MixerChannelItem* item = findChannelItem(trackId)) {
            item->subscribeOnAudioSignalChanges(signalChanges);
        }
    })
    .onReject(this, [](int errCode, std::string text) {
        LOGE() << "unable to subscribe on audio signal changes from mixer channel, error code: " << errCode
               << ", " << text;
    });

    connect(item, &MixerChannelItem::controlParamsChanged, this, [this, trackId, index](const AudioOutputParams& params) {
        playback()->setControlParams(trackId, params.control());

        AudioOutputParams outParams = audioSettings()->auxOutputParams(index);
        outParams.volume = params.volume;
        outParams.balance = params.balance;
        outParams.gain = params.gain;
        audioSettings()->setAuxOutputParams(index, outParams);
    });
    connect(item, &MixerChannelItem::fxChainParamsChanged, this, [this, trackId](const AudioOutputParams& params) {
        updateOutputResourceItemCount();
        playback()->setFxChainParams(trackId, params.fxChain);
    });
    connect(item, &MixerChannelItem::auxSendsParamsChanged, this, [this, trackId](const AudioOutputParams& params) {
        playback()->setAuxSendsParams(trackId, params.auxSends);
        updateAuxSendItemCount();
    });

    connect(item, &MixerChannelItem::soloMuteStateChanged, this,
            [this, index](const notation::INotationSoloMuteState::SoloMuteState& state) {
        audioSettings()->setAuxSoloMuteState(index, state);
    });

    connect(item, &MixerChannelItem::colorChanged, this, [this, item, index]() {
        AudioOutputParams outParams = audioSettings()->auxOutputParams(index);
        outParams.color = item->color();
        audioSettings()->setAuxOutputParams(index, outParams);
    });

    connectGlobalMuteSoloAggregate(item);
    connectContinuousChangeUndo(item);

    return item;
}

MixerChannelItem* MixerPanelModel::buildVideoChannelItem()
{
    MixerChannelItem* item = new MixerChannelItem(this, MixerChannelItem::Type::Video, true /*outputOnly*/, VIDEO_TRACK_ID);
    item->setPanelSection(m_navigationSection);
    item->setTitle(muse::qtrc("playback", "Video"));

    project::VideoAttachmentSettings attachment = videoSettings()->attachment();

    AudioOutputParams outParams;
    outParams.volume = videoVolumeToDb(attachment.volume);
    outParams.balance = attachment.balance;
    outParams.muted = attachment.muted;
    outParams.solo = attachment.solo;
    loadOutputParams(item, std::move(outParams));

    connect(item, &MixerChannelItem::controlParamsChanged, this, [this](const AudioOutputParams& params) {
        IProjectVideoSettingsPtr settings = videoSettings();
        if (!settings) {
            return;
        }

        VideoAttachmentSettings updated = settings->attachment();
        if (!updated.isValid()) {
            return;
        }

        updated.volume = videoVolumeFromDb(params.volume);
        updated.balance = params.balance;
        updated.muted = params.muted;
        settings->setAttachment(updated);
    });

    connect(item, &MixerChannelItem::soloMuteStateChanged, this, [this](const notation::INotationSoloMuteState::SoloMuteState& state) {
        IProjectVideoSettingsPtr settings = videoSettings();
        if (!settings) {
            return;
        }

        VideoAttachmentSettings updated = settings->attachment();
        if (!updated.isValid()) {
            return;
        }

        updated.muted = state.mute;
        updated.solo = state.solo;
        if (updated.solo) {
            updated.muted = false;
        }
        settings->setAttachment(updated);
    });

    connectGlobalMuteSoloAggregate(item);
    connectContinuousChangeUndo(item);

    return item;
}

MixerChannelItem* MixerPanelModel::buildMasterChannelItem()
{
    MixerChannelItem* item = new MixerChannelItem(this, MixerChannelItem::Type::Master, true /*outputOnly*/, MASTER_TRACK_ID);
    item->setPanelSection(m_navigationSection);
    item->setTitle(muse::qtrc("playback", "Master"));

    loadOutputParams(item, effectiveMasterOutputParams());

    playback()->masterSignalChanges()
    .onResolve(this, [this, item](AudioSignalChanges signalChanges) {
        if (m_masterChannelItem && item == m_masterChannelItem) {
            item->subscribeOnAudioSignalChanges(signalChanges);
        }
    })
    .onReject(this, [](int errCode, std::string text) {
        LOGE() << "unable to subscribe on audio signal changes from master channel, error code: " << errCode
               << ", " << text;
    });

    connect(item, &MixerChannelItem::controlParamsChanged, this, [this](const AudioOutputParams& params) {
        AudioOutputParams playbackParams = params;
        playbackParams.forceMute = controller()->isMasterOutputForceMuted();
        if (playbackParams.forceMute) {
            playbackParams.muted = true;
        }

        playback()->setMasterControlParams(playbackParams.control());

        AudioOutputParams outParams = audioSettings()->masterAudioOutputParams();
        outParams.volume = params.volume;
        outParams.balance = params.balance;
        outParams.gain = params.gain;
        audioSettings()->setMasterAudioOutputParams(outParams);
    });

    connect(item, &MixerChannelItem::fxChainParamsChanged, this, [this](const AudioOutputParams& params) {
        playback()->setMasterFxChainParams(params.fxChain);
    });

    connect(item, &MixerChannelItem::auxSendsParamsChanged, this, [this](const AudioOutputParams& params) {
        playback()->setMasterAuxSendsParams(params.auxSends);
        updateAuxSendItemCount();
    });

    connect(item, &MixerChannelItem::soloMuteStateChanged, this, [this, item](const notation::INotationSoloMuteState::SoloMuteState&) {
        playback()->setMasterControlParams(item->outputParams().control());
    });

    connectGlobalMuteSoloAggregate(item);
    connectContinuousChangeUndo(item);

    return item;
}

int MixerPanelModel::masterChannelIndex() const
{
    return m_mixerChannelList.size() - 1;
}

MixerChannelItem* MixerPanelModel::findChannelItem(const TrackId& trackId) const
{
    for (MixerChannelItem* item : m_mixerChannelList) {
        if (item->trackId() == trackId) {
            return item;
        }
    }

    return nullptr;
}

void MixerPanelModel::loadOutputParams(MixerChannelItem* item, const AudioOutputParams& params)
{
    IF_ASSERT_FAILED(item) {
        return;
    }

    item->loadOutputParams(params);
    updateOutputResourceItemCount();
    updateAuxSendItemCount();
}

void MixerPanelModel::updateOutputResourceItemCount()
{
    size_t maxFxCount = 0;

    for (const MixerChannelItem* item : m_mixerChannelList) {
        const AudioFxChain& chain = item->outputParams().fxChain;

        //! NOTE: find the highest chainOrder among REAL (non-blank) entries - the chain's
        //! own highest key can itself be a blank/padding slot (e.g. its own trailing blank),
        //! which must not count towards how many real slots are needed before the shared
        //! trailing blank below is added, or an extra blank slot creeps in on every recount
        for (auto it = chain.crbegin(); it != chain.crend(); ++it) {
            if (it->second.isValid()) {
                maxFxCount = std::max(maxFxCount, static_cast<size_t>(it->first) + 1);
                break;
            }
        }
    }

    for (MixerChannelItem* item : m_mixerChannelList) {
        //! NOTE: skip a channel that hasn't loaded its own real data yet (e.g. an
        //! instrument track whose playback()->params() promise hasn't resolved) - padding
        //! it now, before it has anything of its own, is not just premature but actively
        //! wrong once its real data does arrive (see outputParamsLoaded()'s doc comment)
        if (!item->outputParamsLoaded()) {
            continue;
        }

        item->setOutputResourceItemCount(maxFxCount + 1 /* + 1 blank slot */);
    }
}

AudioOutputParams MixerPanelModel::effectiveMasterOutputParams() const
{
    AudioOutputParams params = audioSettings()->masterAudioOutputParams();
    params.forceMute = controller()->isMasterOutputForceMuted();
    if (params.forceMute) {
        params.muted = true;
    }

    return params;
}

void MixerPanelModel::updateAuxSendItemCount()
{
    size_t maxRealSendCount = 0;

    for (const MixerChannelItem* item : m_mixerChannelList) {
        size_t realCount = 0;
        for (const AuxSendItem* auxSendItem : item->auxSendItems()) {
            if (auxSendItem->auxIndex() != AuxSendItem::NO_BUS) {
                ++realCount;
            }
        }

        maxRealSendCount = std::max(maxRealSendCount, realCount);
    }

    for (MixerChannelItem* item : m_mixerChannelList) {
        //! NOTE: skip a channel that hasn't loaded its own real data yet - see the
        //! matching note in updateOutputResourceItemCount(). Unlike fx slots (positioned
        //! by each fx's own fixed chainOrder key), aux-send slots are positioned by a
        //! "next free slot" allocator, so padding an unloaded channel here would let
        //! those blanks permanently claim the front slots ahead of the real entries that
        //! only get placed once this channel's own async load actually completes
        if (!item->outputParamsLoaded()) {
            continue;
        }

        item->setAuxSendItemCount(maxRealSendCount + 1 /* + 1 blank slot */);
    }
}

INotationProjectPtr MixerPanelModel::currentProject() const
{
    return context()->currentProject();
}

IProjectAudioSettingsPtr MixerPanelModel::audioSettings() const
{
    return currentProject() ? currentProject()->audioSettings() : nullptr;
}

IProjectVideoSettingsPtr MixerPanelModel::videoSettings() const
{
    return currentProject() ? currentProject()->videoSettings() : nullptr;
}

IProjectUndoStackPtr MixerPanelModel::projectUndoStack() const
{
    return currentProject() ? currentProject()->undoStack() : nullptr;
}

INotationPlaybackPtr MixerPanelModel::notationPlayback() const
{
    return currentProject() ? currentProject()->masterNotation()->playback() : nullptr;
}

INotationPartsPtr MixerPanelModel::masterNotationParts() const
{
    return currentProject() ? currentProject()->masterNotation()->parts() : nullptr;
}

muse::ui::NavigationSection* MixerPanelModel::navigationSection() const
{
    return m_navigationSection;
}

void MixerPanelModel::setNavigationSection(muse::ui::NavigationSection* navigationSection)
{
    if (m_navigationSection == navigationSection) {
        return;
    }

    m_navigationSection = navigationSection;
    emit navigationSectionChanged();
}

int MixerPanelModel::navigationOrderStart() const
{
    return m_navigationOrderStart;
}

void MixerPanelModel::setNavigationOrderStart(int navigationOrderStart)
{
    if (m_navigationOrderStart == navigationOrderStart) {
        return;
    }

    m_navigationOrderStart = navigationOrderStart;
    emit navigationOrderStartChanged();

    updateItemsPanelsOrder();
}
