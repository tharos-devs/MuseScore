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

#include <QCoreApplication>
#include <QCursor>
#include <QGuiApplication>
#include <QPointer>
#include <QTimer>

#include "async/notifylist.h"
#include "defer.h"
#include "log.h"
#include "translation.h"

#include "channeleditundo.h"

#include "notation/imasternotation.h"
#include "notation/inotation.h"
#include "notation/inotationsolomutestate.h"
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

MixerPanelModel::MixerPanelModel(QObject* parent)
    : QAbstractListModel(parent), muse::Contextable(muse::iocCtxForQmlObject(this))
{
}

MixerPanelModel::~MixerPanelModel()
{
    //! NOTE: closed while dragging an effect
    restoreDragCursor();

    //! NOTE: the channel items are children of this model, but the views only deleteLater() the delegates showing
    //! them once destroyed themselves, i.e. after this model (declared first in MixerPanel.qml) - so the items are
    //! detached from it, and only queued for deletion one event loop iteration later, behind those delegates
    const QList<MixerChannelItem*> items = m_mixerChannelList;
    for (MixerChannelItem* item : items) {
        item->setParent(nullptr);
    }

    QMetaObject::invokeMethod(qApp, [items]() {
        deleteItemsLater(items);
    }, Qt::QueuedConnection);
}

//! NOTE: deleting a channel item while a delegate still shows it nulls that delegate's channelItem,
//! re-evaluating every one of its bindings against null (hundreds of TypeErrors with all the Mixer
//! sections) - so items must only be queued for deletion after the views have queued their delegates'
void MixerPanelModel::deleteItemsLater(const QList<MixerChannelItem*>& items)
{
    //! NOTE: no need to disconnect them: clear() already did, and a destroyed model loses its connections anyway
    for (MixerChannelItem* item : items) {
        item->deleteLater();
    }
}

void MixerPanelModel::componentComplete()
{
    init();
}

void MixerPanelModel::init()
{
    //! NOTE Must be set from Qml
    DO_ASSERT(m_navigationSection);

    m_soundTrackChannels = makeSoundTrackChannels(iocContext());

    controller()->playbackInitedChanged().onReceive(this, [this](bool) {
        reload();
    });

    m_auxBusEditor = AuxBusEditor::shared(iocContext());

    controller()->trackAdded().onReceive(this, [this](const TrackId trackId) {
        onTrackAdded(trackId);
    });

    //! NOTE: a bus once a redo restored it (see AuxBusEditor::onTrackAdded())
    m_auxBusEditor->auxBusAdded().onReceive(this, [this](aux_channel_idx_t index, const TrackId trackId) {
        if (isAuxBusVisible(index)) {
            addItem(buildAuxChannelItem(index, trackId), resolveAuxInsertIndex(index, controller()->isAuxBusGroup(index)));
        }
    });

    controller()->trackRemoved().onReceive(this, [this](const TrackId trackId) {
        removeItem(trackId);
    });

    //! NOTE: a kind of channels shown or hidden ("…" menu)
    configuration()->channelCategoryVisibleChanged().onReceive(this, [this](ChannelListPanel panel, ChannelCategory, bool) {
        if (panel == ChannelListPanel::Mixer) {
            reload();
        }
    });

    reload();
}

bool MixerPanelModel::isCategoryVisible(ChannelCategory category) const
{
    return configuration()->isChannelCategoryVisible(ChannelListPanel::Mixer, category);
}

bool MixerPanelModel::isAuxBusVisible(aux_channel_idx_t index) const
{
    return isCategoryVisible(controller()->isAuxBusGroup(index) ? ChannelCategory::AuxGroup : ChannelCategory::AuxFx);
}

bool MixerPanelModel::isInstrumentTrackVisible(const InstrumentTrackId& instrumentTrackId) const
{
    const bool isMetronome = notationPlayback() && instrumentTrackId == notationPlayback()->metronomeTrackId();
    return isCategoryVisible(isMetronome ? ChannelCategory::Metronome : ChannelCategory::Instruments);
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
    ChannelSelection::select(m_mixerChannelList, item, extendSelection, rangeSelection, m_selectionAnchorIndex,
                             [](const MixerChannelItem* channel) {
        return channel->type() == MixerChannelItem::Type::PrimaryInstrument
               || channel->type() == MixerChannelItem::Type::SecondaryInstrument
               || channel->type() == MixerChannelItem::Type::Aux;
    });
}

