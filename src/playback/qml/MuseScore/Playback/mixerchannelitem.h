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

#pragma once

#include <functional>
#include <optional>

#include <QElapsedTimer>
#include <QObject>
#include <qqmlintegration.h>

#include "async/asyncable.h"

#include "modularity/ioc.h"
#include "rcommand/icommanddispatcher.h"
#include "project/iprojectsession.h"
#include "interactive/iinteractive.h"
#include "iplaybackconfiguration.h"
#include "iplaybackcontroller.h"

#include "ui/qml/Muse/Ui/navigationpanel.h"

#include "audio/common/audiotypes.h"
#include "audio/main/iplayback.h"
#include "notation/inotationconfiguration.h"
#include "project/iprojectaudiosettings.h"
#include "inputresourceitem.h"
#include "outputresourceitem.h"
#include "auxsenditem.h"
#include "auxsenditemlistmodel.h"

namespace mu::playback {
//! NOTE: a channel's aux sends as a whole - their params and the bus each slot targets, in slot order -
//! captured and restored by MixerPanelModel's undo of a user's aux-send choice
struct ChannelAuxSendsState {
    muse::audio::AuxSendsParams params;
    QList<muse::audio::aux_channel_idx_t> slotBuses;

    bool operator==(const ChannelAuxSendsState& other) const
    {
        return params == other.params && slotBuses == other.slotBuses;
    }
};

class MixerChannelItem : public QObject, public muse::async::Asyncable, public muse::Contextable
{
    Q_OBJECT

    Q_PROPERTY(Type type READ type CONSTANT)
    Q_PROPERTY(bool outputOnly READ outputOnly CONSTANT)
    Q_PROPERTY(bool isGroupBus READ isGroupBus CONSTANT)
    Q_PROPERTY(bool isReverbBus READ isReverbBus CONSTANT)
    //! NOTE: a sound track's channel (the video's, the reference audio's), see SoundTrackChannel
    Q_PROPERTY(bool isSoundTrack READ isSoundTrack CONSTANT)
    //! NOTE: only meaningful for Type::Aux channels - see auxBusIndex()'s own doc comment.
    //! No NOTIFY: read once by the Mixer's aux drag-and-drop reorder gesture, never bound
    //! to live QML UI.
    Q_PROPERTY(int auxBusIndex READ auxBusIndex)

    Q_PROPERTY(QString title READ title NOTIFY titleChanged)

    Q_PROPERTY(mu::playback::InputResourceItem * inputResourceItem READ inputResourceItem NOTIFY inputResourceItemChanged)
    Q_PROPERTY(
        QList<mu::playback::OutputResourceItem*> outputResourceItemList READ outputResourceItemList NOTIFY outputResourceItemListChanged)
    //! NOTE: a real incremental list model, not a plain list property - see
    //! AuxSendItemListModel's doc comment for why
    Q_PROPERTY(QObject * auxSendItemModel READ auxSendItemModel CONSTANT)

    Q_PROPERTY(float leftChannelPressure READ leftChannelPressure NOTIFY leftChannelPressureChanged)
    Q_PROPERTY(float rightChannelPressure READ rightChannelPressure NOTIFY rightChannelPressureChanged)

    Q_PROPERTY(float volumeLevel READ volumeLevel WRITE setVolumeLevel NOTIFY volumeLevelChanged)
    Q_PROPERTY(float volumeLevelMin READ volumeLevelMin CONSTANT)
    Q_PROPERTY(float volumeLevelMax READ volumeLevelMax CONSTANT)
    Q_PROPERTY(int balance READ balance WRITE setBalance NOTIFY balanceChanged)
    Q_PROPERTY(int balanceMin READ balanceMin CONSTANT)
    Q_PROPERTY(int balanceMax READ balanceMax CONSTANT)
    Q_PROPERTY(int gain READ gain WRITE setGain NOTIFY gainChanged)
    Q_PROPERTY(int gainMin READ gainMin CONSTANT)
    Q_PROPERTY(int gainMax READ gainMax CONSTANT)
    //! NOTE The track delay (see AudioSourceParams::delayMs), in ms: a VST3 instrument's only
    Q_PROPERTY(int delayMs READ delayMs NOTIFY delayChanged)
    Q_PROPERTY(bool delayAvailable READ delayAvailable NOTIFY delayChanged)
    Q_PROPERTY(int delayMsMin READ delayMsMin CONSTANT)
    Q_PROPERTY(int delayMsMax READ delayMsMax CONSTANT)
    //! NOTE The channel EQ: { enabled, flat, bands: [{ type, typeTitle, frequency, gain, q, enabled, hasGain, hasQ }] }
    Q_PROPERTY(QVariantMap eq READ eq NOTIFY eqChanged)
    Q_PROPERTY(bool hasVolumeAutomation READ hasVolumeAutomation NOTIFY hasVolumeAutomationChanged)
    Q_PROPERTY(bool hasBalanceAutomation READ hasBalanceAutomation NOTIFY hasBalanceAutomationChanged)
    Q_PROPERTY(bool solo READ solo WRITE setSolo NOTIFY soloChanged)
    Q_PROPERTY(bool muted READ muted WRITE setMuted NOTIFY mutedChanged)
    Q_PROPERTY(bool forceMute READ forceMute NOTIFY forceMuteChanged)

