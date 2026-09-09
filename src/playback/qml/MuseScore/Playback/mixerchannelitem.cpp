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

#include "mixerchannelitem.h"

#include <algorithm>

#include <QRandomGenerator>
#include <QTimer>

#include "defer.h"
#include "translation.h"
#include "log.h"

#include "notation/imasternotation.h" // IWYU pragma: keep
#include "notation/inotationautomation.h"
#include "notation/inotationplayback.h"

#include "project/inotationproject.h"

using namespace mu::playback;
using namespace muse;
using namespace muse::audio;
using namespace mu::project;

static constexpr volume_dbfs_t MAX_DISPLAYED_DBFS = volume_dbfs_t::make(0.f);   // 100%
static constexpr volume_dbfs_t MIN_DISPLAYED_DBFS = volume_dbfs_t::make(-60.f); // 0%

static constexpr float BALANCE_SCALING_FACTOR = 100.f;

static constexpr int OUTPUT_RESOURCE_COUNT_LIMIT = 5;
static constexpr int AUX_SEND_SLOT_LIMIT = 5;

static const std::string VSTFX_EDITOR_ACTION("action://vst/fx_editor");
static const std::string VSTI_EDITOR_ACTION("action://vst/instrument_editor");

static const std::string TRACK_ID_KEY("trackId");
static const std::string RESOURCE_ID_KEY("resourceId");
static const std::string CHAIN_ORDER_KEY("chainOrder");

//! NOTE: the aux bus's own channel strip is always titled positionally ("FX N"/"Group N"),
//! regardless of any fx loaded on that bus (see PlaybackController::addAuxTrack/
//! resolveAuxTrackTitle, considerFx=false) - aux-send slots must show that same name for
//! consistency, so this deliberately does not use IPlaybackController::auxChannelName(),
//! which is fx-aware
static QString auxBusPositionalName(aux_channel_idx_t displayNumber, bool isGroupBus)
{
    //! NOTE: each literal is passed directly to qtrc() (not via a ternary as the argument) -
    //! translation-extraction tooling scans for string-literal arguments to qtrc()/mtrc()/
    //! trc() calls, and a computed/ternary argument can make it fail to pick up one or both
    //! strings as translatable
    if (isGroupBus) {
        return muse::qtrc("playback", "Group %1").arg(displayNumber);
    }

    return muse::qtrc("playback", "FX %1").arg(displayNumber);
}

//! NOTE: a real, assigned-but-silent send (bypassed and/or its knob pulled to 0%) is
//! {signalAmount: 0.f, active: false} - the SAME value a default-constructed AuxSendParams
//! has. Using -1.f (outside the valid [0;1] gain range) as the "never assigned" sentinel
//! instead lets a reload correctly tell the two apart; the engine never acts on this value
//! for a blank entry since it's always paired with active=false (see writeTrackToAuxBuffers,
//! which skips non-active sends before ever reading signalAmount)
static AuxSendParams blankAuxSendParams()
{
    return AuxSendParams { -1.f, false };
}

static bool isBlankAuxSend(const AuxSendParams& params)
{
    return params == blankAuxSendParams();
}

//! NOTE: growing this vector must pad new positions with the blank sentinel, not the
//! natural zero-initialized default - otherwise a gap introduced by resizing to reach a
//! higher bus index would be indistinguishable from a real, silent-but-assigned send
static void resizeAuxSendsWithBlankPadding(AuxSendsParams& auxSends, size_t newSize)
{
    size_t oldSize = auxSends.size();
    auxSends.resize(newSize);

    for (size_t i = oldSize; i < newSize; ++i) {
        auxSends[i] = blankAuxSendParams();
    }
}

//! NOTE: fxChain can (and, once any slot's params round-trip through the engine, normally
//! does) contain blank/padding entries alongside real ones - the chain's own highest key can
//! itself be a blank slot. Callers that need "how many slots are actually required by real
//! content" must look at the highest VALID entry, not fxChain.size() or its highest key.
static size_t requiredOutputResourceItemCount(const audio::AudioFxChain& fxChain)
{
    for (auto it = fxChain.crbegin(); it != fxChain.crend(); ++it) {
        if (it->second.isValid()) {
            return static_cast<size_t>(it->first) + 1;
        }
    }

    return 0;
}

MixerChannelItem::MixerChannelItem(QObject* parent, Type type, bool outputOnly, audio::TrackId trackId)
    : QObject(parent), muse::Contextable(muse::iocCtxForQmlObject(this)),
    m_type(type),
    m_trackId(trackId),
    m_outputOnly(outputOnly),
    m_leftChannelPressure(MIN_DISPLAYED_DBFS),
    m_rightChannelPressure(MIN_DISPLAYED_DBFS)
{
    if (!m_outputOnly) {
        m_inputResourceItem = buildInputResourceItem();
    }

    m_auxSendItemListModel = new AuxSendItemListModel(this);

    m_panel = new ui::NavigationPanel(this);
    m_panel->setDirection(ui::NavigationPanel::Vertical);
    m_panel->setName("MixerChannelPanel " + QString::number(m_trackId));
    m_panel->accessible()->setName(muse::qtrc("playback", "Mixer channel panel %1").arg(m_trackId));
    m_panel->componentComplete();

    connect(this, &MixerChannelItem::mutedChanged, this, [this]() {
        //!NOTE Video channels reset their pressure via updateTimerState() in
        //!     setupVideoMeterAnimation() instead, since that also covers the
        //!     non-mute reasons (video not playing) the meter needs to go dark for.
        if (m_type != Type::Video && muted()) {
            resetAudioChannelsVolumePressure();
        }
    });

    if (m_type == Type::Video) {
        setupVideoMeterAnimation();
    }
}

MixerChannelItem::~MixerChannelItem()
{
    m_audioSignalChanges.disconnect(this);
}

MixerChannelItem::Type MixerChannelItem::type() const
{
    return m_type;
}

bool MixerChannelItem::isGroupBus() const
{
    return m_type == Type::Aux && playbackController()->isAuxBusGroup(m_auxBusIndex);
}

bool MixerChannelItem::isReverbBus() const
{
    return m_type == Type::Aux && m_auxBusIndex == REVERB_CHANNEL_IDX;
}

aux_channel_idx_t MixerChannelItem::auxBusIndex() const
{
    return m_auxBusIndex;
}