void MixerPanelModel::setColorForSelectedChannels(const QColor& color)
{
    applyColorToSelectedChannels(color, muse::TranslatableString("undoableAction", "Change Mixer channel color"));
}

void MixerPanelModel::resetColorForSelectedChannels()
{
    applyColorToSelectedChannels(QColor(), muse::TranslatableString("undoableAction", "Reset Mixer channel color"));
}

//! NOTE: the channel picks it up from the audio settings, like the other channels' colors
void MixerPanelModel::setSoundTrackColor(MixerChannelItem* item, const QColor& color)
{
    if (const SoundTrackChannel* channel = item ? findSoundTrackChannel(m_soundTrackChannels, item->type()) : nullptr) {
        channel->setColor(color);
    }
}

void MixerPanelModel::resetSoundTrackColor(MixerChannelItem* item)
{
    setSoundTrackColor(item, QColor());
}

void MixerPanelModel::applyColorToSelectedChannels(const QColor& color, const muse::TranslatableString& actionName)
{
    std::vector<ChannelColorChange::Target> targets;

    for (const MixerChannelItem* item : std::as_const(m_mixerChannelList)) {
        if (!item->selected()) {
            continue;
        }

        if (item->type() == MixerChannelItem::Type::Aux) {
            targets.push_back({ InstrumentTrackId(), item->auxBusIndex() });
        } else {
            targets.push_back({ item->instrumentTrackId(), std::nullopt });
        }
    }

    //! NOTE: the items pick the new colors up from the audio settings (see setupConnections())
    ChannelColorChange::apply(audioSettings(), projectUndoStack(), targets, color, actionName);
}

bool MixerPanelModel::dropFx(MixerChannelItem* source, int sourceIndex, MixerChannelItem* target, int insertIndex, bool duplicate)
{
    if (!source || !target || source == target) {
        return false;
    }

    const QList<OutputResourceItem*> sourceItems = source->outputResourceItemList();
    if (sourceIndex < 0 || sourceIndex >= sourceItems.size() || sourceItems[sourceIndex]->isBlank()) {
        return false;
    }

    OutputResourceItem* sourceItem = sourceItems[sourceIndex];

    const ChannelEditUndo::ChannelFxChains before {
        { source, source->outputParams().fxChain },
        { target, target->outputParams().fxChain },
    };

    //! NOTE: the effect with its settings (and on/off state): the target's own instance is created with them
    if (!target->insertFx(insertIndex, sourceItem->params())) {
        return false;
    }

    if (!duplicate) {
        sourceItem->setFx(muse::audio::AudioResourceMeta());
    }

    ChannelEditUndo::pushFxChainsUndo(projectUndoStack(),
                                      duplicate ? muse::TranslatableString("undoableAction", "Copy Mixer audio effect")
                                      : muse::TranslatableString("undoableAction", "Move Mixer audio effect"), before);

    return true;
}

void MixerPanelModel::setDragCursor(int cursorShape)
{
    const QCursor cursor(static_cast<Qt::CursorShape>(cursorShape));
    if (m_dragCursorSet) {
        QGuiApplication::changeOverrideCursor(cursor);
    } else {
        QGuiApplication::setOverrideCursor(cursor);
        m_dragCursorSet = true;
    }
}

void MixerPanelModel::restoreDragCursor()
{
    if (m_dragCursorSet) {
        QGuiApplication::restoreOverrideCursor();
        m_dragCursorSet = false;
    }
}

void MixerPanelModel::connectPickedChangeUndo(MixerChannelItem* item)
{
    ChannelEditUndo::connectPickedChanges(item, this, [this]() { return projectUndoStack(); },
                                          [this]() { return m_mixerChannelList; });
}

void MixerPanelModel::setMutedForSelectedChannels(bool muted)
{
    ChannelSelection::setMuted(m_mixerChannelList, muted);
}