    Q_PROPERTY(QColor color READ color WRITE setColor NOTIFY colorChanged)
    Q_PROPERTY(bool hasCustomColor READ hasCustomColor NOTIFY colorChanged)
    Q_PROPERTY(bool selected READ selected WRITE setSelected NOTIFY selectedChanged)

    Q_PROPERTY(muse::ui::NavigationPanel * panel READ panel NOTIFY panelChanged)

    QML_ELEMENT;
    QML_UNCREATABLE("Must be created in C++ only")

    muse::GlobalInject<IPlaybackConfiguration> configuration;
    muse::GlobalInject<notation::INotationConfiguration> notationConfiguration;
    muse::ContextInject<muse::IInteractive> interactive = { this };
    muse::ContextInject<muse::rcommand::ICommandDispatcher> commandDispatcher = { this };
    muse::ContextInject<project::IProjectSession> context = { this };
    muse::ContextInject<IPlaybackController> playbackController = { this };
    muse::ContextInject<muse::audio::IPlayback> playback = { this };

public:
    enum class Type {
        Unknown,
        PrimaryInstrument,
        SecondaryInstrument,
        Metronome,
        Video,
        ReferenceAudio,
        Aux,
        Master,
    };
    Q_ENUM(Type)

    MixerChannelItem()
        : muse::Contextable(muse::iocCtxForQmlObject(this)) {}
    MixerChannelItem(QObject* parent, Type type, bool outputOnly = false, muse::audio::TrackId trackId = -1);

    ~MixerChannelItem() override;

    //! NOTE: a channel item bound to this track and loaded, in whatever panel shows it: to undo/redo a change made
    //! in a panel that may have been closed since (the change reaches the other panels through the engine)
    static MixerChannelItem* findBoundChannel(muse::audio::TrackId trackId);

    Type type() const;
    bool isGroupBus() const;
    //! NOTE: only meaningful for Type::Aux channels - the Reverb bus is a built-in default
    //! that can be renamed but not deleted (see IPlaybackController::removeAuxBus)
    bool isReverbBus() const;

    //! NOTE: the channels of the sound tracks (see SoundTrackChannel): placed and handled alike
    static bool isSoundTrackType(Type type);
    bool isSoundTrack() const;

    muse::audio::TrackId trackId() const;

    //! NOTE: only meaningful for Type::Aux channels - the bus index this channel strip
    //! itself represents (distinct from AuxSendItem::auxIndex(), which is the target bus
    //! of one per-track send slot), needed to query IPlaybackController::isAuxBusGroup()
    muse::audio::aux_channel_idx_t auxBusIndex() const;
    void setAuxBusIndex(muse::audio::aux_channel_idx_t index);

    const engraving::InstrumentTrackId& instrumentTrackId() const;
    void setInstrumentTrackId(const engraving::InstrumentTrackId& instrumentTrackId);

    QString title() const;

    float leftChannelPressure() const;
    float rightChannelPressure() const;

    float volumeLevel() const;
    float volumeLevelMin() const;
    float volumeLevelMax() const;
    int balance() const;
    int balanceMin() const;
    int balanceMax() const;
    int gain() const;
    int gainMin() const;
    int gainMax() const;
    int delayMs() const;
    bool delayAvailable() const;
    int delayMsMin() const;
    int delayMsMax() const;