void MixerChannelItem::setAuxBusIndex(aux_channel_idx_t index)
{
    m_auxBusIndex = index;
}

TrackId MixerChannelItem::trackId() const
{
    return m_trackId;
}

const mu::engraving::InstrumentTrackId& MixerChannelItem::instrumentTrackId() const
{
    return m_instrumentTrackId;
}

void MixerChannelItem::setInstrumentTrackId(const mu::engraving::InstrumentTrackId& instrumentTrackId)
{
    m_instrumentTrackId = instrumentTrackId;

    updateHasAutomationFlags();
}

QString MixerChannelItem::title() const
{
    return m_title;
}

float MixerChannelItem::leftChannelPressure() const
{
    return m_leftChannelPressure;
}

float MixerChannelItem::rightChannelPressure() const
{
    return m_rightChannelPressure;
}

float MixerChannelItem::volumeLevel() const
{
    return m_volumeLevel;
}

float MixerChannelItem::volumeLevelMin() const
{
    return audio::VOLUME_DB_MIN.raw();
}

float MixerChannelItem::volumeLevelMax() const
{
    return audio::VOLUME_DB_MAX.raw();
}

int MixerChannelItem::balance() const
{
    return m_balance;
}

int MixerChannelItem::balanceMin() const
{
    return audio::BALANCE_MIN.raw() * BALANCE_SCALING_FACTOR;
}

int MixerChannelItem::balanceMax() const
{
    return audio::BALANCE_MAX.raw() * BALANCE_SCALING_FACTOR;
}

bool MixerChannelItem::hasVolumeAutomation() const
{
    return m_hasVolumeAutomation;
}

bool MixerChannelItem::hasBalanceAutomation() const
{
    return m_hasBalanceAutomation;
}

bool MixerChannelItem::solo() const
{
    return m_outParams.solo;
}

bool MixerChannelItem::muted() const
{
    return m_outParams.muted;
}

bool MixerChannelItem::forceMute() const
{
    return m_outParams.forceMute;
}

QColor MixerChannelItem::color() const
{
    return m_outParams.color;
}

bool MixerChannelItem::hasCustomColor() const
{
    return m_outParams.color.isValid();
}

bool MixerChannelItem::selected() const
{
    return m_selected;
}

muse::ui::NavigationPanel* MixerChannelItem::panel() const
{
    return m_panel;
}

void MixerChannelItem::setPanelOrder(int panelOrder)
{
    m_panel->setOrder(panelOrder);
}

void MixerChannelItem::setPanelSection(muse::ui::INavigationSection* section)
{
    m_panel->setSection(section);
}

void MixerChannelItem::setOutputResourceItemCount(size_t count)
{
    IF_ASSERT_FAILED(count >= requiredOutputResourceItemCount(m_outParams.fxChain)) {
        return;
    }

    count = std::min(count, static_cast<size_t>(OUTPUT_RESOURCE_COUNT_LIMIT));
    size_t itemsSize = static_cast<size_t>(m_outputResourceItems.size());

    if (itemsSize == count) {
        return;
    }

    if (itemsSize < count) {
        addBlankSlots(count - itemsSize);
    } else if (itemsSize > count) {
        removeBlankSlotsFromEnd(itemsSize - count);
    }
}

void MixerChannelItem::moveOutputResourceItem(int fromIndex, int toIndex)
{
    TRACEFUNC;

    if (m_fxChainUpdatePending) {
        // NOTE: a previous reorder's fxChainParamsChanged is still awaiting
        // its engine round-trip (cleared in loadOutputResourceItems below).
        // Applying another reorder now, before that round-trip lands, risks
        // the round-trip's own (by-then-stale) echo overwriting this newer
        // one once it does arrive. Requires starting and finishing an entire
        // second drag gesture faster than that round-trip, so this is a rare
        // defensive guard rather than something expected to trigger in
        // practice. Re-emitting outputResourceItemListChanged() rolls the QML
        // side back to the actual (unchanged) order, in case it already
        // showed an optimistic shift for this now-rejected drag.
        emit outputResourceItemListChanged();
        return;
    }

    QList<OutputResourceItem*> items = m_outputResourceItems.values();

    IF_ASSERT_FAILED(fromIndex >= 0 && fromIndex < items.size() && toIndex >= 0 && toIndex < items.size()) {
        return;
    }

    if (fromIndex == toIndex) {
        return;
    }

    items.move(fromIndex, toIndex);

    m_outputResourceItemsLoading = true;
    m_outputResourceItems.clear();

    for (int i = 0; i < items.size(); ++i) {
        AudioFxParams params = items[i]->params();
        params.chainOrder = static_cast<AudioFxChainOrder>(i);

        // NOTE: blocked because two items can transiently share the same
        // chainOrder/id() mid-loop (e.g. moving index 2 to 0 briefly gives
        // both the just-moved item and the not-yet-processed original
        // occupant of slot 0 the same chainOrder) -- fxParamsChanged() is
        // NOTIFY for OutputResourceItem::id(), so letting it fire here would
        // let a QML binding on it (id is used as this delegate's navigation
        // name) observe that duplicate. outputResourceItemListChanged() at
        // the end of this function -- which rebuilds every delegate against
        // the now-fully-consistent state -- covers the necessary UI update.
        items[i]->blockSignals(true);
        items[i]->setParams(params);
        items[i]->blockSignals(false);

        m_outputResourceItems.insert(params.chainOrder, items[i]);
    }

    m_outputResourceItemsLoading = false;

    m_outParams.fxChain.clear();
    for (const OutputResourceItem* item : std::as_const(m_outputResourceItems)) {
        m_outParams.fxChain.insert({ item->params().chainOrder, item->params() });
    }

    m_fxChainUpdatePending = true;

    emit fxChainParamsChanged(m_outParams);
    emit outputResourceItemListChanged();
}

void MixerChannelItem::addBlankSlots(size_t count)
{
    TRACEFUNC;

    if (count == 0) {
        return;
    }

    for (size_t i = 0; i < count; ++i) {
        AudioFxParams params;
        params.chainOrder = resolveNewBlankOutputResourceItemOrder();
        m_outputResourceItems.insert(params.chainOrder, buildOutputResourceItem(params));
    }

    emit outputResourceItemListChanged();
}

