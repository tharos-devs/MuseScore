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
#include <vector>

#include <QAbstractListModel>
#include <QQmlParserStatus>
#include <QList>
#include <qqmlintegration.h>

#include "modularity/ioc.h"
#include "async/asyncable.h"
#include "audio/main/iplayback.h"
#include "context/iglobalcontext.h"
#include "playback/iplaybackconfiguration.h"
#include "project/iprojectaudiosettings.h"
#include "project/iprojectvideosettings.h"
#include "project/iprojectundostack.h"
#include "ui/qml/Muse/Ui/navigationsection.h"

#include "iplaybackcontroller.h"
#include "mixerchannelitem.h"

namespace mu::playback {
class MixerPanelModel : public QAbstractListModel, public QQmlParserStatus, public muse::async::Asyncable, public muse::Contextable
{
    Q_OBJECT
    Q_INTERFACES(QQmlParserStatus)

    Q_PROPERTY(
        muse::ui::NavigationSection * navigationSection READ navigationSection WRITE setNavigationSection NOTIFY navigationSectionChanged)
    Q_PROPERTY(int navigationOrderStart READ navigationOrderStart WRITE setNavigationOrderStart NOTIFY navigationOrderStartChanged)

    Q_PROPERTY(int count READ rowCount NOTIFY rowCountChanged)

    Q_PROPERTY(bool globalMuteEngaged READ globalMuteEngaged NOTIFY globalMuteEngagedChanged)
    Q_PROPERTY(bool globalSoloEngaged READ globalSoloEngaged NOTIFY globalSoloEngagedChanged)

    QML_ELEMENT

    muse::GlobalInject<IPlaybackConfiguration> configuration;
    muse::ContextInject<muse::audio::IPlayback> playback = { this };
    muse::ContextInject<IPlaybackController> controller = { this };
    muse::ContextInject<context::IGlobalContext> context = { this };

public:
    explicit MixerPanelModel(QObject* parent = nullptr);

    Q_INVOKABLE QVariantMap get(int index);

    Q_INVOKABLE void selectChannel(mu::playback::MixerChannelItem* item, bool extendSelection, bool rangeSelection);
    Q_INVOKABLE void setColorForSelectedChannels(const QColor& color);
    Q_INVOKABLE void resetColorForSelectedChannels();
    Q_INVOKABLE void clearSelection();

    //! NOTE: same "apply to every currently-selected channel" idiom as
    //! setColorForSelectedChannels() above - called instead of setting
    //! channelItem.muted/.solo directly whenever the clicked channel is itself
    //! currently selected, so a multi-selection's Mute/Solo buttons move together
    //! (clicking one clicked-but-unselected channel's own button still only ever
    //! affects that one channel, same as it always has).
    Q_INVOKABLE void setMutedForSelectedChannels(bool muted);
    Q_INVOKABLE void setSoloForSelectedChannels(bool solo);

    Q_INVOKABLE void renameAuxChannel(mu::playback::MixerChannelItem* channelItem, const QString& name);
    Q_INVOKABLE void deleteAuxChannel(mu::playback::MixerChannelItem* channelItem);

    //! NOTE: returns the aux bus indices of currently-selected channels of the given
    //! type (FX or Group), in current display order - the set the Mixer's
    //! drag-and-drop reorder gesture drags together when the press started on an
    //! already-selected channel of that type.
    Q_INVOKABLE QVariantList selectedAuxBusIndices(bool isGroupBus) const;

    //! NOTE: returns EVERY aux bus index of the given type (FX or Group), selected or
    //! not, in current display order - what the drag-and-drop reorder gesture computes
    //! its live drop target against (see MixerTitleSection.qml).
    Q_INVOKABLE QVariantList auxBusIndicesOfType(bool isGroupBus) const;

    //! NOTE: this aux bus's row index in THIS model (spanning every channel, not just
    //! aux ones) - -1 if it doesn't currently exist. Lets MixerPanel.qml's drop
    //! indicator overlay (spanning the whole channel column, not just the Name row)
    //! compute which horizontal slot to line up with during an aux drag.
    Q_INVOKABLE int auxBusModelIndex(int auxBusIndex) const;

    //! NOTE: commits a completed aux drag-and-drop reorder - draggedAuxBusIndices (in
    //! their current display order) are moved as a block to just before
    //! dropBeforeAuxBusIndex (or to the end of their type's section if
    //! dropBeforeAuxBusIndex is -1). All of them, and dropBeforeAuxBusIndex if given,
    //! must be the same type (FX or Group); this is enforced defensively here, but the
    //! caller (MixerTitleSection.qml) is expected to never construct a cross-type drag
    //! in the first place - see resolveAuxInsertIndex()'s NOTE for why sort order (not
    //! the bus's own stable index) is what actually gets reassigned.
    Q_INVOKABLE void reorderAuxChannels(const QVariantList& draggedAuxBusIndices, int dropBeforeAuxBusIndex);

