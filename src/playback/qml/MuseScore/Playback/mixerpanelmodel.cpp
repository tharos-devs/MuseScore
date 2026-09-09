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
    for (MixerChannelItem* item : m_mixerChannelList) {
        if (item->selected()) {
            item->setColor(color);
        }
    }
}

void MixerPanelModel::resetColorForSelectedChannels()
{
    for (MixerChannelItem* item : m_mixerChannelList) {
        if (item->selected()) {
            item->setColor(QColor());
        }
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

    if (videoSettings() && videoSettings()->attachment().isValid()) {
        m_mixerChannelList.push_back(buildVideoChannelItem());
    }

    addInstrumentTrack(notationPlayback()->metronomeTrackId());

    if (configuration()->areAuxChannelsVisible()) {
        const auto& auxTrackIdMap = controller()->auxTrackIdMap();
        for (aux_channel_idx_t index : sortedAuxIndices()) {
            m_mixerChannelList.push_back(buildAuxChannelItem(index, auxTrackIdMap.at(index)));
        }
    }

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
        if (configuration()->areAuxChannelsVisible()) {
            bool isGroupBus = controller()->isAuxBusGroup(auxIt->first);
            addItem(buildAuxChannelItem(auxIt->first, trackId), resolveAuxInsertIndex(auxIt->first, isGroupBus));
        }
    }
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
            //! NOTE: same FX-then-Group ordering as reloadItems() - see sortedAuxIndices()
            for (aux_channel_idx_t index : sortedAuxIndices()) {
                TrackId trackId = auxMap.at(index);
                if (!findChannelItem(trackId)) {
                    addItem(buildAuxChannelItem(index, trackId), masterChannelIndex());
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
    // - metronome channel is placed to the immediate left of the master (or auxes if visible)
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
    //! NOTE: single source of truth for "FX-type buses first (ascending index), then
    //! Group-type buses (ascending index)" - shared by reloadItems() and the
    //! areAuxChannelsVisibleChanged handler in setupConnections(), which both need to
    //! (re)build the aux section of the channel list in this same order. Must stay
    //! consistent with resolveAuxInsertIndex() below, which resolves the equivalent
    //! position for a single newly-added bus rather than the whole list at once.
    std::vector<aux_channel_idx_t> indices;
    const auto& auxTrackIdMap = controller()->auxTrackIdMap();
    indices.reserve(auxTrackIdMap.size());

    for (const auto& pair : auxTrackIdMap) {
        if (!controller()->isAuxBusGroup(pair.first)) {
            indices.push_back(pair.first);
        }
    }
    for (const auto& pair : auxTrackIdMap) {
        if (controller()->isAuxBusGroup(pair.first)) {
            indices.push_back(pair.first);
        }
    }

    return indices;
}

int MixerPanelModel::resolveAuxInsertIndex(aux_channel_idx_t index, bool isGroupBus) const
{
    //! NOTE: FX-type aux buses are grouped together (ascending by index), followed by all
    //! Group-type buses (also ascending by index), then the master channel. Mirrors
    //! sortedAuxIndices()'s ordering, resolving the equivalent insert position for a single
    //! newly-added bus rather than the whole list at once - without the index
    //! comparison below, a bus recreated at a lower, freed index (e.g. "FX3" after deleting
    //! the old FX3 and re-adding) would always land after every existing same-type sibling
    //! instead of in its correct sorted position, making the on-screen order depend on
    //! WHETHER a bus arrived via this incremental path or a full reload, rather than being a
    //! stable function of the current bus set.
    if (isGroupBus) {
        for (int i = 0; i < m_mixerChannelList.size(); ++i) {
            const MixerChannelItem* item = m_mixerChannelList[i];
            if (item->type() == MixerChannelItem::Type::Master) {
                return i;
            }
            if (item->type() == MixerChannelItem::Type::Aux && item->isGroupBus() && item->auxBusIndex() > index) {
                return i;
            }
        }

        return masterChannelIndex();
    }

    for (int i = 0; i < m_mixerChannelList.size(); ++i) {
        const MixerChannelItem* item = m_mixerChannelList[i];
        if (item->type() == MixerChannelItem::Type::Master
            || (item->type() == MixerChannelItem::Type::Aux && item->isGroupBus())) {
            return i;
        }
        if (item->type() == MixerChannelItem::Type::Aux && item->auxBusIndex() > index) {
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

        //! NOTE Only persist volume/balance here; solo/mute/forceMute are owned by
        //! INotationSoloMuteState and must not be echoed back into the saved output params.
        AudioOutputParams outParams = audioSettings()->trackOutputParams(instrumentTrackId);
        outParams.volume = params.volume;
        outParams.balance = params.balance;
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