    QVariantMap eq() const;
    const muse::audio::EqParams& eqParams() const;
    bool hasVolumeAutomation() const;
    bool hasBalanceAutomation() const;
    bool solo() const;
    bool muted() const;
    bool forceMute() const;
    QColor color() const;
    bool hasCustomColor() const;
    bool selected() const;

    muse::ui::NavigationPanel* panel() const;
    void setPanelOrder(int panelOrder);
    void setPanelSection(muse::ui::INavigationSection* section);

    void updateHasAutomationFlags();

    //! NOTE: articulation maps drive keyswitch/CC articulation changes of third-party VST instruments.
    //! The name is null without a map (empty for an untitled one)
    QString articulationMapName() const;

    //! NOTE: the "Articulation map" menu of this track (Mixer, Track list): its name, then the staff context
    //! menu's articulation map commands (see NotationActionController). To rebuild on each show: the map has no
    //! change signal. handleArticulationMapMenuItem() returns false for an item that isn't one of them
    Q_INVOKABLE QVariantList articulationMapMenuItems() const;
    Q_INVOKABLE bool handleArticulationMapMenuItem(const QString& itemId) const;

    //! NOTE: its VST instrument's window, opened from any panel
    Q_INVOKABLE void closeInstrumentEditor();

    //! NOTE: "Save as track preset…" and "Load track preset…" (see TrackPresets), in the Mixer's and the Track list's
    //! menus. handleTrackPresetMenuItem() returns false for an item that isn't one of them
    Q_INVOKABLE QVariantList trackPresetMenuItems() const;
    Q_INVOKABLE bool handleTrackPresetMenuItem(const QString& itemId) const;

    void setOutputResourceItemCount(size_t count);
    void setAuxSendItemCount(size_t count);
    //! NOTE: every effect and aux send slot there can be (a single channel shown, e.g. in the Track list: its layout
    //! doesn't change with its content, unlike the Mixer's channels lined up on the one with the most)
    void showAllSlots();

    //! NOTE Reorders the channel's own FX slots by moving the slot currently
    //! at fromIndex to toIndex, shifting the slots in between -- e.g. moving
    //! index 2 to index 0 in [A,B,C,D] yields [C,A,B,D], not a two-item swap.
    //! Indices are positions in outputResourceItemList()'s current order (== the
    //! items' current chainOrder, since that's always contiguous 0..count-1 --
    //! see addBlankSlots()/resolveNewBlankOutputResourceItemOrder()). Reassigns
    //! each affected item's chainOrder to its new position via setParams(), so
    //! it flows through the same fxChainParamsChanged persistence/engine path
    //! as any other FX chain edit (add/remove/replace).
    Q_INVOKABLE void moveOutputResourceItem(int fromIndex, int toIndex);

    //! NOTE: an aux send slot dragged elsewhere among this channel's: only how they're shown (the sends are parallel),
    //! the same in every panel showing this channel, saved with the project and undoable. The empty slots come last
    Q_INVOKABLE void moveAuxSendItem(int fromIndex, int toIndex);
    //! NOTE: the buses of the sends, in their slots' order
    std::vector<muse::audio::aux_channel_idx_t> auxSendsOrder() const;

    //! NOTE: for an effect dragged from another channel (see MixerPanelModel::dropFx()): puts fxParams (effect and
    //! settings) in this channel's first empty slot, moved to insertIndex (0..slot count: the slot it's dropped
    //! before). False when there's no empty slot, or a previous change is still on its way to the engine
    bool insertFx(int insertIndex, const muse::audio::AudioFxParams& fxParams);

    //! NOTE: an empty slot to drop an effect from another channel in (see insertFx())
    Q_INVOKABLE bool hasBlankOutputResourceItem() const;

    //! NOTE: an instrument channel's link to its engine track, the same in every panel showing one (Mixer, Track
    //! list): loads its params, sends its own changes to the engine (persisting volume/balance/gain, and solo/mute
    //! to the notation's state), and follows what the engine gets from elsewhere (sound, aux sends): one panel's
    //! change can't be undone by another's stale copy. outputParamsReceived() fires whenever its output params are in
    void bindInstrumentTrack(const engraving::InstrumentTrackId& instrumentTrackId);

