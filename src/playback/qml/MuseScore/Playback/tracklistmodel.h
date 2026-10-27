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

#include <map>
#include <optional>

#include <QAbstractListModel>
#include <QColor>
#include <QList>
#include <QPointer>
#include <QQmlParserStatus>
#include <qqmlintegration.h>

#include "async/asyncable.h"
#include "modularity/ioc.h"

#include "muse_framework_config.h"
#ifdef MUSE_MODULE_VST
#include "vst/ivstpluginstateprovider.h"
#endif

#include "audio/main/iplayback.h"
#include "project/iprojectsession.h"
#include "project/iprojectaudiosettings.h"
#include "project/iprojectundostack.h"
#include "interactive/iinteractive.h"

#include "iplaybackcontroller.h"
#include "iplaybackconfiguration.h"
#include "auxbuseditor.h"
#include "channelcolorchange.h"
#include "channelselection.h"
#include "globalmutesolotoggle.h"
#include "mixerchannelitem.h"
#include "soundtrackchannel.h"

namespace mu::engraving {
class Part;
}

namespace mu::playback {
//! NOTE: the Track list panel: one row per part, with the part's Mixer controls that fit in a row (color, mute,
//! solo, sound, VST editor, articulation map; a hidden part dimmed), then one row per Aux FX/Aux Group bus
//! (color, mute, solo), in the Mixer's order. Each row's channel item is a Mixer channel item of its own (not the
//! Mixer's), bound to the part's first instrument's track or to the bus like the Mixer's (see
//! MixerChannelItem::bindInstrumentTrack()/bindAuxBus()): everything goes through the same services as the Mixer
//! and the Timeline, which pick the changes up from there (and the other way around). The Metronome, Video and
//! Reference audio rows are above them; each kind of rows can be hidden (View menu, see ChannelCategoriesMenu)
class TrackListModel : public QAbstractListModel, public QQmlParserStatus, public muse::async::Asyncable, public muse::Contextable
{
    Q_OBJECT
    Q_INTERFACES(QQmlParserStatus)

    Q_PROPERTY(int count READ rowCount NOTIFY rowCountChanged)

    Q_PROPERTY(bool globalMuteEngaged READ globalMuteEngaged NOTIFY globalMuteEngagedChanged)
    Q_PROPERTY(bool globalSoloEngaged READ globalSoloEngaged NOTIFY globalSoloEngagedChanged)

    //! NOTE: whether a row of the list has an articulation map button (a VST instrument or a map) / a sound with a
    //! window: the rows' columns for them only take room when one does
    Q_PROPERTY(bool hasArticulationMapColumn READ hasArticulationMapColumn NOTIFY columnsChanged)
    Q_PROPERTY(bool hasEditorColumn READ hasEditorColumn NOTIFY columnsChanged)

    //! NOTE: the sound tracks' rows (see SoundTrackChannel), above the parts' rows: the Video row, the Mixer's Video
    //! channel (its mute/solo are the video attachment's, shared with the Video panel and the Timeline), then the
    //! Reference audio row (its mute/solo shared with the Audio sync window), each there even without its source,
    //! to load one
    Q_PROPERTY(MixerChannelItem * videoChannelItem READ videoChannelItem NOTIFY soundTrackChannelItemsChanged)
    Q_PROPERTY(bool hasVideo READ hasVideo NOTIFY soundTrackSourcesChanged)
    Q_PROPERTY(MixerChannelItem * referenceAudioChannelItem READ referenceAudioChannelItem NOTIFY soundTrackChannelItemsChanged)
    Q_PROPERTY(bool hasReferenceAudio READ hasReferenceAudio NOTIFY soundTrackSourcesChanged)

    //! NOTE: the Metronome row, above the sound tracks' rows: its mute only
    Q_PROPERTY(MixerChannelItem * metronomeChannelItem READ metronomeChannelItem NOTIFY metronomeChannelItemChanged)

    //! NOTE: the channel shown below the list, with the Mixer's controls: the first selected row's (a track or a bus);
    //! with none selected, the one shown last, or the first instrument track's (see updateStripChannelItem())
    Q_PROPERTY(MixerChannelItem * stripChannelItem READ stripChannelItem NOTIFY stripChannelItemChanged)
    Q_PROPERTY(bool stripChannelSelected READ stripChannelSelected NOTIFY stripChannelItemChanged)
    //! NOTE: the height of that area, remembered (a height above its controls': all of them, whatever their height)
    Q_PROPERTY(int stripHeight READ stripHeight WRITE setStripHeight NOTIFY stripHeightChanged)
    //! NOTE: the stripHeight meaning all of it
    Q_PROPERTY(int fullStripHeight READ fullStripHeight CONSTANT)
    //! NOTE: the height of all of the strip's controls, last measured (remembered): the strip keeps it with no channel
    Q_PROPERTY(int stripContentHeight READ stripContentHeight WRITE setStripContentHeight NOTIFY stripContentHeightChanged)

