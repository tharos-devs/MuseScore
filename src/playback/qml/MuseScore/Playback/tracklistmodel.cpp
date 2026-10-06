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

#include "tracklistmodel.h"

#include <QCoreApplication>

#include "async/notifylist.h"
#include "containers.h"
#include "log.h"

#include "engraving/dom/part.h"
#include "engraving/dom/sharedpart.h"

#include "notation/imasternotation.h"
#include "notation/inotation.h"
#include "notation/inotationparts.h"
#include "notation/inotationsolomutestate.h"
#include "notation/inotationundostack.h"
#include "notation/utilities/partutilities.h"

#include "project/inotationproject.h"

using namespace muse;
using namespace mu::playback;
using namespace muse::audio;
using namespace mu::engraving;
using namespace mu::notation;
using namespace mu::project;

TrackListModel::TrackListModel(QObject* parent)
    : QAbstractListModel(parent), muse::Contextable(muse::iocCtxForQmlObject(this))
{
}

TrackListModel::~TrackListModel()
{
    //! NOTE: same as the Mixer's channel items (see ~MixerPanelModel()): the delegates showing them are
    //! destroyed after this model, so the items are only deleted one event loop iteration later
    QList<MixerChannelItem*> items;
    for (const Row& row : std::as_const(m_rows)) {
        row.item->setParent(nullptr);
        items.push_back(row.item);
    }

    QMetaObject::invokeMethod(qApp, [items]() {
        for (MixerChannelItem* item : items) {
            item->deleteLater();
        }
    }, Qt::QueuedConnection);
}

void TrackListModel::componentComplete()
{
    controller()->playbackInitedChanged().onReceive(this, [this](bool) {
        scheduleReload();
    });

    controller()->trackAdded().onReceive(this, [this](const TrackId) {
        scheduleReload();
    });

    controller()->trackRemoved().onReceive(this, [this](const TrackId) {
        scheduleReload();
    });

    //! NOTE: the live mute (with the one forced by another track's solo), see the Mixer's
    controller()->trackMuteStateChanged().onReceive(
        this, [this](const InstrumentTrackId& instrumentTrackId, bool muted, bool forceMute) {
        const int row = rowOf(instrumentTrackId);
        if (row >= 0) {
            m_rows.at(row).item->loadMuteForceMuteState(muted, forceMute);
        }
    });

    context()->currentNotationChanged().onNotify(this, [this]() {
        onCurrentNotationChanged();
    });

    onCurrentNotationChanged();
}

void TrackListModel::onCurrentNotationChanged()
{
    for (const INotationPtr& notation : { m_notation, m_masterNotation }) {
        if (notation) {
            notation->undoStack()->stackChanged().disconnect(this);
        }
    }

    if (m_notation) {
        m_notation->parts()->partsChanged().disconnect(this);
        if (m_notation->soloMuteState()) {
            m_notation->soloMuteState()->trackSoloMuteStateChanged().disconnect(this);
        }
    }

    if (m_audioSettings) {
        m_audioSettings->settingsChanged().disconnect(this);
    }

    m_notation = currentNotation();
    m_masterNotation = nullptr;
    m_audioSettings = audioSettings();

    //! NOTE: even with the same tracks, the solo/mute states are the new notation's
    m_rebuildRequired = true;
    m_globalMuteSolo.clear();

    if (m_notation) {
        subscribeOnUndoStack(m_notation);

        const INotationPtr masterNotation = m_notation->masterNotation()->notation();
        if (masterNotation && masterNotation->undoStack() != m_notation->undoStack()) {
            m_masterNotation = masterNotation;
            subscribeOnUndoStack(m_masterNotation);
        }

        m_notation->parts()->partsChanged().onNotify(this, [this]() {
            scheduleReload();
        });

        if (m_notation->soloMuteState()) {
            m_notation->soloMuteState()->trackSoloMuteStateChanged().onReceive(
                this, [this](const InstrumentTrackId& instrumentTrackId, const INotationSoloMuteState::SoloMuteState& state) {
                const int row = rowOf(instrumentTrackId);
                if (row >= 0) {
                    m_rows.at(row).item->loadSoloMuteState(state);
                }
            });
        }
    }

    if (m_audioSettings) {
        //! NOTE: a color changed from the Mixer, the Timeline or here
        m_audioSettings->settingsChanged().onNotify(this, [this]() {
            updateColors();
        });
    }

    scheduleReload();
}

//! NOTE: covers the parts' visibility, names and order, stave sharing, and the articulation maps (all undoable)
void TrackListModel::subscribeOnUndoStack(const INotationPtr& notation)
{
    notation->undoStack()->stackChanged().onNotify(this, [this]() {
        scheduleReload();
    });
}

//! NOTE: coalesces the bursts of changes (e.g. one trackAdded per part when a score opens)
void TrackListModel::scheduleReload()
{
    if (m_reloadScheduled) {
        return;
    }

    m_reloadScheduled = true;
    QMetaObject::invokeMethod(this, [this]() {
        m_reloadScheduled = false;
        reload();
    }, Qt::QueuedConnection);
}