void MixerPanelModel::setSoloForSelectedChannels(bool solo)
{
    ChannelSelection::setSolo(m_mixerChannelList, solo);
}

void MixerPanelModel::clearSelection()
{
    ChannelSelection::clear(m_mixerChannelList, m_selectionAnchorIndex);
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

    //! NOTE: the channel item itself is removed from m_mixerChannelList via the
    //! trackRemoved() signal this triggers (see init()), not here
    m_auxBusEditor->deleteAuxBus(channelItem->auxBusIndex());
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
    //! immediately follows it (another aux type, a sound track/metronome, or master). Deliberately
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
                || item->isSoundTrack()
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
    return m_auxBusEditor && m_auxBusEditor->canAddAuxBus();
}

void MixerPanelModel::addFxChannel()
{
    m_auxBusEditor->addAuxBus(false);
}

void MixerPanelModel::addGroupChannel()
{
    m_auxBusEditor->addAuxBus(true);
}

void MixerPanelModel::addFxChannelForSelectedTracks()
{
    m_auxBusEditor->addAuxBusForTracks(false, selectedInstrumentTrackIds());
}

void MixerPanelModel::addGroupChannelForSelectedTracks()
{
    m_auxBusEditor->addAuxBusForTracks(true, selectedInstrumentTrackIds());
}

QList<TrackId> MixerPanelModel::selectedInstrumentTrackIds() const
{
    QList<TrackId> result;
    for (const MixerChannelItem* item : std::as_const(m_mixerChannelList)) {
        bool isInstrument = item->type() == MixerChannelItem::Type::PrimaryInstrument
                            || item->type() == MixerChannelItem::Type::SecondaryInstrument;
        if (isInstrument && item->selected()) {
            result.push_back(item->trackId());
        }
    }

    return result;
}

//! NOTE: every channel type except Metronome
GlobalMuteSoloToggle::Channels MixerPanelModel::globalMuteSoloChannels() const
{
    GlobalMuteSoloToggle::Channels channels;
    for (MixerChannelItem* item : m_mixerChannelList) {
        if (item->type() != MixerChannelItem::Type::Metronome) {
            channels.push_back(item);
        }
    }

    return channels;
}

//! NOTE: live - true whenever ANY channel is currently muted, not just right after toggleGlobalMute() runs.
//! So muting a single channel via its own per-channel button lights up the global button too (see
//! connectGlobalMuteSoloAggregate()).
bool MixerPanelModel::globalMuteEngaged() const
{
    return GlobalMuteSoloToggle::muteEngaged(globalMuteSoloChannels());
}

bool MixerPanelModel::globalSoloEngaged() const
{
    return GlobalMuteSoloToggle::soloEngaged(globalMuteSoloChannels());
}

void MixerPanelModel::toggleGlobalMute()
{
    m_globalMuteSolo.toggleMute(globalMuteSoloChannels());

    //! NOTE: each setMuted() call already triggers globalMuteEngagedChanged() via
    //! connectGlobalMuteSoloAggregate() - this covers the no-op case (nothing to mute/restore)
    emit globalMuteEngagedChanged();
}

void MixerPanelModel::toggleGlobalSolo()
{
    m_globalMuteSolo.toggleSolo(globalMuteSoloChannels());

    emit globalSoloEngagedChanged();
}