    Q_PROPERTY(bool videoVisible READ videoVisible NOTIFY categoriesVisibleChanged)
    Q_PROPERTY(bool referenceAudioVisible READ referenceAudioVisible NOTIFY categoriesVisibleChanged)
    Q_PROPERTY(bool metronomeVisible READ metronomeVisible NOTIFY categoriesVisibleChanged)

    QML_ELEMENT

    muse::ContextInject<IPlaybackController> controller = { this };
    muse::ContextInject<muse::audio::IPlayback> playback = { this };
    muse::ContextInject<project::IProjectSession> context = { this };
    muse::ContextInject<muse::IInteractive> interactive = { this };
    muse::GlobalInject<IPlaybackConfiguration> playbackConfiguration;
#ifdef MUSE_MODULE_VST
    muse::GlobalInject<muse::vst::IVstPluginStateProvider> vstPluginStateProvider;
#endif

public:
    explicit TrackListModel(QObject* parent = nullptr);
    ~TrackListModel() override;

    //! NOTE: like the Mixer's (see ChannelSelection): a click on a row selects it alone, Cmd/Ctrl+click adds or
    //! removes it, Shift+click selects the rows from the last clicked one
    Q_INVOKABLE void selectRow(int row, bool toggle, bool range);
    Q_INVOKABLE void clearSelection();
    Q_INVOKABLE void setMutedForSelectedRows(bool muted);
    Q_INVOKABLE void setSoloForSelectedRows(bool solo);

    //! NOTE: the Mixer's "Add FX/Group channel (for selected tracks)" and a bus's "Delete channel" (see AuxBusEditor)
    Q_INVOKABLE QVariantList auxBusMenuItems(int row) const;
    Q_INVOKABLE bool handleAuxBusMenuItem(int row, const QString& itemId);

    //! NOTE: undoable, like the Mixer's "Edit color…"/"Reset color" (see ChannelColorChange); on a selected row,
    //! the whole selection, as one undo step
    Q_INVOKABLE void setTrackColor(int row, const QColor& color);
    Q_INVOKABLE void resetTrackColor(int row);

    //! NOTE: folders of tracks and buses (see project::TrackListFolder), only the Track list's organization: the
    //! score's order isn't changed (a folder's tracks are in the score's order). All undoable. A row: as shown.
    //! A new folder: above the first selected track or bus (above its folder when it's in one), or first
    Q_INVOKABLE void addFolder();
    Q_INVOKABLE void renameFolder(int row, const QString& name);
    Q_INVOKABLE void removeFolder(int row);
    Q_INVOKABLE void toggleFolderCollapsed(int row);
    //! NOTE: the row's track or bus (on a selected row, the selection's) into the folder of folderRow (-1: out of
    //! any folder)
    Q_INVOKABLE void moveRowToFolder(int row, int folderRow);
    //! NOTE: the folder of row before the top-level row beforeRow (the end: rowCount())
    Q_INVOKABLE void moveFolder(int row, int beforeRow);
    //! NOTE: whether moveFolder() can put it there: before a row that isn't in a folder, or at the end
    Q_INVOKABLE bool canMoveFolderBefore(int row, int beforeRow) const;
    //! NOTE: a row's menu items for folders: "Add new folder" (an empty one above the row), the "Move to folder"
    //! submenu; handleMoveToFolderMenuItem() returns false for another item
    Q_INVOKABLE QVariantList folderMenuItems(int row) const;
    Q_INVOKABLE bool handleMoveToFolderMenuItem(int row, const QString& itemId);

    //! NOTE: a folder's Mute/Solo, engaged while any of its tracks and buses is muted/soloed: a click then unmutes/unsolos
    //! them all, else mutes/solos them all (Solo: those that have one; a track muted by another's solo isn't touched)
    Q_INVOKABLE void toggleFolderMute(int row);
    Q_INVOKABLE void toggleFolderSolo(int row);

    //! NOTE: the Mixer's global Mute/Solo (see GlobalMuteSoloToggle), on the listed tracks
    Q_INVOKABLE void toggleGlobalMute();
    Q_INVOKABLE void toggleGlobalSolo();

    bool globalMuteEngaged() const;
    bool globalSoloEngaged() const;