void MixerChannelItem::removeBlankSlotsFromEnd(size_t count)
{
    TRACEFUNC;

    bool itemsRemoved = false;
    DEFER {
        if (itemsRemoved) {
            emit outputResourceItemListChanged();
        }
    };

    for (size_t i = 0; i < count; ++i) {
        if (m_outputResourceItems.empty()) {
            return;
        }

        auto lastItemIt = std::prev(m_outputResourceItems.end());
        OutputResourceItem* item = lastItemIt.value();

        if (!item->isBlank()) {
            return;
        }

        m_outputResourceItems.erase(lastItemIt);
        closeEditor(item);
        item->disconnect();
        item->deleteLater();
        itemsRemoved = true;
    }
}

void MixerChannelItem::loadInputParams(const AudioInputParams& newParams)
{
    if (m_outputOnly) {
        return;
    }

    if (m_inputParams == newParams) {
        return;
    }

    m_inputParams = newParams;
    m_inputResourceItem->setParams(newParams);
}

void MixerChannelItem::loadOutputParams(const AudioOutputParams& newParams)
{
    //! NOTE Deliberately not touching solo/muted/forceMute here: those are owned by
    //! INotationSoloMuteState (or, for aux tracks, IProjectAudioSettings::auxSoloMuteState)
    //! and by PlaybackController's live force-mute computation, and are always default-false
    //! on an AudioOutputParams fetched from IProjectAudioSettings. Applying them here would
    //! clobber the solo/mute state already loaded via loadSoloMuteState().
    if (!muse::RealIsEqual(m_outParams.volume, newParams.volume)) {
        m_outParams.volume = newParams.volume;
        if (!m_hasVolumeAutomation) {
            setDisplayedVolumeLevel(m_outParams.volume);
        }
    }

    if (!muse::RealIsEqual(m_outParams.balance, newParams.balance)) {
        m_outParams.balance = newParams.balance;
        if (!m_hasBalanceAutomation) {
            setDisplayedBalance(m_outParams.balance.raw() * BALANCE_SCALING_FACTOR);
        }
    }

    if (m_outParams.color != newParams.color) {
        m_outParams.color = newParams.color;
        emit colorChanged();
    }

    if (m_outParams.color != newParams.color) {
        m_outParams.color = newParams.color;
        emit colorChanged();
    }

    loadOutputResourceItems(newParams.fxChain);
    loadAuxSendItems(newParams.auxSends);

    m_outputParamsLoaded = true;
}

void MixerChannelItem::loadOutputResourceItems(const AudioFxChain& fxChain)
{
    // NOTE: any incoming fx chain update -- including the engine round-trip
    // echoing back a reorder this object itself just sent -- means the
    // engine has processed at least one round-trip since the last one we
    // sent, so it's safe to allow moveOutputResourceItem() again. See its
    // own comment on m_fxChainUpdatePending.
    m_fxChainUpdatePending = false;

    m_outParams.fxChain = fxChain;

    m_outputResourceItemsLoading = true;
    DEFER {
        m_outputResourceItemsLoading = false;
    };

    QMap<AudioFxChainOrder, OutputResourceItem*> newItems = m_outputResourceItems;

    for (AudioFxChainOrder chainOrder : m_outputResourceItems.keys()) {
        OutputResourceItem* item = m_outputResourceItems.value(chainOrder);

        if (item->isBlank()) {
            continue;
        }

        if (fxChain.find(chainOrder) == fxChain.cend()) {
            item->disconnect();
            item->deleteLater();
            newItems.remove(chainOrder);
        }
    }

    for (const auto& pair : fxChain) {
        OutputResourceItem* item = newItems.value(pair.first, nullptr);

        if (item) {
            item->setParams(pair.second);
        } else {
            newItems.insert(pair.first, buildOutputResourceItem(pair.second));
        }
    }

    if (m_outputResourceItems != newItems) {
        m_outputResourceItems = std::move(newItems);
        emit outputResourceItemListChanged();
    }
}

void MixerChannelItem::loadAuxSendItems(const AuxSendsParams& auxSends)
{
    if (m_outParams.auxSends == auxSends) {
        return;
    }

    m_outParams.auxSends = auxSends;

    //! NOTE: keep existing slots whose target bus is still assigned - this preserves their
    //! on-screen (slot-order) position; anything else is rebuilt from scratch below
    QMap<int, AuxSendItem*> newItems;
    std::vector<aux_channel_idx_t> handledBuses;

    for (auto it = m_auxSendItems.begin(); it != m_auxSendItems.end(); ++it) {
        AuxSendItem* item = it.value();
        aux_channel_idx_t busIndex = item->auxIndex();

        bool stillAssigned = busIndex != AuxSendItem::NO_BUS
                             && busIndex < auxSends.size()
                             && !isBlankAuxSend(auxSends[busIndex]);

        if (stillAssigned) {
            item->blockSignals(true);
            item->setIsActive(auxSends[busIndex].active);
            item->setAudioSignalPercentage(static_cast<int>(auxSends[busIndex].signalAmount * 100.f));
            item->blockSignals(false);
            newItems.insert(it.key(), item);
            handledBuses.push_back(busIndex);
        } else if (item->isBlank()) {
            newItems.insert(it.key(), item);
        } else {
            item->disconnect();
            item->deleteLater();
        }
    }

    for (aux_channel_idx_t i = 0; i < auxSends.size(); ++i) {
        if (isBlankAuxSend(auxSends[i]) || muse::contains(handledBuses, i)) {
            continue;
        }

        //! NOTE: guard against ever exceeding the per-track slot cap here too (mirroring
        //! addAuxSendBlankSlot()'s guard) - without it, more than AUX_SEND_SLOT_LIMIT
        //! non-blank entries (which should never happen, but could from a desync) would
        //! make resolveNewBlankAuxSendItemOrder() return an already-used key, silently
        //! overwriting (and leaking) whatever item currently holds that slot
        if (newItems.size() >= AUX_SEND_SLOT_LIMIT) {
            break;
        }

        int slotOrder = resolveNewBlankAuxSendItemOrder(newItems);
        newItems.insert(slotOrder, buildAuxSendItem(i, auxSends[i]));
    }

    bool changed = m_auxSendItems != newItems;

    if (changed) {
        m_auxSendItems = std::move(newItems);
    }

    ensureTrailingBlankAuxSlot(); // may itself sync the model if it adds a slot

    if (changed) {
        m_auxSendItemListModel->sync(m_auxSendItems.values());
    }
}

