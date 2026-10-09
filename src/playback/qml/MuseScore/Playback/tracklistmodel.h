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

#include <QAbstractListModel>
#include <QColor>
#include <QList>
#include <QQmlParserStatus>
#include <qqmlintegration.h>

#include "async/asyncable.h"
#include "modularity/ioc.h"

#include "muse_framework_config.h"
#ifdef MUSE_MODULE_VST
#include "vst/ivstpluginstateprovider.h"
#endif

#include "context/iglobalcontext.h"
#include "project/iprojectaudiosettings.h"
#include "project/iprojectundostack.h"
#include "project/iprojectvideosettings.h"
#include "interactive/iinteractive.h"

#include "iplaybackcontroller.h"
#include "iplaybackconfiguration.h"
#include "channelcolorchange.h"
#include "channelselection.h"
#include "globalmutesolotoggle.h"
#include "mixerchannelitem.h"

namespace mu::playback {
//! NOTE: the Track list panel: one row per part, with the part's Mixer controls that fit in a row (color, mute,
//! solo, sound, VST editor, articulation map) and the part's visibility. Each row's channel item is a Mixer channel
//! item of its own (not the Mixer's), bound to the part's first instrument's track like the Mixer's (see
//! MixerChannelItem::bindInstrumentTrack()): everything goes through the same services as the Mixer and the
//! Timeline, which pick the changes up from there (and the other way around)
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

    //! NOTE: the Video row, above the parts' rows: the Mixer's Video channel (its mute/solo are the video
    //! attachment's, shared with the Video panel and the Timeline), there even without a video, to load one
    Q_PROPERTY(MixerChannelItem * videoChannelItem READ videoChannelItem NOTIFY videoChannelItemChanged)
    Q_PROPERTY(bool hasVideo READ hasVideo NOTIFY hasVideoChanged)

    QML_ELEMENT

    muse::ContextInject<IPlaybackController> controller = { this };
    muse::ContextInject<context::IGlobalContext> context = { this };
    muse::ContextInject<muse::IInteractive> interactive = { this };
    muse::GlobalInject<IPlaybackConfiguration> playbackConfiguration;
#ifdef MUSE_MODULE_VST
    muse::GlobalInject<muse::vst::IVstPluginStateProvider> vstPluginStateProvider;
#endif

public:
    explicit TrackListModel(QObject* parent = nullptr);
    ~TrackListModel() override;

    Q_INVOKABLE void togglePartVisible(int row);

    //! NOTE: like the Mixer's (see ChannelSelection): a click on a row selects it alone, Cmd/Ctrl+click adds or
    //! removes it, Shift+click selects the rows from the last clicked one
    Q_INVOKABLE void selectRow(int row, bool toggle, bool range);
    Q_INVOKABLE void clearSelection();
    Q_INVOKABLE void setMutedForSelectedRows(bool muted);
    Q_INVOKABLE void setSoloForSelectedRows(bool solo);

    //! NOTE: undoable, like the Mixer's "Edit color…"/"Reset color" (see ChannelColorChange); on a selected row,
    //! the whole selection, as one undo step
    Q_INVOKABLE void setTrackColor(int row, const QColor& color);
    Q_INVOKABLE void resetTrackColor(int row);

    //! NOTE: the Mixer's global Mute/Solo (see GlobalMuteSoloToggle), on the listed tracks
    Q_INVOKABLE void toggleGlobalMute();
    Q_INVOKABLE void toggleGlobalSolo();

    bool globalMuteEngaged() const;
    bool globalSoloEngaged() const;

    bool hasArticulationMapColumn() const;
    bool hasEditorColumn() const;

    MixerChannelItem* videoChannelItem() const;
    bool hasVideo() const;

    //! NOTE: same as the Timeline's Video row "Load video" (see Timeline::chooseVideoFile())
    Q_INVOKABLE void chooseVideoFile();
    Q_INVOKABLE void setVideoColor(const QColor& color);
    Q_INVOKABLE void resetVideoColor();

    QVariant data(const QModelIndex& index, int role) const override;
    int rowCount(const QModelIndex& parent = QModelIndex()) const override;
    QHash<int, QByteArray> roleNames() const override;

signals:
    void rowCountChanged();
    void globalMuteEngagedChanged();
    void globalSoloEngagedChanged();
    void columnsChanged();
    void videoChannelItemChanged();
    void hasVideoChanged();

private:
    enum Roles {
        ChannelItemRole = Qt::UserRole + 1,
        PartVisibleRole,
        HasArticulationMapRole,
        IsVstInstrumentRole,
        ArticulationMapEditorOpenedRole,
        InstrumentEditorOpenedRole
    };

    //! NOTE: a part's tracks: its first instrument's, whose sound, instrument window and articulation map the row
    //! shows, and its other instruments' (instrument changes), which follow the row's mute, solo and color
    struct PartTracks {
        engraving::InstrumentTrackId instrumentTrackId;
        muse::audio::TrackId trackId = -1;
        std::vector<engraving::InstrumentTrackId> otherInstrumentTrackIds;

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
    bool isInstrumentEditorOpened(const Row& row) const;
    void subscribeOnUndoStack(const notation::INotationPtr& notation);
    void scheduleReload();
    void reload();
    std::vector<PartTracks> currentTracks() const;
    MixerChannelItem* buildChannelItem(const PartTracks& tracks);
    bool updateRowState(Row& row) const;
    void updateRowStates();
    void updateColors();
    void updateColumns();
    void rebuildVideoChannelItem();
    void loadVideoMuteState();
    void loadVideoColor();
    GlobalMuteSoloToggle::Channels channels() const;

    int rowOf(const engraving::InstrumentTrackId& instrumentTrackId) const;
    const engraving::Part* visibilityPart(const Row& row) const;

    void applyTrackColor(int row, const QColor& color, const muse::TranslatableString& actionName);

    notation::INotationPtr currentNotation() const;
    project::IProjectAudioSettingsPtr audioSettings() const;
    project::IProjectUndoStackPtr projectUndoStack() const;
    project::IProjectVideoSettingsPtr videoSettings() const;

    QList<Row> m_rows;
    bool m_reloadScheduled = false;
    bool m_rebuildRequired = false;

    GlobalMuteSoloToggle m_globalMuteSolo;
    int m_selectionAnchorIndex = -1;

    //! NOTE: see updateColumns()
    bool m_hasArticulationMapColumn = false;
    bool m_hasEditorColumn = false;

    notation::INotationPtr m_notation;
    //! NOTE: the master's, when m_notation is a part's: articulation maps are edited on its undo stack
    notation::INotationPtr m_masterNotation;
    project::IProjectAudioSettingsPtr m_audioSettings;
    notation::INotationArticulationMapsPtr m_articulationMaps;
    project::IProjectVideoSettingsPtr m_videoSettings;

    MixerChannelItem* m_videoChannelItem = nullptr;
    bool m_hasVideo = false;
};
}