void MixerPanelModel::connectGlobalMuteSoloAggregate(MixerChannelItem* item)
{
    connect(item, &MixerChannelItem::mutedChanged, this, [this]() {
        emit globalMuteEngagedChanged();
    });

    //! NOTE: the global mute state depends on forceMute too - e.g. soloing one channel
    //! force-mutes every other one without changing their own muted flag, which must NOT
    //! light up the global Mute button (see GlobalMuteSoloToggle::muteEngaged())
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

    QList<MixerChannelItem*> oldItems;

    DEFER {
        endResetModel();
        emit rowCountChanged();

        //! NOTE: only once the views have released the delegates showing them
        deleteItemsLater(oldItems);
    };

    oldItems = clear();

    if (!controller()->isPlaybackInited()) {
        return;
    }

    const auto& instrumentTrackIdMap = controller()->instrumentTrackIdMap();

    auto addInstrumentTrack = [this, &instrumentTrackIdMap](const InstrumentTrackId& instrumentTrackId, bool isPrimary = true) {
        auto search = instrumentTrackIdMap.find(instrumentTrackId);
        if (search == instrumentTrackIdMap.cend() || !isInstrumentTrackVisible(instrumentTrackId)) {
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

    //! NOTE: aux buses come before the sound tracks/metronome (all placed immediately left of
    //! master instead) so the aux section stays adjacent to the instrument tracks
    //! it routes from, rather than sitting between channels (video, reference audio, metronome)
    //! that have nothing to do with aux routing.
    const auto& auxTrackIdMap = controller()->auxTrackIdMap();
    for (aux_channel_idx_t index : sortedAuxIndices()) {
        if (isAuxBusVisible(index)) {
            m_mixerChannelList.push_back(buildAuxChannelItem(index, auxTrackIdMap.at(index)));
        }
    }

    for (const SoundTrackChannel& channel : m_soundTrackChannels) {
        const TrackId trackId = channel.trackId();
        if (trackId != INVALID_TRACK_ID && isCategoryVisible(channel.category)) {
            m_mixerChannelList.push_back(buildSoundTrackChannelItem(channel, trackId));
        }
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

    for (const SoundTrackChannel& channel : m_soundTrackChannels) {
        if (trackId == channel.trackId()) {
            if (indexOf(trackId) == INVALID_INDEX && isCategoryVisible(channel.category)) {
                addItem(buildSoundTrackChannelItem(channel, trackId), resolveSoundTrackInsertIndex(channel.type));
            }
            return;
        }
    }

    const IPlaybackController::InstrumentTrackIdMap& instrumentTracks = controller()->instrumentTrackIdMap();
    auto instrumentIt = std::find_if(instrumentTracks.cbegin(), instrumentTracks.cend(), [trackId](const auto& pair) {
        return pair.second == trackId;
    });

    if (instrumentIt != instrumentTracks.end()) {
        const InstrumentTrackId& instrumentTrackId = instrumentIt->first;
        if (!isInstrumentTrackVisible(instrumentTrackId)) {
            return;
        }

        const Part* part = masterNotationParts()->part(instrumentTrackId.partId);
        bool isPrimary = part ? part->instrument()->id() == instrumentTrackId.instrumentId : true;
        MixerChannelItem* item = buildInstrumentChannelItem(trackId, instrumentTrackId, isPrimary);
        int index = resolveInsertIndex(instrumentTrackId);

        addItem(item, index);
        return;
    }

    //! NOTE: an aux bus: see auxBusAdded() in init()
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

QList<MixerChannelItem*> MixerPanelModel::clear()
{
    TRACEFUNC;

    m_masterChannelItem = nullptr;
    QList<MixerChannelItem*> items = std::move(m_mixerChannelList);
    m_mixerChannelList.clear();

    //! NOTE Disconnect immediately so a not-yet-destroyed item can't fire stale
    //! controlParamsChanged/soloMuteStateChanged signals (they are only deleted later).
    //! Only from this model, which holds all their C++ connections: a full disconnect()
    //! would also cut their destroyed() signal (Qt warns about it)
    for (MixerChannelItem* item : items) {
        item->disconnect(this);
    }

    //! NOTE The channel list is being fully rebuilt, so any stored index would point at the wrong
    //! (or a stale/deleted) channel.
    m_selectionAnchorIndex = INVALID_INDEX;

    //! NOTE: same reasoning - a remembered mute/solo snapshot would otherwise reference
    //! trackIds from the channel list that no longer exists
    m_globalMuteSolo.clear();
    emit globalMuteEngagedChanged();
    emit globalSoloEngagedChanged();

    return items;
}

void MixerPanelModel::setupConnections()
{
    //! NOTE: an instrument track's own Mute/Solo can also be toggled from outside the Mixer
    //! (the Timeline's row buttons) - pick it up like the aux buses do (see MixerChannelItem::bindAuxBus()),
    //! so the channel's buttons and the global Mute/Solo header buttons (fed by its mutedChanged/soloChanged,
    //! see connectGlobalMuteSoloAggregate()) follow. Same no-op when it came from the Mixer.
    const notation::INotationPtr currentNotation = context()->currentNotation();
    if (currentNotation && currentNotation->soloMuteState()) {
        const notation::INotationSoloMuteState* soloMuteState = currentNotation->soloMuteState().get();
        currentNotation->soloMuteState()->trackSoloMuteStateChanged().onReceive(
            this, [this, soloMuteState](const InstrumentTrackId& instrumentTrackId,
                                        const notation::INotationSoloMuteState::SoloMuteState& newSoloMuteState) {
            //! NOTE: solo/mute states are per notation (score/part) - ignore a notation that's
            //! no longer the current one
            const notation::INotationPtr current = context()->currentNotation();
            if (!current || current->soloMuteState().get() != soloMuteState) {
                return;
            }

            TrackId trackId = muse::value(controller()->instrumentTrackIdMap(), instrumentTrackId, INVALID_TRACK_ID);
            if (MixerChannelItem* item = findChannelItem(trackId)) {
                item->loadSoloMuteState(newSoloMuteState);
            }
        }, Asyncable::Mode::SetReplace);
    }

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

    //! NOTE: an instrument channel follows its own sound and aux sends, see MixerChannelItem::bindInstrumentTrack()

    playback()->fxChainParamsChanged().onReceive(this, [this](const TrackId trackId, const AudioFxChain& params) {
        if (MixerChannelItem* item = findChannelItem(trackId)) {
            //! NOTE Each channel type keeps its output params in its own place: reloading the others'
            //! (default ones, for an invalid instrument track id) would reset its fader/pan/gain/color/sends
            AudioOutputParams outParams;
            if (const SoundTrackChannel* channel = findSoundTrackChannel(m_soundTrackChannels, item->type())) {
                outParams = channel->outputParams();
            } else if (item->type() == MixerChannelItem::Type::Aux) {
                outParams = audioSettings()->auxOutputParams(item->auxBusIndex());
            } else {
                outParams = audioSettings()->trackOutputParams(item->instrumentTrackId());
            }

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

    for (const SoundTrackChannel& channel : m_soundTrackChannels) {
        if (std::optional<async::Notification> settingsChanged = channel.settingsChanged()) {
            settingsChanged->onNotify(this, [this, channel]() {
                onSoundTrackSettingsChanged(channel);
            });
        }
    }

    //! NOTE: the instrument tracks', metronome's, aux buses' and sound tracks' colors are changed through the audio
    //! settings, from the Mixer itself, the Timeline or the Track list (see ChannelColorChange) - pick them up from there
    if (audioSettings()) {
        audioSettings()->settingsChanged().onNotify(this, [this]() {
            for (MixerChannelItem* item : m_mixerChannelList) {
                std::optional<QColor> color;

                if (item->type() == MixerChannelItem::Type::Aux) {
                    color = audioSettings()->auxOutputParams(item->auxBusIndex()).color;
                } else if (item->type() == MixerChannelItem::Type::PrimaryInstrument
                           || item->type() == MixerChannelItem::Type::SecondaryInstrument
                           || item->type() == MixerChannelItem::Type::Metronome) {
                    //! NOTE: the metronome's is changed from the Track list's Metronome row
                    const engraving::InstrumentTrackId& trackId = item->instrumentTrackId();
                    if (audioSettings()->trackHasExistingOutputParams(trackId)) {
                        color = audioSettings()->trackOutputParams(trackId).color;
                    }
                } else if (const SoundTrackChannel* channel = findSoundTrackChannel(m_soundTrackChannels, item->type())) {
                    //! NOTE: e.g. changed from the Track list's Video/Reference audio row
                    color = channel->outputParams().color;
                }

                if (color && item->color() != *color) {
                    item->setColor(*color);
                }
            }
        }, Asyncable::Mode::SetReplace);
    }

    for (const SoundTrackChannel& channel : m_soundTrackChannels) {
        channel.muteStateChanged().onReceive(this, [this, trackId = channel.trackId](bool muted, bool forceMute) {
            if (MixerChannelItem* item = findChannelItem(trackId())) {
                item->loadMuteForceMuteState(muted, forceMute);
            }
        });
    }
}

void MixerPanelModel::onSoundTrackSettingsChanged(const SoundTrackChannel& channel)
{
    TRACEFUNC;

    //! NOTE The channel itself follows the sound track (see onTrackAdded()/trackRemoved()), only its
    //! solo/mute state is shared with its source's own UI (e.g. the Video panel's "M" button) and must be refreshed
    if (!channel.hasSource()) {
        return;
    }

    if (MixerChannelItem* item = findChannelItem(channel.trackId())) {
        channel.loadMuteState(item);
    }
}

int MixerPanelModel::resolveSoundTrackInsertIndex(MixerChannelItem::Type type) const
{
    //! NOTE: the sound tracks' channels are immediately left of the metronome (or master), in m_soundTrackChannels' order
    auto displayOrder = [this](MixerChannelItem::Type soundTrackType) {
        for (size_t i = 0; i < m_soundTrackChannels.size(); ++i) {
            if (m_soundTrackChannels[i].type == soundTrackType) {
                return i;
            }
        }
        return m_soundTrackChannels.size();
    };

    const size_t ownOrder = displayOrder(type);

    for (int i = 0; i < m_mixerChannelList.size(); ++i) {
        const MixerChannelItem* item = m_mixerChannelList[i];
        if (item->type() == MixerChannelItem::Type::Metronome || item->type() == MixerChannelItem::Type::Master
            || (item->isSoundTrack() && displayOrder(item->type()) > ownOrder)) {
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
    // - metronome channel is placed to the immediate left of the master (the sound
    //   tracks, if present, go immediately left of metronome in turn - see
    //   resolveSoundTrackInsertIndex()); aux buses, if visible, sit further left still,
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

std::vector<aux_channel_idx_t> MixerPanelModel::sortedAuxIndices() const
{
    //! NOTE: the order reloadItems() builds the aux section of the channel list in (also the
    //! order of the aux send menus and of the Track list's buses). Must stay
    //! consistent with resolveAuxInsertIndex() below, which resolves the equivalent
    //! position for a single newly-added bus rather than the whole list at once.
    return controller()->sortedAuxBusIndices();
}

int MixerPanelModel::resolveAuxInsertIndex(aux_channel_idx_t index, bool isGroupBus) const
{
    //! NOTE: FX-type aux buses are grouped together (by sort order), followed by all
    //! Group-type buses (also by sort order), then the sound tracks/metronome (if present), then
    //! the master channel. Mirrors sortedAuxIndices()'s ordering, resolving the
    //! equivalent insert position for a single newly-added bus rather than the whole
    //! list at once - without the sort-order comparison below, a bus recreated at a
    //! lower, freed index (e.g. "FX3" after deleting the old FX3 and re-adding), or one
    //! reloading from a saved project with a persisted (possibly drag-and-drop
    //! reordered) position, would always land after every existing same-type sibling
    //! instead of in its correct sorted position, making the on-screen order depend on
    //! WHETHER a bus arrived via this incremental path or a full reload, rather than
    //! being a stable function of the current bus set and its persisted order. Stopping
    //! at the sound tracks/metronome too (not just master) keeps aux buses added after those
    //! already exist from landing on the wrong side of them.
    int ownOrder = controller()->auxBusSortOrder(index);

    if (isGroupBus) {
        for (int i = 0; i < m_mixerChannelList.size(); ++i) {
            const MixerChannelItem* item = m_mixerChannelList[i];
            if (item->type() == MixerChannelItem::Type::Master
                || item->isSoundTrack()
                || item->type() == MixerChannelItem::Type::Metronome) {
                return i;
            }
            if (item->type() == MixerChannelItem::Type::Aux && item->isGroupBus()
                && controller()->auxBusSortOrder(item->auxBusIndex()) > ownOrder) {
                return i;
            }
        }

        return masterChannelIndex();
    }

    for (int i = 0; i < m_mixerChannelList.size(); ++i) {
        const MixerChannelItem* item = m_mixerChannelList[i];
        if (item->type() == MixerChannelItem::Type::Master
            || item->isSoundTrack()
            || item->type() == MixerChannelItem::Type::Metronome
            || (item->type() == MixerChannelItem::Type::Aux && item->isGroupBus())) {
            return i;
        }
        if (item->type() == MixerChannelItem::Type::Aux && controller()->auxBusSortOrder(item->auxBusIndex()) > ownOrder) {
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
    item->setPanelSection(m_navigationSection);
    item->bindInstrumentTrack(instrumentTrackId);

    connect(item, &MixerChannelItem::outputParamsReceived, this, [this]() {
        updateOutputResourceItemCount();
        updateAuxSendItemCount();
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

    connect(item, &MixerChannelItem::fxChainParamsChanged, this, [this]() {
        updateOutputResourceItemCount();
    });

    connect(item, &MixerChannelItem::auxSendsParamsChanged, this, [this]() {
        updateAuxSendItemCount();
    });

    connectPickedChangeUndo(item);

    connectGlobalMuteSoloAggregate(item);
    ChannelEditUndo::connectContinuousChanges(item, this, [this]() { return projectUndoStack(); });

    return item;
}

MixerChannelItem* MixerPanelModel::buildAuxChannelItem(aux_channel_idx_t index, const TrackId trackId)
{
    MixerChannelItem* item = new MixerChannelItem(this, MixerChannelItem::Type::Aux, true /*outputOnly*/, trackId);
    item->setPanelSection(m_navigationSection);
    item->bindAuxBus(index);

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

    //! NOTE: its output params, and their way to the engine and the project, are the item's (see bindAuxBus()):
    //! only its slots are lined up with the other channels' here
    updateOutputResourceItemCount();
    updateAuxSendItemCount();

    item->subscribeOnTrackAudioSignalChanges();

    connect(item, &MixerChannelItem::fxChainParamsChanged, this, [this]() {
        updateOutputResourceItemCount();
    });
    connect(item, &MixerChannelItem::auxSendsParamsChanged, this, [this]() {
        updateAuxSendItemCount();
    });
    connect(item, &MixerChannelItem::outputParamsReceived, this, [this]() {
        updateOutputResourceItemCount();
        updateAuxSendItemCount();
    });
    connectPickedChangeUndo(item);

    connectGlobalMuteSoloAggregate(item);
    ChannelEditUndo::connectContinuousChanges(item, this, [this]() { return projectUndoStack(); });

    return item;
}

MixerChannelItem* MixerPanelModel::buildSoundTrackChannelItem(const SoundTrackChannel& channel, const TrackId trackId)
{
    MixerChannelItem* item = new MixerChannelItem(this, channel.type, true /*outputOnly*/, trackId);
    item->setPanelSection(m_navigationSection);
    item->setTitle(channel.title());

    //! NOTE Solo/mute aren't part of the persisted output params (loadOutputParams() ignores them)
    channel.loadMuteState(item);
    loadOutputParams(item, channel.outputParams());

    item->subscribeOnTrackAudioSignalChanges();

    channel.connectItem(item, this, [this]() { return playback().get(); }, [this]() { return projectUndoStack(); });

    connect(item, &MixerChannelItem::fxChainParamsChanged, this, [this, trackId](const AudioOutputParams& params) {
        updateOutputResourceItemCount();
        playback()->setFxChainParams(trackId, params.fxChain);
    });

    connect(item, &MixerChannelItem::auxSendsParamsChanged, this, [this, trackId](const AudioOutputParams& params) {
        playback()->setAuxSendsParams(trackId, params.auxSends);
        updateAuxSendItemCount();
    });

    connectGlobalMuteSoloAggregate(item);

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
        playback()->setMasterControlParams(params.control());

        AudioOutputParams outParams = audioSettings()->masterAudioOutputParams();
        outParams.volume = params.volume;
        outParams.balance = params.balance;
        outParams.gain = params.gain;
        outParams.eq = params.eq;
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
    ChannelEditUndo::connectContinuousChanges(item, this, [this]() { return projectUndoStack(); });

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
    return audioSettings()->masterAudioOutputParams();
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