void MixerChannelItem::loadSoloMuteState(const notation::INotationSoloMuteState::SoloMuteState& newState)
{
    if (m_outParams.muted != newState.mute) {
        m_outParams.muted = newState.mute;
        emit mutedChanged();
    }

    if (m_outParams.solo != newState.solo) {
        m_outParams.solo = newState.solo;
        emit soloChanged();
    }
}

void MixerChannelItem::loadMuteForceMuteState(bool muted, bool forceMute)
{
    if (m_outParams.muted != muted) {
        m_outParams.muted = muted;
        emit mutedChanged();
    }

    if (m_outParams.forceMute != forceMute) {
        m_outParams.forceMute = forceMute;
        emit forceMuteChanged();
    }
}

void MixerChannelItem::subscribeOnAudioSignalChanges(AudioSignalChanges& audioSignalChanges)
{
    m_audioSignalChanges = audioSignalChanges;

    m_audioSignalChanges.onReceive(this, [this](const AudioSignalValuesMap& signalValues) {
        //!Note There should be no signal changes when the mixer channel is muted.
        //!     But some audio signal changes still might be "on the way" from the times when the mixer channel wasn't muted
        //!     So that we have to just ignore them
        if (muted()) {
            return;
        }

        for (const auto& pair : signalValues) {
            audioch_t audioChNum = pair.first;
            volume_dbfs_t newPressure = pair.second.pressure;

            if (newPressure < MIN_DISPLAYED_DBFS) {
                setAudioChannelVolumePressure(audioChNum, MIN_DISPLAYED_DBFS);
            } else if (newPressure > MAX_DISPLAYED_DBFS) {
                setAudioChannelVolumePressure(audioChNum, MAX_DISPLAYED_DBFS);
            } else {
                setAudioChannelVolumePressure(audioChNum, newPressure);
            }
        }
    });
}

void MixerChannelItem::subscribeOnAutomatedControlParamsChanges(AutomatedControlParamsChanges& changes)
{
    m_automatedControlParamsChanges = changes;

    m_automatedControlParamsChanges.onReceive(this, [this](const AutomatedControlParams& params) {
        if (m_hasVolumeAutomation) {
            setDisplayedVolumeLevel(params.volume.raw());
        }

        if (m_hasBalanceAutomation) {
            setDisplayedBalance(params.balance.raw() * BALANCE_SCALING_FACTOR);
        }
    });
}

void MixerChannelItem::setTitle(QString title)
{
    if (m_title == title) {
        return;
    }

    m_title = title;
    emit titleChanged(m_title);
}

void MixerChannelItem::setLeftChannelPressure(float leftChannelPressure)
{
    if (qFuzzyCompare(m_leftChannelPressure, leftChannelPressure)) {
        return;
    }

    m_leftChannelPressure = leftChannelPressure;
    emit leftChannelPressureChanged(m_leftChannelPressure);
}

void MixerChannelItem::setRightChannelPressure(float rightChannelPressure)
{
    if (qFuzzyCompare(m_rightChannelPressure, rightChannelPressure)) {
        return;
    }

    m_rightChannelPressure = rightChannelPressure;
    emit rightChannelPressureChanged(m_rightChannelPressure);
}

void MixerChannelItem::setVolumeLevel(float volumeLevel)
{
    if (qFuzzyCompare(m_outParams.volume, volumeLevel)) {
        return;
    }

    m_outParams.volume = volumeLevel;
    setDisplayedVolumeLevel(volumeLevel);
    emit controlParamsChanged(m_outParams);
}

void MixerChannelItem::setBalance(int balance)
{
    if (m_outParams.balance.raw() * BALANCE_SCALING_FACTOR == balance) {
        return;
    }

    m_outParams.balance = balance / BALANCE_SCALING_FACTOR;
    setDisplayedBalance(balance);
    emit controlParamsChanged(m_outParams);
}

void MixerChannelItem::setSolo(bool solo)
{
    if (m_outParams.solo == solo) {
        return;
    }

    m_outParams.solo = solo;

    notation::INotationSoloMuteState::SoloMuteState soloMuteState;
    soloMuteState.mute = m_outParams.muted;
    soloMuteState.solo = m_outParams.solo;

    emit soloMuteStateChanged(soloMuteState);
    emit soloChanged();

    if (solo && m_outParams.muted) {
        setMuted(false);
    }
}

void MixerChannelItem::setMuted(bool mute)
{
    if (m_outParams.muted == mute) {
        return;
    }

    m_outParams.muted = mute;

    notation::INotationSoloMuteState::SoloMuteState soloMuteState;
    soloMuteState.mute = m_outParams.muted;
    soloMuteState.solo = m_outParams.solo;

    emit soloMuteStateChanged(soloMuteState);
    emit mutedChanged();

    if (mute && m_outParams.solo) {
        setSolo(false);
    }
}

void MixerChannelItem::setColor(QColor color)
{
    if (m_outParams.color == color) {
        return;
    }

    m_outParams.color = color;
    emit colorChanged();
}

void MixerChannelItem::setSelected(bool selected)
{
    if (m_selected == selected) {
        return;
    }

    m_selected = selected;
    emit selectedChanged();
}

mu::notation::INotationPlaybackPtr MixerChannelItem::notationPlayback() const
{
    project::INotationProjectPtr project = context()->currentProject();
    return project ? project->masterNotation()->playback() : nullptr;
}

