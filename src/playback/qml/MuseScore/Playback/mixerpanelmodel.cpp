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

QVariant MixerPanelModel::data(const QModelIndex& index, int role) const
{
    if (!index.isValid() || index.row() >= rowCount() || role != ChannelItemRole) {
        return QVariant();
    }

    return QVariant::fromValue(m_mixerChannelList.at(index.row()));
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

    addInstrumentTrack(notationPlayback()->metronomeTrackId());

    if (configuration()->areAuxChannelsVisible()) {
        const auto& auxTrackIdMap = controller()->auxTrackIdMap();
        for (auto it = auxTrackIdMap.cbegin(); it != auxTrackIdMap.cend(); ++it) {
            m_mixerChannelList.push_back(buildAuxChannelItem(it->first, it->second));
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
            addItem(buildAuxChannelItem(auxIt->first, trackId), m_mixerChannelList.size() - 1);
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

    //! NOTE: aux channel items load their initial output params (incl. fx chain) synchronously,
    //! before they are inserted here - re-sync now so the new item gets the same fx slot
    //! count as every other channel (instrument items already get this via their own,
    //! separately-resolved loadOutputParams() call, so this is a no-op for them)
    updateOutputResourceItemCount();
    updateAuxSendItemCount();

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

    updateOutputResourceItemCount();

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
    qDeleteAll(m_mixerChannelList);
    m_mixerChannelList.clear();
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

    configuration()->areAuxChannelsVisibleChanged().onReceive(this, [this](bool visible) {
        const auto& auxMap = controller()->auxTrackIdMap();

        if (visible) {
            for (auto it = auxMap.cbegin(); it != auxMap.cend(); ++it) {
                if (!findChannelItem(it->second)) {
                    addItem(buildAuxChannelItem(it->first, it->second), masterChannelIndex());
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

    connect(item, &MixerChannelItem::controlParamsChanged, this, [this, trackId](const AudioOutputParams& params) {
        playback()->setControlParams(trackId, params.control());
    });

    connect(item, &MixerChannelItem::fxChainParamsChanged, this, [this, trackId](const AudioOutputParams& params) {
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

    return item;
}

MixerChannelItem* MixerPanelModel::buildAuxChannelItem(aux_channel_idx_t index, const TrackId trackId)
{
    MixerChannelItem* item = new MixerChannelItem(this, MixerChannelItem::Type::Aux, true /*outputOnly*/, trackId);
    item->setPanelSection(m_navigationSection);
    item->setAuxIndex(index);
    item->loadSoloMuteState(audioSettings()->auxSoloMuteState(index));

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

    AudioOutputParams outParams = audioSettings()->auxOutputParams(index);
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

    connect(item, &MixerChannelItem::controlParamsChanged, this, [this, trackId](const AudioOutputParams& params) {
        playback()->setControlParams(trackId, params.control());
    });
    connect(item, &MixerChannelItem::fxChainParamsChanged, this, [this, trackId](const AudioOutputParams& params) {
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

    return item;
}

MixerChannelItem* MixerPanelModel::buildMasterChannelItem()
{
    MixerChannelItem* item = new MixerChannelItem(this, MixerChannelItem::Type::Master, true /*outputOnly*/, MASTER_TRACK_ID);
    item->setPanelSection(m_navigationSection);
    item->setTitle(muse::qtrc("playback", "Master"));

    AudioOutputParams outParams = audioSettings()->masterAudioOutputParams();
    loadOutputParams(item, outParams);

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
    });

    connect(item, &MixerChannelItem::fxChainParamsChanged, this, [this](const AudioOutputParams& params) {
        playback()->setMasterFxChainParams(params.fxChain);
    });

    connect(item, &MixerChannelItem::auxSendsParamsChanged, this, [this](const AudioOutputParams& params) {
        playback()->setMasterAuxSendsParams(params.auxSends);
        updateAuxSendItemCount();
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
        item->setOutputResourceItemCount(maxFxCount + 1 /* + 1 blank slot */);
    }
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