//! NOTE: the current notation's parts (only the part's own in a part score, like the Timeline). A stave sharing
//! combined part has no track of its own: skipped, the parts it combines are listed instead
std::vector<TrackListModel::PartTracks> TrackListModel::currentTracks() const
{
    std::vector<PartTracks> result;
    if (!m_notation || !controller()->isPlaybackInited()) {
        return result;
    }

    const IPlaybackController::InstrumentTrackIdMap& trackIdMap = controller()->instrumentTrackIdMap();

    for (const Part* part : m_notation->parts()->partList()) {
        const Instrument* instrument = part->instrument();
        if (!instrument) {
            continue;
        }

        PartTracks tracks;
        tracks.instrumentTrackId = { part->id(), instrument->id() };

        auto it = trackIdMap.find(tracks.instrumentTrackId);
        if (it == trackIdMap.end()) {
            continue;
        }
        tracks.trackId = it->second;

        for (const InstrumentTrackId& trackId : part->instrumentTrackIdList()) {
            if (trackId != tracks.instrumentTrackId && muse::contains(trackIdMap, trackId)) {
                tracks.otherInstrumentTrackIds.push_back(trackId);
            }
        }

        result.push_back(std::move(tracks));
    }

    return result;
}

void TrackListModel::reload()
{
    TRACEFUNC;

    const std::vector<PartTracks> tracks = currentTracks();

    bool sameTracks = static_cast<int>(tracks.size()) == m_rows.size();
    for (size_t i = 0; sameTracks && i < tracks.size(); ++i) {
        sameTracks = m_rows.at(static_cast<int>(i)).tracks == tracks.at(i);
    }

    if (sameTracks && !m_rebuildRequired) {
        updateRowStates();
        return;
    }

    //! NOTE: like the Mixer's, a remembered global mute/solo is about the tracks it was taken on
    if (!sameTracks) {
        m_globalMuteSolo.clear();
    }

    m_rebuildRequired = false;

    //! NOTE: the new rows are complete before the reset: no change notification in the middle of it
    QList<Row> newRows;
    for (const PartTracks& partTracks : tracks) {
        Row row;
        row.tracks = partTracks;
        row.item = buildChannelItem(partTracks);
        updateRowState(row);
        newRows.push_back(std::move(row));
    }

    beginResetModel();

    QList<MixerChannelItem*> oldItems;
    for (const Row& row : std::as_const(m_rows)) {
        row.item->disconnect(this);
        oldItems.push_back(row.item);
    }
    m_rows = std::move(newRows);

    endResetModel();

    updateColors();

    emit rowCountChanged();
    emit globalMuteEngagedChanged();
    emit globalSoloEngagedChanged();

    //! NOTE: only once the views have released the delegates showing them
    for (MixerChannelItem* item : oldItems) {
        item->deleteLater();
    }
}

MixerChannelItem* TrackListModel::buildChannelItem(const PartTracks& tracks)
{
    MixerChannelItem* item = new MixerChannelItem(this, MixerChannelItem::Type::PrimaryInstrument, false /*outputOnly*/, tracks.trackId);
    item->bindInstrumentTrack(tracks.instrumentTrackId);

    //! NOTE: forceMute too, see GlobalMuteSoloToggle::muteEngaged()
    connect(item, &MixerChannelItem::mutedChanged, this, &TrackListModel::globalMuteEngagedChanged);
    connect(item, &MixerChannelItem::forceMuteChanged, this, &TrackListModel::globalMuteEngagedChanged);
    connect(item, &MixerChannelItem::soloChanged, this, &TrackListModel::globalSoloEngagedChanged);

    //! NOTE: the row is the whole part: its other instruments' tracks follow its mute and solo
    const std::vector<InstrumentTrackId> otherTrackIds = tracks.otherInstrumentTrackIds;
    if (!otherTrackIds.empty()) {
        connect(item, &MixerChannelItem::soloMuteStateChanged, this,
                [this, otherTrackIds](const INotationSoloMuteState::SoloMuteState& state) {
            for (const InstrumentTrackId& trackId : otherTrackIds) {
                controller()->setTrackSoloMuteState(trackId, state);
            }
        });
    }

    return item;
}

//! NOTE: whether the row's visibility or articulation map changed (its title has no role of its own)
bool TrackListModel::updateRowState(Row& row) const
{
    const Part* part = m_notation ? m_notation->parts()->part(row.tracks.instrumentTrackId.partId) : nullptr;

    //! NOTE: building the title parses the names' HTML: only when they changed (this runs on every undo stack change)
    const muse::String titleSource = part ? part->partName() + muse::String(u"\n") + part->longName() + muse::String(u"\n")
                                     + part->instrumentName() : muse::String();
    if (titleSource != row.titleSource || row.item->title().isEmpty()) {
        row.titleSource = titleSource;
        row.item->setTitle(PartUtilities::displayName(part));
    }

    const Part* shownPart = visibilityPart(row);
    const bool partVisible = shownPart && shownPart->show();
    const bool hasArticulationMap = !row.item->articulationMapName().isNull();

    if (row.partVisible == partVisible && row.hasArticulationMap == hasArticulationMap) {
        return false;
    }

    row.partVisible = partVisible;
    row.hasArticulationMap = hasArticulationMap;
    return true;
}