void MixerChannelItem::updateHasAutomationFlags()
{
    bool hasVolumeAutomation = false;
    bool hasBalanceAutomation = false;

    INotationProjectPtr project = context()->currentProject();
    const notation::AutomationDataConstPtr automationData
        = (project && m_instrumentTrackId.isValid()) ? project->masterNotation()->automation()->automationData() : nullptr;

    if (automationData) {
        hasVolumeAutomation = !automationData->curve(
            notation::AutomationCurveKey::instrument(notation::AutomationType::Volume, m_instrumentTrackId)).empty();
        hasBalanceAutomation = !automationData->curve(
            notation::AutomationCurveKey::instrument(notation::AutomationType::Pan, m_instrumentTrackId)).empty();
    }

    if (m_hasVolumeAutomation != hasVolumeAutomation) {
        m_hasVolumeAutomation = hasVolumeAutomation;
        emit hasVolumeAutomationChanged();
        if (!m_hasVolumeAutomation) {
            setDisplayedVolumeLevel(m_outParams.volume);
        }
    }

    if (m_hasBalanceAutomation != hasBalanceAutomation) {
        m_hasBalanceAutomation = hasBalanceAutomation;
        emit hasBalanceAutomationChanged();
        if (!m_hasBalanceAutomation) {
            setDisplayedBalance(m_outParams.balance.raw() * BALANCE_SCALING_FACTOR);
        }
    }
}

void MixerChannelItem::setDisplayedVolumeLevel(float volumeLevel)
{
    if (muse::RealIsEqual(m_volumeLevel, volumeLevel)) {
        return;
    }

    m_volumeLevel = volumeLevel;
    emit volumeLevelChanged(m_volumeLevel);
}

void MixerChannelItem::setDisplayedBalance(int balance)
{
    if (m_balance == balance) {
        return;
    }

    m_balance = balance;
    emit balanceChanged(m_balance);
}

void MixerChannelItem::setAudioChannelVolumePressure(const audio::audioch_t chNum, const float newValue)
{
    if (chNum == 0) {
        setLeftChannelPressure(newValue);
    } else {
        setRightChannelPressure(newValue);
    }
}

void MixerChannelItem::resetAudioChannelsVolumePressure()
{
    setLeftChannelPressure(MIN_DISPLAYED_DBFS);
    setRightChannelPressure(MIN_DISPLAYED_DBFS);
}

void MixerChannelItem::setupVideoMeterAnimation()
{
    constexpr int METER_UPDATE_INTERVAL_MS = 100;

    m_videoMeterTimer = new QTimer(this);
    m_videoMeterTimer->setInterval(METER_UPDATE_INTERVAL_MS);
    connect(m_videoMeterTimer, &QTimer::timeout, this, &MixerChannelItem::updateFakeVideoMeter);

    auto updateTimerState = [this]() {
        if (playbackController()->isVideoPlaying() && !muted()) {
            m_videoMeterTimer->start();
        } else {
            m_videoMeterTimer->stop();
            resetAudioChannelsVolumePressure();
        }
    };

    playbackController()->isVideoPlayingChanged().onNotify(this, updateTimerState);
    connect(this, &MixerChannelItem::mutedChanged, this, updateTimerState);

    updateTimerState();
}

void MixerChannelItem::updateFakeVideoMeter()
{
    float base = std::clamp(volumeLevel() - 12.f, MIN_DISPLAYED_DBFS.raw(), MAX_DISPLAYED_DBFS.raw());

    auto jitteredPressure = [base]() {
        float jitter = static_cast<float>(QRandomGenerator::global()->generateDouble()) * 6.f - 3.f;
        return std::clamp(base + jitter, MIN_DISPLAYED_DBFS.raw(), MAX_DISPLAYED_DBFS.raw());
    };

    setLeftChannelPressure(jitteredPressure());
    setRightChannelPressure(jitteredPressure());
}

InputResourceItem* MixerChannelItem::buildInputResourceItem()
{
    InputResourceItem* newItem = new InputResourceItem(this);

    connect(newItem, &InputResourceItem::inputParamsChangeRequested, this, [this, newItem](const AudioResourceMeta& newMeta) {
        if (askAboutChangingSound()) {
            newItem->setParamsRecourceMeta(newMeta);
        }
    });

    connect(newItem, &InputResourceItem::inputParamsChanged, this, [this, newItem]() {
        bool audioSourceChanged = m_inputParams.type() != newItem->params().type();

        m_inputParams = newItem->params();
        emit inputParamsChanged(m_inputParams);

        if (!audioSourceChanged) {
            return;
        }

        bool auxParamsChanged = false;
        for (aux_channel_idx_t idx = 0; idx < static_cast<size_t>(m_outParams.auxSends.size()); ++idx) {
            //! NOTE: skip unassigned/blank positions - they have nothing displayed, and
            //! writing a fresh default signalAmount into one would move it away from the
            //! blank sentinel, wrongly turning it into an assigned send
            if (isBlankAuxSend(m_outParams.auxSends.at(idx))) {
                continue;
            }

            const muse::String& soundId = m_inputParams.resourceMeta.attributeVal(PLAYBACK_SETUP_DATA_ATTRIBUTE);
            gain_t newAudioSignalAmount = configuration()->defaultAuxSendValue(idx, m_inputParams.type(), soundId);

            //! NOTE: m_auxSendItems is keyed by stable slot order, not bus index - the
            //! item targeting this bus (if any is currently live) must be found by value
            AuxSendItem* item = nullptr;
            for (AuxSendItem* candidate : std::as_const(m_auxSendItems)) {
                if (candidate->auxIndex() == idx) {
                    item = candidate;
                    break;
                }
            }

            if (!item) {
                if (!muse::RealIsEqual(m_outParams.auxSends.at(idx).signalAmount, newAudioSignalAmount)) {
                    m_outParams.auxSends.at(idx).signalAmount = newAudioSignalAmount;
                    auxParamsChanged = true;
                }
            } else {
                item->setAudioSignalPercentage(newAudioSignalAmount * 100.f);
            }
        }

        if (auxParamsChanged) {
            emit auxSendsParamsChanged(m_outParams);
        }
    });

    connect(newItem, &InputResourceItem::isBlankChanged, this, &MixerChannelItem::inputResourceItemChanged);

    connect(newItem, &InputResourceItem::nativeEditorViewLaunchRequested, this, [this, newItem]() {
        if (newItem->params().type() != AudioSourceType::Vsti) {
            return;
        }

        actions::ActionQuery aq(VSTI_EDITOR_ACTION);
        aq.addParam(TRACK_ID_KEY, Val(m_trackId));
        aq.addParam(RESOURCE_ID_KEY, Val(newItem->params().resourceMeta.id));

        openEditor(newItem, aq);
    });

    connect(newItem, &InputResourceItem::nativeEditorViewCloseRequested, this, [this, newItem]() {
        closeEditor(newItem);
    });

    return newItem;
}