    Q_INVOKABLE bool canAddAuxBus() const;
    Q_INVOKABLE void addFxChannel();
    Q_INVOKABLE void addGroupChannel();

    //! NOTE: same underlying bus creation as addFxChannel()/addGroupChannel() above,
    //! but also assigns every currently-selected instrument track to the new bus once
    //! it actually exists - see requestNewAuxBusForSelectedTracks()'s own NOTE for why
    //! that assignment can't happen synchronously, right here.
    Q_INVOKABLE void addFxChannelForSelectedTracks();
    Q_INVOKABLE void addGroupChannelForSelectedTracks();

    //! NOTE: toggleGlobalMute/toggleGlobalSolo are simple on/off toggles, not aggregate
    //! setters - the first call remembers which channels (all types except Metronome) are
    //! currently muted/soloed and turns those off; the second call restores mute/solo on
    //! exactly the channels that were remembered, regardless of what else changed on
    //! individual channels in between
    Q_INVOKABLE void toggleGlobalMute();
    Q_INVOKABLE void toggleGlobalSolo();

    QVariant data(const QModelIndex& index, int role) const override;
    int rowCount(const QModelIndex& parent = QModelIndex()) const override;
    QHash<int, QByteArray> roleNames() const override;

    muse::ui::NavigationSection* navigationSection() const;
    void setNavigationSection(muse::ui::NavigationSection* navigationSection);

    int navigationOrderStart() const;
    void setNavigationOrderStart(int navigationOrderStart);

    bool globalMuteEngaged() const;
    bool globalSoloEngaged() const;

signals:
    void navigationSectionChanged();
    void navigationOrderStartChanged();
    void rowCountChanged();

    void globalMuteEngagedChanged();
    void globalSoloEngagedChanged();

private:
    void classBegin() override {}
    void componentComplete() override;
    void init();

    enum Roles {
        ChannelItemRole = Qt::UserRole + 1
    };

    void reload();
    void reloadItems();
    void onTrackAdded(const muse::audio::TrackId& trackId);
    void addItem(MixerChannelItem* item, int index);
    void removeItem(const muse::audio::TrackId trackId);
    void updateItemsPanelsOrder();
    void clear();
    void setupConnections();

    void subscribeOnAutomationChanges();

    void onVideoAttachmentChanged();
    //! NOTE: shared by setColorForSelectedChannels()/resetColorForSelectedChannels() -
    //! applies color to every currently-selected channel and pushes a single undo
    //! command for the whole batch, keyed by each channel's stable trackId() rather
    //! than a raw MixerChannelItem* (which the undo/redo closures might otherwise
    //! outlive - see the QPointer<MixerPanelModel> guard in the .cpp).
    void applyColorToSelectedChannels(const QColor& color, const muse::TranslatableString& actionName);
    //! NOTE: captures every currently-selected instrument track, then requests a new
    //! aux bus of the given type - the actual assignment happens later, once
    //! onTrackAdded() sees that bus resolve (see m_pendingAuxAssignTrackIds' own NOTE
    //! for why this can't be synchronous).
    void requestNewAuxBusForSelectedTracks(bool isGroupBus);
    int resolveInsertIndex(const engraving::InstrumentTrackId& instrumentTrackId) const;
    int resolveVideoInsertIndex() const;
    std::vector<muse::audio::aux_channel_idx_t> sortedAuxIndices() const;
    int resolveAuxInsertIndex(muse::audio::aux_channel_idx_t index, bool isGroupBus) const;
    //! NOTE: shared by sortedAuxIndices() and resolveAuxInsertIndex(), which must stay
    //! consistent with each other - see sortedAuxIndices()'s own NOTE.
    int auxSortOrderOrIndex(muse::audio::aux_channel_idx_t index) const;
    int indexOf(const muse::audio::TrackId trackId) const;

    MixerChannelItem* buildInstrumentChannelItem(const muse::audio::TrackId trackId, const engraving::InstrumentTrackId& instrumentTrackId,
                                                 bool isPrimary = true);
    MixerChannelItem* buildVideoChannelItem();
    MixerChannelItem* buildAuxChannelItem(muse::audio::aux_channel_idx_t index, const muse::audio::TrackId trackId);
    MixerChannelItem* buildMasterChannelItem();

    int masterChannelIndex() const;

    MixerChannelItem* findChannelItem(const muse::audio::TrackId& trackId) const;

    //! NOTE: keeps the global Mute/Solo header buttons' checked state live - see
    //! globalMuteEngaged()/globalSoloEngaged()
    void connectGlobalMuteSoloAggregate(MixerChannelItem* item);

    //! NOTE: makes every channel's volume/balance/gain/aux-send-level bracketed
    //! gestures (see MixerChannelItem::beginVolumeChange() et al.) undoable - called
    //! for every channel type, same as connectGlobalMuteSoloAggregate() above.
    void connectContinuousChangeUndo(MixerChannelItem* item);