    //! NOTE: an Aux/Group bus channel's link to its engine track, the same in every panel showing one (Mixer, Track
    //! list): loads its mute/solo (with the live force-mute) and output params, sends its own changes to the engine
    //! and the project's aux settings, and follows the ones made elsewhere
    void bindAuxBus(muse::audio::aux_channel_idx_t index);

    //! NOTE: Solo clicked by the user: on a Group bus, also applied to the channels routed to it (see
    //! IPlaybackController::setGroupBusSourcesSolo()). setSolo() alone is for restoring a state (e.g. global Solo)
    Q_INVOKABLE void setUserSolo(bool solo);

    void loadInputParams(const project::AudioInputParams& newParams);
    void loadOutputParams(const project::AudioOutputParams& newParams);
    void loadSoloMuteState(const notation::INotationSoloMuteState::SoloMuteState& newState);

    //! NOTE: refreshes the LIVE effective muted/forceMute state (as opposed to
    //! loadSoloMuteState(), which loads the persisted solo/mute the user actually set) -
    //! called when THIS channel becomes (or stops being) force-muted as a side effect of some
    //! OTHER track/bus's solo changing (see IPlaybackController::trackMuteStateChanged())
    void loadMuteForceMuteState(bool muted, bool forceMute);

    void subscribeOnAudioSignalChanges(muse::audio::AudioSignalChanges& audioSignalChanges);
    //! NOTE: the same, for this channel's own track (not the master's, see IPlayback::masterSignalChanges())
    void subscribeOnTrackAudioSignalChanges();

    bool outputOnly() const;

    //! NOTE: true once loadOutputParams() has actually applied real data to this item at
    //! least once. An instrument channel item's real data (incl. fx chain and aux sends)
    //! only arrives asynchronously (see MixerPanelModel::buildInstrumentChannelItem()'s
    //! playback()->params(trackId) promise) - before that, its fx/aux-send slot lists are
    //! genuinely empty, not "correctly synced to zero slots". Cross-channel slot-count
    //! syncing (MixerPanelModel::updateOutputResourceItemCount()/updateAuxSendItemCount())
    //! must skip padding a not-yet-loaded item, or the padding permanently corrupts
    //! aux-send slot order once the real data does arrive (see git history for details)
    bool outputParamsLoaded() const;

    const project::AudioInputParams& inputParams() const;
    const project::AudioOutputParams& outputParams() const;

    InputResourceItem* inputResourceItem() const;
    QList<OutputResourceItem*> outputResourceItemList() const;
    QObject* auxSendItemModel() const;

    const QMap<int, AuxSendItem*>& auxSendItems() const;

    void resetAudioChannelsVolumePressure();

    //! NOTE: updates the title of any of this track's aux-send slots that target busIndex -
    //! called on every other channel item when an aux bus is renamed
    void renameAuxSendsTargeting(muse::audio::aux_channel_idx_t busIndex, const QString& newName);

    //! NOTE: resets to "No aux send" any of this track's aux-send slots that target busIndex -
    //! called on every other channel item when an aux/group bus is deleted, so no slot is
    //! left pointing at a bus index that no longer exists
    //! Returns whether any slot was reset
    bool clearAuxSendsTargeting(muse::audio::aux_channel_idx_t busIndex);

    //! NOTE: for MixerPanelModel's multi-select fan-out of an FX slot's choice: an invalid newMeta
    //! empties the first slot holding oldResourceId; otherwise newMeta replaces oldResourceId, or goes in
    //! the first empty slot (nothing if this channel already has newMeta)
    void applyFxChange(const QString& oldResourceId, const muse::audio::AudioResourceMeta& newMeta);

    //! NOTE: for MixerPanelModel's undo/redo of a user's aux-send or FX choice
    ChannelAuxSendsState auxSendsState() const;
    void restoreAuxSendsState(const ChannelAuxSendsState& state);
    void restoreFxChain(const muse::audio::AudioFxChain& fxChain);

    //! NOTE: assigns this track's next available aux-send slot to target busIndex - the
    //! same reassignment reassignAuxSend() performs when a user picks a bus from an
    //! existing slot's own dropdown, exposed here as a single public call for
    //! MixerPanelModel's "add FX/Group channel for selected tracks" bulk actions.
    void assignAuxSend(muse::audio::aux_channel_idx_t busIndex);