OutputResourceItem* MixerChannelItem::buildOutputResourceItem(const audio::AudioFxParams& fxParams)
{
    OutputResourceItem* newItem = new OutputResourceItem(this, fxParams);

    connect(newItem, &OutputResourceItem::fxParamsChanged, this, [this]() {
        if (m_outputResourceItemsLoading) {
            return;
        }

        m_outParams.fxChain.clear();

        for (const OutputResourceItem* item : std::as_const(m_outputResourceItems)) {
            m_outParams.fxChain.insert({ item->params().chainOrder, item->params() });
        }

        emit fxChainParamsChanged(m_outParams);
    });

    connect(newItem, &OutputResourceItem::nativeEditorViewLaunchRequested, this, [this, newItem]() {
        if (newItem->params().type() != AudioFxType::VstFx) {
            return;
        }

        actions::ActionQuery aq(VSTFX_EDITOR_ACTION);

        aq.addParam(TRACK_ID_KEY, Val(m_trackId));
        aq.addParam(RESOURCE_ID_KEY, Val(newItem->params().resourceMeta.id));
        aq.addParam(CHAIN_ORDER_KEY, Val(newItem->params().chainOrder));

        openEditor(newItem, aq);
    });

    connect(newItem, &OutputResourceItem::nativeEditorViewCloseRequested, this, [this, newItem]() {
        closeEditor(newItem);
    });

    return newItem;
}

AuxSendItem* MixerChannelItem::buildAuxSendItem(aux_channel_idx_t index, const AuxSendParams& params)
{
    bool isBlankSlot = isBlankAuxSend(params);

    AuxSendItem* newItem = new AuxSendItem(this);
    newItem->blockSignals(true);
    newItem->setAuxIndex(isBlankSlot ? AuxSendItem::NO_BUS : index);
    newItem->setIsActive(params.active);
    newItem->setAudioSignalPercentage(static_cast<int>(params.signalAmount * 100.f));
    newItem->setTitle(isBlankSlot ? QString() : auxBusDisplayName(index));
    newItem->blockSignals(false);

    connect(newItem, &AuxSendItem::isActiveChanged, this, [this, newItem]() {
        updateAuxSendField(newItem, [newItem](AuxSendParams& params) {
            params.active = newItem->isActive();
        });
    });

    connect(newItem, &AuxSendItem::audioSignalPercentageChanged, this, [this, newItem](int percentage) {
        updateAuxSendField(newItem, [percentage](AuxSendParams& params) {
            params.signalAmount = static_cast<float>(percentage) / 100.f;
        });
    });

    newItem->setMenuDataProvider([this, newItem]() {
        return buildAuxSendMenuData(newItem);
    });

    newItem->setMenuItemHandler([this, newItem](const QString& menuItemId) {
        handleAuxSendMenuItem(newItem, menuItemId);
    });

    return newItem;
}

void MixerChannelItem::updateAuxSendField(const AuxSendItem* item, const std::function<void(AuxSendParams&)>& setter)
{
    aux_channel_idx_t idx = item->auxIndex();
    if (idx == AuxSendItem::NO_BUS) {
        return;
    }

    if (idx >= m_outParams.auxSends.size()) {
        resizeAuxSendsWithBlankPadding(m_outParams.auxSends, idx + 1);
    }

    setter(m_outParams.auxSends[idx]);
    emit auxSendsParamsChanged(m_outParams);
}

AuxSendItem::MenuData MixerChannelItem::buildAuxSendMenuData(const AuxSendItem* item) const
{
    AuxSendItem::MenuData data;

    for (const auto& pair : playbackController()->auxTrackIdMap()) {
        aux_channel_idx_t busIndex = pair.first;

        //! NOTE: whether another slot is really using a bus must be based on its auxIndex(),
        //! not isBlank() - a slot bypassed with its knob at 0% is isBlank() but still holds
        //! a real target bus, and would otherwise let this bus be offered to two slots at once
        bool usedByAnotherSlot = false;
        for (const AuxSendItem* other : std::as_const(m_auxSendItems)) {
            if (other == item || other->auxIndex() == AuxSendItem::NO_BUS) {
                continue;
            }

            if (other->auxIndex() == busIndex) {
                usedByAnotherSlot = true;
                break;
            }
        }

        if (usedByAnotherSlot) {
            continue; // already targeted by another slot on this track
        }

        bool isGroupBus = playbackController()->isAuxBusGroup(busIndex);
        QString busName = auxBusDisplayName(busIndex);
        data.availableBuses.push_back({ busIndex, busName, isGroupBus });
    }

    //! NOTE: don't offer "Add Aux send" when a blank slot other than this one already
    //! exists - it would be a no-op (addAuxSendBlankSlot() itself refuses to create a
    //! second blank), and the user should just use that existing blank slot instead
    bool anotherBlankExists = false;
    for (const AuxSendItem* other : std::as_const(m_auxSendItems)) {
        if (other != item && other->auxIndex() == AuxSendItem::NO_BUS) {
            anotherBlankExists = true;
            break;
        }
    }

    data.canAddSend = m_auxSendItems.size() < AUX_SEND_SLOT_LIMIT && !anotherBlankExists;
    //! NOTE: "Add Aux bus"/"Add Bus" share the same underlying MAX_AUX_CHANNEL_NUM pool -
    //! both bus kinds are tracked in the same auxTrackIdMap()
    bool canAddAnotherBus = playbackController()->canAddAuxBus();
    data.canAddBus = canAddAnotherBus;
    data.canAddGroupBus = canAddAnotherBus;

    return data;
}

void MixerChannelItem::handleAuxSendMenuItem(AuxSendItem* item, const QString& menuItemId)
{
    if (menuItemId == "noAuxSend") {
        blankAuxSend(item);
        return;
    }

    if (menuItemId == "addAuxSend") {
        addAuxSendBlankSlot();
        return;
    }

    if (menuItemId == "addAuxBus") {
        playbackController()->addNewAuxBus();
        return;
    }

    if (menuItemId == "addGroupBus") {
        playbackController()->addNewGroupBus();
        return;
    }

    bool ok = false;
    aux_channel_idx_t newBusIndex = static_cast<aux_channel_idx_t>(menuItemId.toUInt(&ok));
    if (ok) {
        reassignAuxSend(item, newBusIndex);
    }
}