void TrackListModel::updateRowStates()
{
    for (int i = 0; i < m_rows.size(); ++i) {
        if (updateRowState(m_rows[i])) {
            const QModelIndex modelIndex = index(i);
            emit dataChanged(modelIndex, modelIndex, { PartVisibleRole, HasArticulationMapRole });
        }
    }
}

void TrackListModel::updateColors()
{
    const IProjectAudioSettingsPtr settings = audioSettings();
    if (!settings) {
        return;
    }

    for (const Row& row : std::as_const(m_rows)) {
        if (settings->trackHasExistingOutputParams(row.tracks.instrumentTrackId)) {
            row.item->setColor(settings->trackOutputParams(row.tracks.instrumentTrackId).color);
        }
    }
}

//! NOTE: with "Enable stave sharing", the combined part is the one shown (the parts it combines are hidden, see
//! SharedPart): the eye shows and changes its visibility, like the Timeline's
const Part* TrackListModel::visibilityPart(const Row& row) const
{
    const Part* part = m_notation ? m_notation->parts()->part(row.tracks.instrumentTrackId.partId) : nullptr;
    if (part && part->sharedPart() && part->sharedPart()->enabled()) {
        return part->sharedPart();
    }

    return part;
}

void TrackListModel::togglePartVisible(int row)
{
    if (row < 0 || row >= m_rows.size() || !m_notation) {
        return;
    }

    //! NOTE: undoable, same as the Layout panel's eye
    if (const Part* part = visibilityPart(m_rows.at(row))) {
        m_notation->parts()->setPartVisible(part->id(), !part->show());
    }
}

void TrackListModel::setTrackColor(int row, const QColor& color)
{
    applyTrackColor(row, color, TranslatableString("undoableAction", "Change Mixer channel color"));
}

void TrackListModel::resetTrackColor(int row)
{
    applyTrackColor(row, QColor(), TranslatableString("undoableAction", "Reset Mixer channel color"));
}

//! NOTE: the whole part: its other instruments' tracks too
void TrackListModel::applyTrackColor(int row, const QColor& color, const TranslatableString& actionName)
{
    if (row < 0 || row >= m_rows.size()) {
        return;
    }

    const PartTracks& tracks = m_rows.at(row).tracks;
    std::vector<ChannelColorChange::Target> targets { { tracks.instrumentTrackId, std::nullopt } };
    for (const InstrumentTrackId& trackId : tracks.otherInstrumentTrackIds) {
        targets.push_back({ trackId, std::nullopt });
    }

    ChannelColorChange::apply(audioSettings(), projectUndoStack(), targets, color, actionName);
}

GlobalMuteSoloToggle::Channels TrackListModel::channels() const
{
    GlobalMuteSoloToggle::Channels channels;
    for (const Row& row : m_rows) {
        channels.push_back(row.item);
    }

    return channels;
}

bool TrackListModel::globalMuteEngaged() const
{
    return GlobalMuteSoloToggle::muteEngaged(channels());
}

bool TrackListModel::globalSoloEngaged() const
{
    return GlobalMuteSoloToggle::soloEngaged(channels());
}

void TrackListModel::toggleGlobalMute()
{
    m_globalMuteSolo.toggleMute(channels());
    emit globalMuteEngagedChanged();
}

void TrackListModel::toggleGlobalSolo()
{
    m_globalMuteSolo.toggleSolo(channels());
    emit globalSoloEngagedChanged();
}

int TrackListModel::rowOf(const InstrumentTrackId& instrumentTrackId) const
{
    for (int i = 0; i < m_rows.size(); ++i) {
        if (m_rows.at(i).tracks.instrumentTrackId == instrumentTrackId) {
            return i;
        }
    }

    return -1;
}

QVariant TrackListModel::data(const QModelIndex& index, int role) const
{
    if (!index.isValid() || index.row() >= m_rows.size()) {
        return QVariant();
    }

    const Row& row = m_rows.at(index.row());

    switch (role) {
    case ChannelItemRole: return QVariant::fromValue(row.item);
    case PartVisibleRole: return row.partVisible;
    case HasArticulationMapRole: return row.hasArticulationMap;
    }

    return QVariant();
}

int TrackListModel::rowCount(const QModelIndex&) const
{
    return m_rows.size();
}

QHash<int, QByteArray> TrackListModel::roleNames() const
{
    static const QHash<int, QByteArray> roles {
        { ChannelItemRole, "channelItem" },
        { PartVisibleRole, "partVisible" },
        { HasArticulationMapRole, "hasArticulationMap" }
    };

    return roles;
}

INotationPtr TrackListModel::currentNotation() const
{
    return context()->currentNotation();
}

IProjectAudioSettingsPtr TrackListModel::audioSettings() const
{
    const INotationProjectPtr project = context()->currentProject();
    return project ? project->audioSettings() : nullptr;
}

IProjectUndoStackPtr TrackListModel::projectUndoStack() const
{
    const INotationProjectPtr project = context()->currentProject();
    return project ? project->undoStack() : nullptr;
}