    //! NOTE: this track's own send slot targeting busIndex, if any - nullptr otherwise.
    //! Used by MixerPanelModel to key an aux-send LEVEL undo command by (trackId,
    //! busIndex) instead of a raw AuxSendItem* (which a slot-list rebuild elsewhere -
    //! compactAuxSendItemKeys() etc. - could otherwise leave dangling by undo time).
    AuxSendItem* auxSendItemForBus(muse::audio::aux_channel_idx_t busIndex) const;

    //! NOTE: begin/end pairs below bracket a single continuous UI gesture (a slider/knob
    //! drag, or a discrete text-field/keyboard-step commit treated as a trivial one-tick
    //! gesture) into ONE undoable change, rather than one per intermediate value while
    //! dragging. end* is a no-op (no signal fires) if the value didn't actually change
    //! between begin* and end*, or if end* is called without a matching prior begin*.
    Q_INVOKABLE void beginVolumeChange();
    Q_INVOKABLE void endVolumeChange();
    Q_INVOKABLE void beginBalanceChange();
    Q_INVOKABLE void endBalanceChange();
    Q_INVOKABLE void beginGainChange();
    Q_INVOKABLE void endGainChange();
    //! NOTE: a delay typed in a field: set, and undoable (see delayChangeCommitted)
    Q_INVOKABLE void commitDelayMs(int delayMs);

    //! NOTE: a channel whose track delay isn't in its input params (a sound track's): read and written through these;
    //! called again when it may have changed
    void bindExternalDelay(std::function<int()> getter, std::function<void(int)> setter);

    //! NOTE The EQ: each call is one undoable change, except within beginEqChange()/endEqChange() (e.g. dragging
    //! a band on the curve), which makes the whole gesture one
    Q_INVOKABLE QVariantList eqBandTypes(int bandIndex) const;
    Q_INVOKABLE void setEqEnabled(bool enabled);
    Q_INVOKABLE void resetEq();
    Q_INVOKABLE void setEqBandEnabled(int bandIndex, bool enabled);
    Q_INVOKABLE void setEqBandType(int bandIndex, int type);
    Q_INVOKABLE void setEqBandFrequency(int bandIndex, double frequency);
    Q_INVOKABLE void setEqBandGain(int bandIndex, double gain);
    Q_INVOKABLE void setEqBandQ(int bandIndex, double q);
    Q_INVOKABLE void beginEqChange();
    Q_INVOKABLE void endEqChange();

public slots:
    void setTitle(QString title);

    void setLeftChannelPressure(float leftChannelPressure);
    void setRightChannelPressure(float rightChannelPressure);

    void setVolumeLevel(float volumeLevel);
    void setBalance(int balance);
    void setGain(int gain);
    void setDelayMs(int delayMs);
    void setEqParams(muse::audio::EqParams eq);
    //! NOTE: see moveAuxSendItem()
    void setAuxSendsOrder(std::vector<muse::audio::aux_channel_idx_t> order);
    void setSolo(bool solo);
    void setMuted(bool mute);
    void setColor(QColor color);
    void setSelected(bool selected);

signals:
    void titleChanged(QString title);

    void leftChannelPressureChanged(float leftChannelPressure);
    void rightChannelPressureChanged(float rightChannelPressure);

    void volumeLevelChanged(float volumeLevel);
    void balanceChanged(int balance);
    void gainChanged(int gain);
    void delayChanged();
    void eqChanged();
    void hasVolumeAutomationChanged();
    void hasBalanceAutomationChanged();
    void soloChanged();
    void mutedChanged();
    void forceMuteChanged();
    void colorChanged();
    void selectedChanged();

    void panelChanged(muse::ui::NavigationPanel* panel);

    void inputParamsChanged(const project::AudioInputParams& params);
    void controlParamsChanged(const project::AudioOutputParams& params);
    void fxChainParamsChanged(const project::AudioOutputParams& params);
    void auxSendsParamsChanged(const project::AudioOutputParams& params);
    void soloMuteStateChanged(const notation::INotationSoloMuteState::SoloMuteState& state);