    bool hasArticulationMapColumn() const;
    bool hasEditorColumn() const;

    MixerChannelItem* videoChannelItem() const;
    bool hasVideo() const;
    MixerChannelItem* referenceAudioChannelItem() const;
    bool hasReferenceAudio() const;

    MixerChannelItem* metronomeChannelItem() const;

    MixerChannelItem* stripChannelItem() const;
    bool stripChannelSelected() const;
    int stripHeight() const;
    int fullStripHeight() const;
    int stripContentHeight() const;
    void setStripContentHeight(int height);
    void setStripHeight(int height);

    bool videoVisible() const;
    bool referenceAudioVisible() const;
    bool metronomeVisible() const;

    //! NOTE: same as the Timeline's Video row "Load video" (see Timeline::chooseVideoFile())
    Q_INVOKABLE void chooseVideoFile();
    //! NOTE: same as the Audio sync window's (see audiosync::chooseReferenceAudio())
    Q_INVOKABLE void chooseReferenceAudioFile();
    //! NOTE: a sound track's row (its channel item): not undoable, like its other output params changed from the Mixer
    Q_INVOKABLE void setSoundTrackColor(mu::playback::MixerChannelItem* item, const QColor& color);
    Q_INVOKABLE void resetSoundTrackColor(mu::playback::MixerChannelItem* item);
    //! NOTE: undoable, like a track's (see setTrackColor())
    Q_INVOKABLE void setMetronomeColor(const QColor& color);
    Q_INVOKABLE void resetMetronomeColor();

    QVariant data(const QModelIndex& index, int role) const override;
    int rowCount(const QModelIndex& parent = QModelIndex()) const override;
    QHash<int, QByteArray> roleNames() const override;

signals:
    void rowCountChanged();
    void globalMuteEngagedChanged();
    void globalSoloEngagedChanged();
    void columnsChanged();
    void soundTrackChannelItemsChanged();
    void soundTrackSourcesChanged();
    void metronomeChannelItemChanged();
    void categoriesVisibleChanged();
    void stripChannelItemChanged();
    void stripHeightChanged();
    void stripContentHeightChanged();
    //! NOTE: the score's selection got rows selected: the first one is to be scrolled to if it's out of view
    void rowShowRequested(int row);

private:
    enum Roles {
        ChannelItemRole = Qt::UserRole + 1,
        PartVisibleRole,
        HasArticulationMapRole,
        IsVstInstrumentRole,
        ArticulationMapEditorOpenedRole,
        InstrumentEditorOpenedRole,
        IsFolderRole,
        FolderNameRole,
        FolderCollapsedRole,
        FolderMutedRole,
        FolderSoloedRole,
        InFolderRole
    };

    //! NOTE: a row as shown: a channel (index in m_rows, in the score's order) or a folder (index in m_folders)
    struct DisplayRow {
        int channelRow = -1;
        int folder = -1;
        bool inFolder = false;
    };

    //! NOTE: a part's tracks: its first instrument's, whose sound, instrument window and articulation map the row
    //! shows, and its other instruments' (instrument changes), which follow the row's mute, solo and color
    //! NOTE: or a bus's (auxIndex set, no instrument track)
    struct PartTracks {
        engraving::InstrumentTrackId instrumentTrackId;
        muse::audio::TrackId trackId = -1;
        std::vector<engraving::InstrumentTrackId> otherInstrumentTrackIds;
        std::optional<muse::audio::aux_channel_idx_t> auxIndex;

        bool operator==(const PartTracks& other) const = default;
    };

    struct Row {
        PartTracks tracks;
        MixerChannelItem* item = nullptr;
        bool partVisible = true;
        bool hasArticulationMap = false;
        //! NOTE: the articulation map button is there for a VST instrument even without a map, for its menu
        bool isVstInstrument = false;
        //! NOTE: whether the track's windows are open: their buttons are colored then
        bool articulationMapEditorOpened = false;
        bool instrumentEditorOpened = false;
        //! NOTE: what the title is made of, to only rebuild it when it changes (see updateRowState())
        muse::String titleSource;
    };

    void classBegin() override {}
    void componentComplete() override;

    void onCurrentNotationChanged();
    void selectRowsOfScoreSelection();
    void updateStripChannelItem();
    static bool isSameChannel(const PartTracks& tracks1, const PartTracks& tracks2);
    void requestFirstSelectedRowShown();

