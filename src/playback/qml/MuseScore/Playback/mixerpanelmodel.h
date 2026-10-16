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
#include "interactive/iinteractive.h"
#include "playback/iplaybackconfiguration.h"
#include "project/iprojectaudiosettings.h"
#include "project/iprojectvideosettings.h"
#include "project/iprojectundostack.h"
#include "ui/qml/Muse/Ui/navigationsection.h"

#include "iplaybackcontroller.h"
#include "auxbuseditor.h"
#include "channelcolorchange.h"
#include "channelselection.h"
#include "globalmutesolotoggle.h"
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
    muse::ContextInject<muse::IInteractive> interactive = { this };

public:
    explicit MixerPanelModel(QObject* parent = nullptr);
    ~MixerPanelModel() override;

    Q_INVOKABLE QVariantMap get(int index);

    Q_INVOKABLE void selectChannel(mu::playback::MixerChannelItem* item, bool extendSelection, bool rangeSelection);
    Q_INVOKABLE void setColorForSelectedChannels(const QColor& color);
    Q_INVOKABLE void resetColorForSelectedChannels();
    //! NOTE: like the Track list's Video row (not undoable, like the video's other output params)
    Q_INVOKABLE void setVideoColor(const QColor& color);
    Q_INVOKABLE void resetVideoColor();
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
    //! it actually exists - see AuxBusEditor::addAuxBusForTracks()'s own NOTE for why
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

    //! NOTE: an Audio FX slot's effect dropped on another channel's slots (any channels): moved there, or copied
    //! (duplicate), with its settings, before the slot at insertIndex (0..slot count). Undoable. False when it can't
    //! be dropped (no empty slot there)
    Q_INVOKABLE bool dropFx(mu::playback::MixerChannelItem* source, int sourceIndex, mu::playback::MixerChannelItem* target,
                            int insertIndex, bool duplicate);

    //! NOTE: the mouse cursor while dragging an Audio FX slot's effect (e.g. a copy's), whatever is under the mouse,
    //! until restoreDragCursor()
    Q_INVOKABLE void setDragCursor(int cursorShape);
    Q_INVOKABLE void restoreDragCursor();

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
    //! Returns the removed items, for deleteItemsLater()
    QList<MixerChannelItem*> clear();
    static void deleteItemsLater(const QList<MixerChannelItem*>& items);
    void setupConnections();

    void subscribeOnAutomationChanges();

    void onVideoAttachmentChanged();
    //! NOTE: shared by setColorForSelectedChannels()/resetColorForSelectedChannels() -
    //! applies color to every currently-selected channel as a single undo command (see
    //! ChannelColorChange)
    void applyColorToSelectedChannels(const QColor& color, const muse::TranslatableString& actionName);
    QList<muse::audio::TrackId> selectedInstrumentTrackIds() const;
    int resolveInsertIndex(const engraving::InstrumentTrackId& instrumentTrackId) const;
    int resolveVideoInsertIndex() const;
    //! NOTE: whether a kind of channels is shown ("…" menu, see ChannelCategoriesMenu)
    bool isCategoryVisible(ChannelCategory category) const;
    bool isAuxBusVisible(muse::audio::aux_channel_idx_t index) const;
    bool isInstrumentTrackVisible(const engraving::InstrumentTrackId& instrumentTrackId) const;
    std::vector<muse::audio::aux_channel_idx_t> sortedAuxIndices() const;
    int resolveAuxInsertIndex(muse::audio::aux_channel_idx_t index, bool isGroupBus) const;
    int indexOf(const muse::audio::TrackId trackId) const;

    MixerChannelItem* buildInstrumentChannelItem(const muse::audio::TrackId trackId, const engraving::InstrumentTrackId& instrumentTrackId,
                                                 bool isPrimary = true);
    MixerChannelItem* buildVideoChannelItem(const muse::audio::TrackId trackId);
    void loadVideoMuteState(MixerChannelItem* item);
    MixerChannelItem* buildAuxChannelItem(muse::audio::aux_channel_idx_t index, const muse::audio::TrackId trackId);
    MixerChannelItem* buildMasterChannelItem();

    int masterChannelIndex() const;

    MixerChannelItem* findChannelItem(const muse::audio::TrackId& trackId) const;

    //! NOTE: keeps the global Mute/Solo header buttons' checked state live - see
    //! globalMuteEngaged()/globalSoloEngaged()
    void connectGlobalMuteSoloAggregate(MixerChannelItem* item);
    GlobalMuteSoloToggle::Channels globalMuteSoloChannels() const;

    //! NOTE: an effect or aux send picked: undoable, and applied to the other selected tracks (see ChannelEditUndo)
    void connectPickedChangeUndo(MixerChannelItem* item);

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

    GlobalMuteSoloToggle m_globalMuteSolo;

    std::shared_ptr<AuxBusEditor> m_auxBusEditor;

    bool m_dragCursorSet = false;
};
}
