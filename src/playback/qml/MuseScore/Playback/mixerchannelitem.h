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

#include <QObject>
#include <qqmlintegration.h>

#include "async/asyncable.h"

#include "modularity/ioc.h"
#include "actions/iactionsdispatcher.h"
#include "context/iglobalcontext.h"
#include "interactive/iinteractive.h"
#include "iplaybackconfiguration.h"
#include "iplaybackcontroller.h"

#include "ui/qml/Muse/Ui/navigationpanel.h"

#include "audio/common/audiotypes.h"
#include "project/iprojectaudiosettings.h"
#include "inputresourceitem.h"
#include "outputresourceitem.h"
#include "auxsenditem.h"

namespace mu::playback {
class MixerChannelItem : public QObject, public muse::async::Asyncable, public muse::Contextable
{
    Q_OBJECT

    Q_PROPERTY(Type type READ type CONSTANT)
    Q_PROPERTY(bool outputOnly READ outputOnly CONSTANT)

    Q_PROPERTY(QString title READ title NOTIFY titleChanged)

    Q_PROPERTY(mu::playback::InputResourceItem * inputResourceItem READ inputResourceItem NOTIFY inputResourceItemChanged)
    Q_PROPERTY(
        QList<mu::playback::OutputResourceItem*> outputResourceItemList READ outputResourceItemList NOTIFY outputResourceItemListChanged)
    Q_PROPERTY(QList<mu::playback::AuxSendItem*> auxSendItemList READ auxSendItemList NOTIFY auxSendItemListChanged)

    Q_PROPERTY(float leftChannelPressure READ leftChannelPressure NOTIFY leftChannelPressureChanged)
    Q_PROPERTY(float rightChannelPressure READ rightChannelPressure NOTIFY rightChannelPressureChanged)

    Q_PROPERTY(float volumeLevel READ volumeLevel WRITE setVolumeLevel NOTIFY volumeLevelChanged)
    Q_PROPERTY(float volumeLevelMin READ volumeLevelMin CONSTANT)
    Q_PROPERTY(float volumeLevelMax READ volumeLevelMax CONSTANT)
    Q_PROPERTY(int balance READ balance WRITE setBalance NOTIFY balanceChanged)
    Q_PROPERTY(int balanceMin READ balanceMin CONSTANT)
    Q_PROPERTY(int balanceMax READ balanceMax CONSTANT)
    Q_PROPERTY(bool hasVolumeAutomation READ hasVolumeAutomation NOTIFY hasVolumeAutomationChanged)
    Q_PROPERTY(bool hasBalanceAutomation READ hasBalanceAutomation NOTIFY hasBalanceAutomationChanged)
    Q_PROPERTY(bool solo READ solo WRITE setSolo NOTIFY soloChanged)
    Q_PROPERTY(bool muted READ muted WRITE setMuted NOTIFY mutedChanged)
    Q_PROPERTY(bool forceMute READ forceMute NOTIFY forceMuteChanged)

    Q_PROPERTY(muse::ui::NavigationPanel * panel READ panel NOTIFY panelChanged)

    QML_ELEMENT;
    QML_UNCREATABLE("Must be created in C++ only")

    muse::GlobalInject<IPlaybackConfiguration> configuration;
    muse::ContextInject<muse::IInteractive> interactive = { this };
    muse::ContextInject<muse::actions::IActionsDispatcher> dispatcher = { this };
    muse::ContextInject<context::IGlobalContext> context = { this };
    muse::ContextInject<IPlaybackController> controller = { this };

public:
    enum class Type {
        Unknown,
        PrimaryInstrument,
        SecondaryInstrument,
        Metronome,
        Aux,
        Master,
    };
    Q_ENUM(Type)

    MixerChannelItem()
        : muse::Contextable(muse::iocCtxForQmlObject(this)) {}
    MixerChannelItem(QObject* parent, Type type, bool outputOnly = false, muse::audio::TrackId trackId = -1);

    ~MixerChannelItem() override;

    Type type() const;

    muse::audio::TrackId trackId() const;

    //! NOTE: only meaningful for Type::Aux - the bus index this channel strip itself represents
    //! (distinct from AuxSendItem::auxIndex(), which is the target bus of one per-track send slot)
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
    bool hasVolumeAutomation() const;
    bool hasBalanceAutomation() const;
    bool solo() const;
    bool muted() const;
    bool forceMute() const;

    muse::ui::NavigationPanel* panel() const;
    void setPanelOrder(int panelOrder);
    void setPanelSection(muse::ui::INavigationSection* section);

    void updateHasAutomationFlags();

    void setOutputResourceItemCount(size_t count);

    void loadInputParams(const project::AudioInputParams& newParams);
    void loadOutputParams(const project::AudioOutputParams& newParams);
    void loadSoloMuteState(const notation::INotationSoloMuteState::SoloMuteState& newState);