void MixerChannelItem::reassignAuxSend(AuxSendItem* item, aux_channel_idx_t newBusIndex)
{
    IF_ASSERT_FAILED(item) {
        return;
    }

    aux_channel_idx_t oldIndex = item->auxIndex();
    if (oldIndex == newBusIndex && !item->isBlank()) {
        return;
    }

    if (newBusIndex >= m_outParams.auxSends.size()) {
        resizeAuxSendsWithBlankPadding(m_outParams.auxSends, newBusIndex + 1);
    }

    AudioSourceType sourceType = m_inputParams.isValid() ? m_inputParams.type() : AudioSourceType::Fluid;
    const muse::String& instrumentSoundId = m_inputParams.resourceMeta.attributeVal(PLAYBACK_SETUP_DATA_ATTRIBUTE);
    gain_t signalAmount = configuration()->defaultAuxSendValue(newBusIndex, sourceType, instrumentSoundId);

    if (oldIndex != AuxSendItem::NO_BUS && oldIndex < m_outParams.auxSends.size() && oldIndex != newBusIndex) {
        m_outParams.auxSends[oldIndex] = blankAuxSendParams();
    }

    m_outParams.auxSends[newBusIndex] = AuxSendParams { signalAmount, true };

    //! NOTE: the item's slot order (its key in m_auxSendItems) does not change here -
    //! only which bus it targets, so it stays visually in place. Signals must NOT be
    //! blocked here (unlike in buildAuxSendItem's initial construction) - this item is
    //! already live/bound in QML, and blocking would silently freeze its displayed title
    item->setAuxIndex(newBusIndex);
    item->setTitle(auxBusDisplayName(newBusIndex));
    item->setIsActive(true);
    item->setAudioSignalPercentage(static_cast<int>(signalAmount * 100.f));

    ensureTrailingBlankAuxSlot();

    emit auxSendsParamsChanged(m_outParams);
    m_auxSendItemListModel->sync(m_auxSendItems.values());
}

void MixerChannelItem::blankAuxSend(AuxSendItem* item)
{
    IF_ASSERT_FAILED(item) {
        return;
    }

    aux_channel_idx_t index = item->auxIndex();
    if (index != AuxSendItem::NO_BUS && index < m_outParams.auxSends.size()) {
        m_outParams.auxSends[index] = blankAuxSendParams();
    }

    int slotOrder = m_auxSendItems.key(item, -1);
    if (slotOrder >= 0) {
        m_auxSendItems.remove(slotOrder);
        //! NOTE: removing a MIDDLE slot (e.g. this item's bus was deleted elsewhere - see
        //! clearAuxSendsTargeting()) leaves a gap in the key sequence. Without closing it,
        //! ensureTrailingBlankAuxSlot()/resolveNewBlankAuxSendItemOrder() below would refill
        //! that same gap (it always picks the LOWEST free key) instead of appending a truly
        //! trailing blank - leaving a blank slot sandwiched before other real, higher-keyed
        //! slots, which violates the "at most one blank, always trailing" invariant
        compactAuxSendItemKeys();
    }

    item->disconnect();
    item->deleteLater();

    ensureTrailingBlankAuxSlot();

    emit auxSendsParamsChanged(m_outParams);
    m_auxSendItemListModel->sync(m_auxSendItems.values());
}

void MixerChannelItem::compactAuxSendItemKeys()
{
    //! NOTE: slot-order keys have no meaning outside this map (unlike a bus INDEX, no
    //! persisted data or external code references a specific key value) - only their
    //! relative (ascending) order matters, so renumbering them to a contiguous 0..N-1
    //! range is always safe and preserves the existing visual order
    QMap<int, AuxSendItem*> compacted;
    int nextKey = 0;
    for (AuxSendItem* item : std::as_const(m_auxSendItems)) {
        compacted.insert(nextKey++, item);
    }
    m_auxSendItems = std::move(compacted);
}

bool MixerChannelItem::hasBlankAuxSendSlot() const
{
    //! NOTE: based on auxIndex(), not isBlank() - see buildAuxSendMenuData for why
    for (const AuxSendItem* item : std::as_const(m_auxSendItems)) {
        if (item->auxIndex() == AuxSendItem::NO_BUS) {
            return true;
        }
    }

    return false;
}

void MixerChannelItem::addAuxSendBlankSlot()
{
    //! NOTE: guard against creating a second blank slot - e.g. "Add Aux send" clicked
    //! from a slot's dropdown while a trailing blank slot is already present
    if (m_auxSendItems.size() >= AUX_SEND_SLOT_LIMIT || hasBlankAuxSendSlot()) {
        return;
    }

    int slotOrder = resolveNewBlankAuxSendItemOrder(m_auxSendItems);
    m_auxSendItems.insert(slotOrder, buildAuxSendItem(AuxSendItem::NO_BUS, blankAuxSendParams()));

    m_auxSendItemListModel->sync(m_auxSendItems.values());
}

void MixerChannelItem::addAuxSendBlankSlots(size_t count)
{
    TRACEFUNC;

    if (count == 0) {
        return;
    }

    //! NOTE: unlike addAuxSendBlankSlot() (used by the "Add Aux send" menu action, which
    //! only ever wants a single trailing blank), this deliberately allows multiple blank
    //! slots at once - used to pad a channel up to match a wider sibling channel's slot
    //! count (mirroring addBlankSlots() for FX slots)
    for (size_t i = 0; i < count && static_cast<int>(m_auxSendItems.size()) < AUX_SEND_SLOT_LIMIT; ++i) {
        int slotOrder = resolveNewBlankAuxSendItemOrder(m_auxSendItems);
        m_auxSendItems.insert(slotOrder, buildAuxSendItem(AuxSendItem::NO_BUS, blankAuxSendParams()));
    }

    m_auxSendItemListModel->sync(m_auxSendItems.values());
}

void MixerChannelItem::removeAuxSendBlankSlotsFromEnd(size_t count)
{
    TRACEFUNC;

    bool itemsRemoved = false;
    DEFER {
        if (itemsRemoved) {
            m_auxSendItemListModel->sync(m_auxSendItems.values());
        }
    };

    for (size_t i = 0; i < count; ++i) {
        if (m_auxSendItems.empty()) {
            return;
        }

        auto lastItemIt = std::prev(m_auxSendItems.end());
        AuxSendItem* item = lastItemIt.value();

        if (item->auxIndex() != AuxSendItem::NO_BUS) {
            return;
        }

        m_auxSendItems.erase(lastItemIt);
        item->disconnect();
        item->deleteLater();
        itemsRemoved = true;
    }
}