    //! NOTE: fires only when the user interactively picks a bus from an existing
    //! send slot's own dropdown (handleAuxSendMenuItem's numeric-id branch) - NOT
    //! from assignAuxSend()'s own reassignAuxSend() call (used both by that
    //! dropdown AND by MixerPanelModel's bulk "assign selected tracks to this new
    //! bus" fan-out), or MixerPanelModel's own multi-select fan-out in response to
    //! this same signal would recurse/double-apply across the selection. oldBusIndex
    //! is AuxSendItem::NO_BUS when the slot was previously blank - MixerPanelModel
    //! needs it (captured before the reassignment happens) to make this undoable.
    //! newBusIndex is AuxSendItem::NO_BUS when the user picked "No aux send". stateBefore: this
    //! channel's sends before the change, for the undo.
    void auxSendReassignedByUser(muse::audio::aux_channel_idx_t oldBusIndex, muse::audio::aux_channel_idx_t newBusIndex,
                                 const mu::playback::ChannelAuxSendsState& stateBefore);

    //! NOTE: see OutputResourceItem::fxChangedByUser()
    void fxChangedByUser(const QString& oldResourceId, const muse::audio::AudioResourceMeta& newMeta);

    //! NOTE: see bindInstrumentTrack()
    void outputParamsReceived();

    //! NOTE: fired by the end*Change() methods above once a bracketed gesture actually
    //! changed the value - MixerPanelModel listens to make these undoable.
    void volumeChangeCommitted(float oldVolumeLevel, float newVolumeLevel);
    void balanceChangeCommitted(int oldBalance, int newBalance);
    void gainChangeCommitted(int oldGain, int newGain);
    void delayChangeCommitted(int oldDelayMs, int newDelayMs);
    void eqChangeCommitted(const muse::audio::EqParams& oldEq, const muse::audio::EqParams& newEq);
    //! NOTE: forwarded from the relevant AuxSendItem's own levelChangeCommitted (see
    //! its NOTE) - fired here too (with the slot's stable auxIndex added) so
    //! MixerPanelModel can wire ALL of this channel's continuous-change undo in one
    //! place, the same way it already does for volume/balance/gain above.
    void auxSendLevelChangeCommitted(muse::audio::aux_channel_idx_t busIndex, int oldPercentage, int newPercentage);
    //! NOTE: see moveAuxSendItem()
    void auxSendsOrderChangeCommitted(const std::vector<muse::audio::aux_channel_idx_t>& oldOrder,
                                      const std::vector<muse::audio::aux_channel_idx_t>& newOrder);

    void inputResourceItemChanged();
    void outputResourceItemListChanged();

protected:
    notation::INotationPlaybackPtr notationPlayback() const;

    void setAudioChannelVolumePressure(const muse::audio::audioch_t chNum, const float newValue);

    void applyMuteToOutputParams(const bool isMuted);

    void loadControlParams(muse::audio::volume_db_t volume, muse::audio::balance_t balance, muse::audio::volume_db_t gain,
                           const muse::audio::EqParams& eq);
    void changeEq(const std::function<void(muse::audio::EqParams&)>& change);
    void loadOutputResourceItems(const muse::audio::AudioFxChain& fxChain);
    void loadAuxSendItems(const muse::audio::AuxSendsParams& auxSends);
    void applyAuxSendsOrder(const std::vector<muse::audio::aux_channel_idx_t>& order);
    void persistAuxSendsOrder(const std::vector<muse::audio::aux_channel_idx_t>& order);
    void followEngineOutputParams();

    InputResourceItem* buildInputResourceItem();
    OutputResourceItem* buildOutputResourceItem(const muse::audio::AudioFxParams& fxParams);
    AuxSendItem* buildAuxSendItem(muse::audio::aux_channel_idx_t index, const muse::audio::AuxSendParams& params);

    //! NOTE: items in their new order: each one's chainOrder becomes its position, then sent to the engine
    void applyOutputResourceItemsOrder(const QList<OutputResourceItem*>& items);

    void addBlankSlots(size_t count);
    void removeBlankSlotsFromEnd(size_t count);

    muse::audio::AudioFxChainOrder resolveNewBlankOutputResourceItemOrder() const;