    static muse::String channelKey(const PartTracks& tracks);
    //! NOTE: m_displayRows from m_rows and m_folders (inside a model reset)
    void rebuildDisplayRows();
    //! NOTE: the project's folders, when they changed (here, or undone/redone)
    void loadFolders();
    void applyFolders(const project::TrackListFolders& folders, const muse::TranslatableString& actionName);
    int channelRowAt(int row) const;
    int folderAt(int row) const;
    int displayRowOfChannel(int channelRow) const;
    int folderOfChannel(int channelRow) const;
    std::vector<int> channelRowsToMove(int channelRow) const;
    GlobalMuteSoloToggle::Channels folderChannels(int folder) const;
    static bool canFolderSolo(const MixerChannelItem* item);
    bool isFolderMuted(int folder) const;
    bool isFolderSoloed(int folder) const;
    void notifyFolderRowsChanged();
    project::TrackListFolders foldersWithChannelsMoved(project::TrackListFolders folders, const std::vector<int>& moved,
                                                       int targetFolder) const;
    void moveRowToNewFolder(int row);
    void addFolderAbove(int row);
    QVariantMap moveToFolderMenu(int row) const;
    void insertFolderBefore(project::TrackListFolders& folders, project::TrackListFolder folder, int beforeRow) const;
    //! NOTE: what the Mixer's channels have too: undo, the fan-out of a picked effect/aux send to the selection,
    //! all their slots (see MixerChannelItem::showAllSlots()), the automation
    void connectMixerControls(MixerChannelItem* item, const PartTracks& tracks);
    bool isInstrumentEditorOpened(const Row& row) const;
    void subscribeOnUndoStack(const notation::INotationPtr& notation);
    void scheduleReload();
    void reload();
    std::vector<PartTracks> currentTracks() const;
    MixerChannelItem* buildChannelItem(const PartTracks& tracks);
    void connectGlobalMuteSolo(MixerChannelItem* item);
    bool isCategoryVisible(ChannelCategory category) const;
    void rebuildMetronomeChannelItem();
    QString auxBusTitle(muse::audio::aux_channel_idx_t index) const;
    bool updateRowState(Row& row) const;
    void updateRowStates();
    void updateColors();
    void updateAuxBusTitles();
    void updateColumns();
    struct SoundTrackRow;
    const SoundTrackRow* soundTrackRow(MixerChannelItem::Type type) const;
    void connectSoundTrackSettings();
    void updateSoundTrackSources();
    void rebuildSoundTrackChannelItem(SoundTrackRow& row);
    void loadSoundTrackColors();
    void applyMetronomeColor(const QColor& color, const muse::TranslatableString& actionName);
    GlobalMuteSoloToggle::Channels channels() const;
    GlobalMuteSoloToggle::Channels globalMuteSoloChannels() const;

    int rowOf(const engraving::InstrumentTrackId& instrumentTrackId) const;
    const engraving::Part* visibilityPart(const Row& row) const;

    void applyTrackColor(int row, const QColor& color, const muse::TranslatableString& actionName);

    notation::INotationPtr currentNotation() const;
    project::IProjectAudioSettingsPtr audioSettings() const;
    project::IProjectUndoStackPtr projectUndoStack() const;

    QList<Row> m_rows;
    bool m_reloadScheduled = false;
    bool m_rebuildRequired = false;

    GlobalMuteSoloToggle m_globalMuteSolo;
    int m_selectionAnchorIndex = -1;

    project::TrackListFolders m_folders;
    std::vector<DisplayRow> m_displayRows;

    QPointer<MixerChannelItem> m_stripChannelItem;
    std::optional<PartTracks> m_stripTracks;
    bool m_stripChannelSelected = false;

    std::shared_ptr<AuxBusEditor> m_auxBusEditor;

    //! NOTE: see updateColumns()
    bool m_hasArticulationMapColumn = false;
    bool m_hasEditorColumn = false;

    notation::INotationPtr m_notation;
    //! NOTE: the master's, when m_notation is a part's: articulation maps are edited on its undo stack
    notation::INotationPtr m_masterNotation;
    project::IProjectAudioSettingsPtr m_audioSettings;
    notation::INotationArticulationMapsPtr m_articulationMaps;

    //! NOTE: a sound track's row (see SoundTrackChannel), in display order
    struct SoundTrackRow {
        SoundTrackChannel channel;
        MixerChannelItem* item = nullptr;
        bool hasSource = false;
        //! NOTE: the current project's (to disconnect from when it changes)
        std::optional<muse::async::Notification> settingsChanged;
    };
    std::vector<SoundTrackRow> m_soundTrackRows;

    MixerChannelItem* m_metronomeChannelItem = nullptr;
};
}