void MixerChannelItem::setAuxSendItemCount(size_t count)
{
    count = std::min(count, static_cast<size_t>(AUX_SEND_SLOT_LIMIT));
    size_t itemsSize = static_cast<size_t>(m_auxSendItems.size());

    if (itemsSize == count) {
        return;
    }

    if (itemsSize < count) {
        addAuxSendBlankSlots(count - itemsSize);
    } else {
        removeAuxSendBlankSlotsFromEnd(itemsSize - count);
    }
}

void MixerChannelItem::ensureTrailingBlankAuxSlot()
{
    if (!hasBlankAuxSendSlot()) {
        addAuxSendBlankSlot();
    }
}

int MixerChannelItem::resolveNewBlankAuxSendItemOrder(const QMap<int, AuxSendItem*>& items) const
{
    for (int order = 0; order < AUX_SEND_SLOT_LIMIT; ++order) {
        if (!items.contains(order)) {
            return order;
        }
    }

    return AUX_SEND_SLOT_LIMIT - 1;
}

void MixerChannelItem::openEditor(AbstractAudioResourceItem* item, const actions::ActionQuery& action)
{
    if (item->editorAction() != action) {
        if (item->editorAction().isValid()) {
            // make and send close
            actions::ActionQuery closeAction = item->editorAction();
            closeAction.addParam("operation", Val("close"));
            closeAction.addParam("sync", Val(true));
            dispatcher()->dispatch(closeAction);
        }
        // set new action
        item->setEditorAction(action);
    }

    dispatcher()->dispatch(action);
}

void MixerChannelItem::closeEditor(AbstractAudioResourceItem* item)
{
    // make and send close
    actions::ActionQuery closeAction = item->editorAction();
    closeAction.addParam("operation", Val("close"));
    closeAction.addParam("sync", Val(true));
    dispatcher()->dispatch(closeAction);

    item->setEditorAction(UriQuery());
}

bool MixerChannelItem::askAboutChangingSound()
{
    if (!configuration()->needToShowResetSoundFlagsWhenChangeSoundWarning()) {
        return true;
    }

    if (!notationPlayback()->hasSoundFlags({ m_instrumentTrackId })) {
        return true;
    }

    int changeBtn = int(IInteractive::Button::Apply);
    IInteractive::Options options = IInteractive::Option::WithIcon | IInteractive::Option::WithDontShowAgainCheckBox;
    IInteractive::ButtonDatas buttons = {
        interactive()->buttonData(IInteractive::Button::Cancel),
        IInteractive::ButtonData(changeBtn, muse::trc("playback", "Change sound"), true /*accent*/)
    };

    IInteractive::Result result = interactive()->warningSync(muse::trc("playback", "Are you sure you want to change this sound?"),
                                                             muse::trc("playback",
                                                                       "Sound flags on this instrument may be reset, but staff text will remain. This action can’t be undone."),
                                                             buttons, changeBtn, options);

    if (result.button() == changeBtn) {
        if (!result.showAgain()) {
            configuration()->setNeedToShowResetSoundFlagsWhenChangeSoundWarning(false);
        }

        return true;
    } else {
        return false;
    }
}

AudioFxChainOrder MixerChannelItem::resolveNewBlankOutputResourceItemOrder() const
{
    if (m_outputResourceItems.empty()) {
        return 0;
    }

    AudioFxChainOrder lastChainOrder = m_outputResourceItems.lastKey();
    if (lastChainOrder < OUTPUT_RESOURCE_COUNT_LIMIT - 1) {
        return lastChainOrder + 1;
    }

    for (AudioFxChainOrder order = 0; order < OUTPUT_RESOURCE_COUNT_LIMIT; ++order) {
        if (!m_outputResourceItems.contains(order)) {
            return order;
        }
    }

    return OUTPUT_RESOURCE_COUNT_LIMIT - 1;
}

bool MixerChannelItem::outputOnly() const
{
    return m_outputOnly;
}

bool MixerChannelItem::outputParamsLoaded() const
{
    return m_outputParamsLoaded;
}

const AudioInputParams& MixerChannelItem::inputParams() const
{
    return m_inputParams;
}

const AudioOutputParams& MixerChannelItem::outputParams() const
{
    return m_outParams;
}

InputResourceItem* MixerChannelItem::inputResourceItem() const
{
    return m_inputResourceItem;
}

QList<OutputResourceItem*> MixerChannelItem::outputResourceItemList() const
{
    return m_outputResourceItems.values();
}

QObject* MixerChannelItem::auxSendItemModel() const
{
    return m_auxSendItemListModel;
}

const QMap<int, AuxSendItem*>& MixerChannelItem::auxSendItems() const
{
    return m_auxSendItems;
}

QString MixerChannelItem::auxBusDisplayName(aux_channel_idx_t index) const
{
    project::INotationProjectPtr project = context()->currentProject();
    if (project) {
        muse::String customName = project->audioSettings()->auxName(index);
        if (!customName.empty()) {
            return customName.toQString();
        }
    }

    return auxBusPositionalName(playbackController()->resolveAuxBusDisplayNumber(index), playbackController()->isAuxBusGroup(index));
}

void MixerChannelItem::renameAuxSendsTargeting(aux_channel_idx_t busIndex, const QString& newName)
{
    for (AuxSendItem* item : std::as_const(m_auxSendItems)) {
        if (item->auxIndex() == busIndex) {
            item->setTitle(newName);
        }
    }
}

void MixerChannelItem::clearAuxSendsTargeting(aux_channel_idx_t busIndex)
{
    //! NOTE: blankAuxSend() removes the item from m_auxSendItems, so the matching items
    //! must be collected up front rather than acted on while iterating that same map
    QList<AuxSendItem*> targetingItems;
    for (AuxSendItem* item : std::as_const(m_auxSendItems)) {
        if (item->auxIndex() == busIndex) {
            targetingItems.push_back(item);
        }
    }

    for (AuxSendItem* item : targetingItems) {
        blankAuxSend(item);
    }
}