    bool hasBlankAuxSendSlot() const;
    void addAuxSendBlankSlot();
    void addAuxSendBlankSlots(size_t count);
    void removeAuxSendBlankSlotsFromEnd(size_t count);
    void ensureTrailingBlankAuxSlot();
    void reassignAuxSend(AuxSendItem* item, muse::audio::aux_channel_idx_t newBusIndex);
    void blankAuxSend(AuxSendItem* item);
    //! NOTE: renumbers m_auxSendItems' keys to a contiguous 0..N-1 range (preserving order) -
    //! closes any gap left by removing a middle slot, so the next trailing-blank refill can't
    //! land back in that gap instead of at the true end (see blankAuxSend())
    void compactAuxSendItemKeys();
    void handleAuxSendMenuItem(AuxSendItem* item, const QString& menuItemId);
    AuxSendItem::MenuData buildAuxSendMenuData(const AuxSendItem* item) const;
    void updateAuxSendField(const AuxSendItem* item, const std::function<void(muse::audio::AuxSendParams&)>& setter);

    //! NOTE: the display name for an aux bus - a persisted custom name if the user renamed it
    //! (see IProjectAudioSettings::auxName), falling back to the positional "Aux N" default
    QString auxBusDisplayName(muse::audio::aux_channel_idx_t index) const;

    //! NOTE: key is a stable slot order (like AudioFxChainOrder for fx slots), NOT the
    //! target bus index - this keeps a slot's on-screen position fixed when its target
    //! bus is reassigned via the dropdown
    int resolveNewBlankAuxSendItemOrder(const QMap<int, AuxSendItem*>& items) const;

    void openEditor(AbstractAudioResourceItem* item, const muse::rcommand::CommandQuery& command);
    void closeEditor(AbstractAudioResourceItem* item);

    bool askAboutChangingSound();

    void subscribeOnAutomationChanges();
    void updateAutomatedControlParams();

    void setDisplayedVolumeLevel(float volumeLevel);
    void setDisplayedBalance(int balance);
    void setDisplayedGain(int gain);

    Type m_type = Type::Unknown;

    muse::audio::TrackId m_trackId = -1;
    engraving::InstrumentTrackId m_instrumentTrackId;
    muse::audio::aux_channel_idx_t m_auxBusIndex = 0;

    project::AudioInputParams m_inputParams;
    project::AudioOutputParams m_outParams;
    QElapsedTimer m_lastControlParamsSent; // see followEngineOutputParams()
    QElapsedTimer m_lastAuxSendChangeSent; // the same, for a send's level or on/off
    bool m_auxSendItemsLoading = false; // their changes then come from the engine: not sent back to it

    float m_volumeLevel = 0.f;
    int m_balance = 0;
    int m_gain = 0;

    bool m_hasVolumeAutomation = false;
    bool m_hasBalanceAutomation = false;
    bool m_subscribedOnAutomationChanges = false;

    InputResourceItem* m_inputResourceItem = nullptr;
    QMap<muse::audio::AudioFxChainOrder, OutputResourceItem*> m_outputResourceItems;
    QMap<int, AuxSendItem*> m_auxSendItems; // NOTE: keyed by stable slot order, not bus index
    AuxSendItemListModel* m_auxSendItemListModel = nullptr;

    muse::audio::AudioSignalChanges m_audioSignalChanges;

    QString m_title;
    bool m_outputOnly = false;
    bool m_outputParamsLoaded = false;

    float m_leftChannelPressure = 0.0;
    float m_rightChannelPressure = 0.0;

    muse::ui::NavigationPanel* m_panel = nullptr;

    bool m_outputResourceItemsLoading = false;

    bool m_selected = false;

    //! NOTE Set by moveOutputResourceItem() while its fxChainParamsChanged is
    //! still awaiting its engine round-trip, cleared once any fx chain update
    //! (including that round-trip's own echo) arrives via
    //! loadOutputResourceItems() -- guards against a second reorder's result
    //! later being clobbered by the first (now-stale) round-trip's echo. See
    //! moveOutputResourceItem()'s own comment.
    bool m_fxChainUpdatePending = false;

    //! NOTE: set by begin*Change(), consumed and cleared by the matching end*Change() -
    //! see those methods' own NOTE.
    std::optional<float> m_volumeChangeStart;
    std::optional<int> m_balanceChangeStart;
    std::optional<int> m_gainChangeStart;
    std::optional<muse::audio::EqParams> m_eqChangeStart;

    std::function<int()> m_externalDelayGetter;
    std::function<void(int)> m_externalDelaySetter;
};
}