    void subscribeOnAudioSignalChanges(muse::audio::AudioSignalChanges& audioSignalChanges);
    void subscribeOnAutomatedControlParamsChanges(muse::audio::AutomatedControlParamsChanges& changes);

    bool outputOnly() const;

    const project::AudioInputParams& inputParams() const;
    const project::AudioOutputParams& outputParams() const;

    InputResourceItem* inputResourceItem() const;
    QList<OutputResourceItem*> outputResourceItemList() const;
    QList<AuxSendItem*> auxSendItemList() const;

    const QMap<int, AuxSendItem*>& auxSendItems() const;

    void resetAudioChannelsVolumePressure();

    //! NOTE: updates the title of any of this track's aux-send slots that target busIndex -
    //! called on every other channel item when an aux bus is renamed
    void renameAuxSendsTargeting(muse::audio::aux_channel_idx_t busIndex, const QString& newName);

public slots:
    void setTitle(QString title);

    void setLeftChannelPressure(float leftChannelPressure);
    void setRightChannelPressure(float rightChannelPressure);

    void setVolumeLevel(float volumeLevel);
    void setBalance(int balance);
    void setSolo(bool solo);
    void setMuted(bool mute);

signals:
    void titleChanged(QString title);

    void leftChannelPressureChanged(float leftChannelPressure);
    void rightChannelPressureChanged(float rightChannelPressure);

    void volumeLevelChanged(float volumeLevel);
    void balanceChanged(int balance);
    void hasVolumeAutomationChanged();
    void hasBalanceAutomationChanged();
    void soloChanged();
    void mutedChanged();
    void forceMuteChanged();

    void panelChanged(muse::ui::NavigationPanel* panel);

    void inputParamsChanged(const project::AudioInputParams& params);
    void controlParamsChanged(const project::AudioOutputParams& params);
    void fxChainParamsChanged(const project::AudioOutputParams& params);
    void auxSendsParamsChanged(const project::AudioOutputParams& params);
    void soloMuteStateChanged(const notation::INotationSoloMuteState::SoloMuteState& state);

    void inputResourceItemChanged();
    void outputResourceItemListChanged();
    void auxSendItemListChanged();

protected:
    notation::INotationPlaybackPtr notationPlayback() const;

    void setAudioChannelVolumePressure(const muse::audio::audioch_t chNum, const float newValue);

    void applyMuteToOutputParams(const bool isMuted);

    void loadOutputResourceItems(const muse::audio::AudioFxChain& fxChain);
    void loadAuxSendItems(const muse::audio::AuxSendsParams& auxSends);

    InputResourceItem* buildInputResourceItem();
    OutputResourceItem* buildOutputResourceItem(const muse::audio::AudioFxParams& fxParams);
    AuxSendItem* buildAuxSendItem(muse::audio::aux_channel_idx_t index, const muse::audio::AuxSendParams& params);

    void addBlankSlots(size_t count);
    void removeBlankSlotsFromEnd(size_t count);

    muse::audio::AudioFxChainOrder resolveNewBlankOutputResourceItemOrder() const;

    bool hasBlankAuxSendSlot() const;
    void addAuxSendBlankSlot();
    void ensureTrailingBlankAuxSlot();
    void reassignAuxSend(AuxSendItem* item, muse::audio::aux_channel_idx_t newBusIndex);
    void blankAuxSend(AuxSendItem* item);
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

    void openEditor(AbstractAudioResourceItem* item, const muse::actions::ActionQuery& action);
    void closeEditor(AbstractAudioResourceItem* item);

    bool askAboutChangingSound();

    void setDisplayedVolumeLevel(float volumeLevel);
    void setDisplayedBalance(int balance);

    Type m_type = Type::Unknown;

    muse::audio::TrackId m_trackId = -1;
    engraving::InstrumentTrackId m_instrumentTrackId;
    muse::audio::aux_channel_idx_t m_auxBusIndex = 0;

    project::AudioInputParams m_inputParams;
    project::AudioOutputParams m_outParams;

    float m_volumeLevel = 0.f;
    int m_balance = 0;

    bool m_hasVolumeAutomation = false;
    bool m_hasBalanceAutomation = false;

    InputResourceItem* m_inputResourceItem = nullptr;
    QMap<muse::audio::AudioFxChainOrder, OutputResourceItem*> m_outputResourceItems;
    QMap<int, AuxSendItem*> m_auxSendItems; // NOTE: keyed by stable slot order, not bus index

    muse::audio::AudioSignalChanges m_audioSignalChanges;
    muse::audio::AutomatedControlParamsChanges m_automatedControlParamsChanges;

    QString m_title;
    bool m_outputOnly = false;

    float m_leftChannelPressure = 0.0;
    float m_rightChannelPressure = 0.0;

    muse::ui::NavigationPanel* m_panel = nullptr;

    bool m_outputResourceItemsLoading = false;
};
}