    //! NOTE: shared by connectContinuousChangeUndo()'s volume/balance/gain wiring - each
    //! pushes a single undo command that just re-invokes the given MixerChannelItem
    //! setter with the old/new value, looked up fresh by trackId() at undo/redo time
    //! (never a captured MixerChannelItem* - see applyColorToSelectedChannels()'s own
    //! NOTE on why).
    template<typename T>
    void pushChannelFieldUndoCommand(const muse::audio::TrackId& trackId, const muse::TranslatableString& actionName,
                                     void (MixerChannelItem::* setter)(T), T oldValue, T newValue);

    void loadOutputParams(MixerChannelItem* item, const project::AudioOutputParams& params);
    void updateOutputResourceItemCount();
    void updateAuxSendItemCount();

    project::AudioOutputParams effectiveMasterOutputParams() const;

    project::INotationProjectPtr currentProject() const;
    project::IProjectAudioSettingsPtr audioSettings() const;
    project::IProjectVideoSettingsPtr videoSettings() const;
    project::IProjectUndoStackPtr projectUndoStack() const;
    notation::INotationPlaybackPtr notationPlayback() const;
    notation::INotationPartsPtr masterNotationParts() const;

    QList<MixerChannelItem*> m_mixerChannelList;
    MixerChannelItem* m_masterChannelItem = nullptr;

    muse::ui::NavigationSection* m_navigationSection = nullptr;
    int m_navigationOrderStart = 1;

    int m_selectionAnchorIndex = -1;

    QList<muse::audio::TrackId> m_mutedTrackIdsBeforeGlobalMute;
    QList<muse::audio::TrackId> m_soloedTrackIdsBeforeGlobalSolo;

    //! NOTE: instrument tracks to assign to the next aux bus of m_pendingAuxAssignIsGroupBus's
    //! type once it actually resolves via onTrackAdded() - addNewAuxBus()/addNewGroupBus()
    //! are fire-and-forget (no return value, no promise), so the new bus's own index is
    //! only knowable once the engine round-trip completes and IPlaybackController::
    //! trackAdded() fires; this can't be done synchronously right after requesting it.
    QList<muse::audio::TrackId> m_pendingAuxAssignTrackIds;
    bool m_pendingAuxAssignIsGroupBus = false;

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

    //! NOTE: the full identity of a bus this model's OWN redo (see
    //! makeRecreateAuxBusClosure()) just asked PlaybackController to recreate - consumed
    //! by onTrackAdded() once that bus resolves, to restore its original name/display
    //! number/sort order/output params, reassign the same tracks, and update `identity`
    //! in place (see AuxBusIdentity above), since addNewAuxBus()/addNewGroupBus()
    //! otherwise only ever hand out fresh defaults. Also doubles as the signal that a
    //! just-resolved bus came from OUR redo rather than a fresh user action, so
    //! onTrackAdded() doesn't push a second undo command for the same addition.
    struct PendingAuxRedo {
        bool isGroupBus = false;
        project::AudioOutputParams outParams;
        muse::String name;
        muse::audio::aux_channel_idx_t displayNumber = 0;
        int sortOrder = -1;
        QList<muse::audio::TrackId> assignedTrackIds;
        std::shared_ptr<AuxBusIdentity> identity;
    };
    std::optional<PendingAuxRedo> m_pendingAuxRedo;

    //! NOTE: captures everything needed to make a just-created aux bus undoable - undo
    //! removes it, redo recreates it and restores its identity via m_pendingAuxRedo
    //! above. Shares its AuxBusIdentity with makeRemoveAuxBusClosure() below so both
    //! ends of the command track the bus's CURRENT (index, trackId), not just the one
    //! captured at push time - see AuxBusIdentity's own NOTE.
    std::function<void()> makeRecreateAuxBusClosure(bool isGroupBus, const project::AudioOutputParams& outParams, const muse::String& name,
                                                    muse::audio::aux_channel_idx_t displayNumber, int sortOrder,
                                                    const QList<muse::audio::TrackId>& assignedTrackIds,
                                                    std::shared_ptr<AuxBusIdentity> identity);

    //! NOTE: removes the bus AuxBusIdentity currently points at - but only after
    //! verifying it's still actually there (same index AND same trackId), since aux
    //! indices are a recycled pool: without this check, a stale command (e.g. one from
    //! before an intervening manual delete+recreate of the same slot) could silently
    //! remove a completely unrelated, later bus that happened to land on the same
    //! index. A no-longer-matching identity means this command is stale - it just no-ops
    //! rather than acting on the wrong bus.
    std::function<void()> makeRemoveAuxBusClosure(std::shared_ptr<AuxBusIdentity> identity);

    void pushAddAuxBusUndoCommand(muse::audio::aux_channel_idx_t index, const muse::audio::TrackId& trackId, bool isGroupBus,
                                  const QList<muse::audio::TrackId>& assignedTrackIds);
};
}
