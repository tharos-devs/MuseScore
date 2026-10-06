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

#include "context/iglobalcontext.h"
#include "project/iprojectaudiosettings.h"
#include "project/iprojectundostack.h"

#include "iplaybackcontroller.h"
#include "channelcolorchange.h"
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

    QML_ELEMENT

    muse::ContextInject<IPlaybackController> controller = { this };
    muse::ContextInject<context::IGlobalContext> context = { this };

public:
    explicit TrackListModel(QObject* parent = nullptr);
    ~TrackListModel() override;

    Q_INVOKABLE void togglePartVisible(int row);

    //! NOTE: undoable, like the Mixer's "Edit color…"/"Reset color" (see ChannelColorChange)
    Q_INVOKABLE void setTrackColor(int row, const QColor& color);
    Q_INVOKABLE void resetTrackColor(int row);

    //! NOTE: the Mixer's global Mute/Solo (see GlobalMuteSoloToggle), on the listed tracks
    Q_INVOKABLE void toggleGlobalMute();
    Q_INVOKABLE void toggleGlobalSolo();

    bool globalMuteEngaged() const;
    bool globalSoloEngaged() const;

    QVariant data(const QModelIndex& index, int role) const override;
    int rowCount(const QModelIndex& parent = QModelIndex()) const override;
    QHash<int, QByteArray> roleNames() const override;

signals:
    void rowCountChanged();
    void globalMuteEngagedChanged();
    void globalSoloEngagedChanged();

private:
    enum Roles {
        ChannelItemRole = Qt::UserRole + 1,
        PartVisibleRole,
        HasArticulationMapRole
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
        //! NOTE: what the title is made of, to only rebuild it when it changes (see updateRowState())
        muse::String titleSource;
    };

    void classBegin() override {}
    void componentComplete() override;

    void onCurrentNotationChanged();
    void subscribeOnUndoStack(const notation::INotationPtr& notation);
    void scheduleReload();
    void reload();
    std::vector<PartTracks> currentTracks() const;
    MixerChannelItem* buildChannelItem(const PartTracks& tracks);
    bool updateRowState(Row& row) const;
    void updateRowStates();
    void updateColors();
    GlobalMuteSoloToggle::Channels channels() const;

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

    notation::INotationPtr m_notation;
    //! NOTE: the master's, when m_notation is a part's: articulation maps are edited on its undo stack
    notation::INotationPtr m_masterNotation;
    project::IProjectAudioSettingsPtr m_audioSettings;
};
}
