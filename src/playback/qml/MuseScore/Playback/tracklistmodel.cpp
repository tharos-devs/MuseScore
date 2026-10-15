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

#include <algorithm>
#include <set>

#include <QCoreApplication>

#include "async/notifylist.h"
#include "containers.h"
#include "log.h"
#include "settings.h"
#include "translation.h"
#include "io/path.h"

#include "engraving/dom/engravingitem.h"
#include "engraving/dom/part.h"
#include "engraving/dom/sharedpart.h"

#include "notation/imasternotation.h"
#include "notation/inotationplayback.h"
#include "notation/inotationarticulationmaps.h"
#include "notation/inotation.h"
#include "notation/inotationinteraction.h"
#include "notation/inotationselection.h"
#include "notation/inotationselectionrange.h"
#include "notation/inotationparts.h"
#include "notation/inotationsolomutestate.h"
#include "notation/inotationundostack.h"
#include "notation/utilities/partutilities.h"

#include "project/inotationproject.h"

#include "channeleditundo.h"

using namespace muse;
using namespace mu::playback;
using namespace muse::audio;
using namespace mu::engraving;
using namespace mu::notation;
using namespace mu::project;

static const muse::Settings::Key STRIP_HEIGHT_KEY("playback", "playback/trackList/stripHeight");
//! NOTE: higher than any strip: all of it shown, even once it grows (see TrackListPanel.qml)
static constexpr int FULL_STRIP_HEIGHT = 100000;

TrackListModel::TrackListModel(QObject* parent)
    : QAbstractListModel(parent), muse::Contextable(muse::iocCtxForQmlObject(this))
{
    muse::settings()->setDefaultValue(STRIP_HEIGHT_KEY, muse::Val(FULL_STRIP_HEIGHT));
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
    for (MixerChannelItem* item : { m_videoChannelItem, m_metronomeChannelItem }) {
        if (item) {
            item->setParent(nullptr);
            items.push_back(item);
        }
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

    //! NOTE: the video's sound track comes and goes with the video (and once its audio is decoded)
    controller()->trackAdded().onReceive(this, [this](const TrackId) {
        scheduleReload();
        if (m_videoChannelItem && m_videoChannelItem->trackId() != controller()->videoTrackId()) {
            rebuildVideoChannelItem();
        }
    });

    controller()->trackRemoved().onReceive(this, [this](const TrackId) {
        scheduleReload();
        if (m_videoChannelItem && m_videoChannelItem->trackId() != controller()->videoTrackId()) {
            rebuildVideoChannelItem();
        }
    });

    controller()->videoMuteStateChanged().onReceive(this, [this](bool, bool) {
        loadVideoMuteState();
    });

    //! NOTE: the live mute (with the one forced by another track's solo), see the Mixer's
    controller()->trackMuteStateChanged().onReceive(
        this, [this](const InstrumentTrackId& instrumentTrackId, bool muted, bool forceMute) {
        const int row = rowOf(instrumentTrackId);
        if (row >= 0) {
            m_rows.at(row).item->loadMuteForceMuteState(muted, forceMute);
        } else if (m_metronomeChannelItem && m_metronomeChannelItem->instrumentTrackId() == instrumentTrackId) {
            m_metronomeChannelItem->loadMuteForceMuteState(muted, forceMute);
        }
    });

    //! NOTE: a kind of rows shown or hidden (View menu)
    playbackConfiguration()->channelCategoryVisibleChanged().onReceive(this, [this](ChannelListPanel panel, ChannelCategory, bool) {
        if (panel == ChannelListPanel::TrackList) {
            m_rebuildRequired = true;
            scheduleReload();
            emit categoriesVisibleChanged();
            emit globalMuteEngagedChanged();
            emit globalSoloEngagedChanged();
        }
    });

    //! NOTE: a bus named after its first effect
    controller()->auxChannelNameChanged().onReceive(this, [this](aux_channel_idx_t, const std::string&) {
        updateAuxBusTitles();
    });

    context()->currentNotationChanged().onNotify(this, [this]() {
        onCurrentNotationChanged();
    });

#ifdef MUSE_MODULE_VST
    if (vstPluginStateProvider()) {
        vstPluginStateProvider()->editorsOpenedChanged().onNotify(this, [this]() {
            updateRowStates();
        });
    }
#endif

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
        m_notation->interaction()->selectionChanged().disconnect(this);
        if (m_notation->soloMuteState()) {
            m_notation->soloMuteState()->trackSoloMuteStateChanged().disconnect(this);
        }
    }

    if (m_audioSettings) {
        m_audioSettings->settingsChanged().disconnect(this);
    }

    if (m_articulationMaps) {
        m_articulationMaps->editorsOpenedChanged().disconnect(this);
    }

    if (m_videoSettings) {
        m_videoSettings->settingsChanged().disconnect(this);
    }

    m_notation = currentNotation();
    m_masterNotation = nullptr;
    m_audioSettings = audioSettings();
    m_articulationMaps = m_notation ? m_notation->masterNotation()->articulationMaps() : nullptr;
    m_videoSettings = videoSettings();

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

        m_notation->interaction()->selectionChanged().onNotify(this, [this]() {
            selectRowsOfScoreSelection();
        });

        if (m_notation->soloMuteState()) {
            m_notation->soloMuteState()->trackSoloMuteStateChanged().onReceive(
                this, [this](const InstrumentTrackId& instrumentTrackId, const INotationSoloMuteState::SoloMuteState& state) {
                const int row = rowOf(instrumentTrackId);
                if (row >= 0) {
                    m_rows.at(row).item->loadSoloMuteState(state);
                } else if (m_metronomeChannelItem && m_metronomeChannelItem->instrumentTrackId() == instrumentTrackId) {
                    m_metronomeChannelItem->loadSoloMuteState(state);
                }
            });
        }
    }

    if (m_articulationMaps) {
        m_articulationMaps->editorsOpenedChanged().onNotify(this, [this]() {
            updateRowStates();
        });
    }

    if (m_videoSettings) {
        //! NOTE: a video loaded/removed, its mute/solo changed (here, in the Video panel, the Mixer or the Timeline)
        m_videoSettings->settingsChanged().onNotify(this, [this]() {
            const bool hasVideo = m_videoSettings->attachment().isValid();
            if (m_hasVideo != hasVideo) {
                m_hasVideo = hasVideo;
                emit hasVideoChanged();
                emit globalMuteEngagedChanged();
                emit globalSoloEngagedChanged();
            }
            loadVideoMuteState();
        });
    }

    m_hasVideo = m_videoSettings && m_videoSettings->attachment().isValid();
    emit hasVideoChanged();
    rebuildVideoChannelItem();

    if (m_audioSettings) {
        //! NOTE: a color or a bus's name changed from the Mixer, the Timeline or here
        m_audioSettings->settingsChanged().onNotify(this, [this]() {
            updateColors();
            updateAuxBusTitles();
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

    const bool instrumentsVisible = isCategoryVisible(ChannelCategory::Instruments);
    for (const Part* part : m_notation->parts()->partList()) {
        const Instrument* instrument = part->instrument();
        if (!instrumentsVisible || !instrument) {
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

    //! NOTE: the buses, in the Mixer's order (FX buses, then Group buses)
    const IPlaybackController::AuxTrackIdMap& auxTrackIdMap = controller()->auxTrackIdMap();
    for (aux_channel_idx_t index : controller()->sortedAuxBusIndices()) {
        const ChannelCategory category = controller()->isAuxBusGroup(index) ? ChannelCategory::AuxGroup : ChannelCategory::AuxFx;
        if (!isCategoryVisible(category)) {
            continue;
        }

        PartTracks tracks;
        tracks.trackId = auxTrackIdMap.at(index);
        tracks.auxIndex = index;
        result.push_back(std::move(tracks));
    }

    return result;
}

bool TrackListModel::isCategoryVisible(ChannelCategory category) const
{
    return playbackConfiguration()->isChannelCategoryVisible(ChannelListPanel::TrackList, category);
}

QString TrackListModel::auxBusTitle(aux_channel_idx_t index) const
{
    //! NOTE: a name given in the Mixer wins, like there
    const IProjectAudioSettingsPtr settings = audioSettings();
    const QString customName = settings ? settings->auxName(index).toQString() : QString();
    return !customName.isEmpty() ? customName : QString::fromStdString(controller()->auxChannelName(index));
}

MixerChannelItem* TrackListModel::metronomeChannelItem() const
{
    return m_metronomeChannelItem;
}

//! NOTE: like the Mixer's Metronome channel, for its mute and level
void TrackListModel::rebuildMetronomeChannelItem()
{
    MixerChannelItem* oldItem = m_metronomeChannelItem;
    m_metronomeChannelItem = nullptr;

    const INotationProjectPtr project = context()->currentProject();
    const INotationPlaybackPtr notationPlayback = project ? project->masterNotation()->playback() : nullptr;
    if (notationPlayback && controller()->isPlaybackInited()) {
        const InstrumentTrackId& metronomeTrackId = notationPlayback->metronomeTrackId();
        const IPlaybackController::InstrumentTrackIdMap& trackIdMap = controller()->instrumentTrackIdMap();
        auto it = trackIdMap.find(metronomeTrackId);
        if (it != trackIdMap.end()) {
            m_metronomeChannelItem = new MixerChannelItem(this, MixerChannelItem::Type::Metronome, false /*outputOnly*/, it->second);
            m_metronomeChannelItem->bindInstrumentTrack(metronomeTrackId);
            m_metronomeChannelItem->setTitle(muse::qtrc("playback", "Metronome"));
        }
    }

    emit metronomeChannelItemChanged();

    //! NOTE: only once the view has released it
    if (oldItem) {
        oldItem->disconnect(this);
        oldItem->deleteLater();
    }
}

bool TrackListModel::videoVisible() const
{
    return isCategoryVisible(ChannelCategory::Video);
}

bool TrackListModel::metronomeVisible() const
{
    return isCategoryVisible(ChannelCategory::Metronome);
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

    //! NOTE: the rows selected keep their selection (e.g. when a kind of channels is shown or hidden); the first
    //! rows get the score's
    const bool firstRows = m_rows.isEmpty();
    std::vector<PartTracks> selectedTracks;
    for (const Row& row : std::as_const(m_rows)) {
        if (row.item->selected()) {
            selectedTracks.push_back(row.tracks);
        }
    }
    m_selectionAnchorIndex = -1;

    m_rebuildRequired = false;

    rebuildMetronomeChannelItem();

    //! NOTE: the new rows are complete before the reset: no change notification in the middle of it
    QList<Row> newRows;
    for (const PartTracks& partTracks : tracks) {
        Row row;
        row.tracks = partTracks;
        row.item = buildChannelItem(partTracks);
        updateRowState(row);

        auto isSameChannel = [&partTracks](const PartTracks& tracks) {
            if (tracks.auxIndex || partTracks.auxIndex) {
                return tracks.auxIndex == partTracks.auxIndex;
            }
            return tracks.instrumentTrackId == partTracks.instrumentTrackId;
        };
        const bool wasSelected = std::any_of(selectedTracks.cbegin(), selectedTracks.cend(), isSameChannel);
        if (wasSelected) {
            row.item->setSelected(true);
            if (m_selectionAnchorIndex < 0) {
                m_selectionAnchorIndex = newRows.size();
            }
        }

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
    updateColumns();

    //! NOTE: only once the views have released the delegates showing them
    for (MixerChannelItem* item : oldItems) {
        item->deleteLater();
    }

    if (firstRows) {
        selectRowsOfScoreSelection();
    }
    updateStripChannelItem();
}

//! NOTE: forceMute too, see GlobalMuteSoloToggle::muteEngaged()
void TrackListModel::connectGlobalMuteSolo(MixerChannelItem* item)
{
    connect(item, &MixerChannelItem::mutedChanged, this, &TrackListModel::globalMuteEngagedChanged);
    connect(item, &MixerChannelItem::forceMuteChanged, this, &TrackListModel::globalMuteEngagedChanged);
    connect(item, &MixerChannelItem::soloChanged, this, &TrackListModel::globalSoloEngagedChanged);
}

MixerChannelItem* TrackListModel::buildChannelItem(const PartTracks& tracks)
{
    if (tracks.auxIndex) {
        MixerChannelItem* item = new MixerChannelItem(this, MixerChannelItem::Type::Aux, true /*outputOnly*/, tracks.trackId);
        item->bindAuxBus(*tracks.auxIndex);
        item->subscribeOnTrackAudioSignalChanges();
        connectGlobalMuteSolo(item);
        connectMixerControls(item, tracks);
        return item;
    }

    MixerChannelItem* item = new MixerChannelItem(this, MixerChannelItem::Type::PrimaryInstrument, false /*outputOnly*/, tracks.trackId);
    item->bindInstrumentTrack(tracks.instrumentTrackId);

    connectGlobalMuteSolo(item);
    connectMixerControls(item, tracks);
    //! NOTE: the sound changed: whether it's a VST instrument (articulation map button) and has a window
    connect(item->inputResourceItem(), &InputResourceItem::hasNativeEditorSupportChanged, this, [this]() {
        updateRowStates();
        updateColumns();
    });

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

void TrackListModel::connectMixerControls(MixerChannelItem* item, const PartTracks& tracks)
{
    ChannelEditUndo::connectContinuousChanges(item, this, [this]() { return projectUndoStack(); });
    ChannelEditUndo::connectPickedChanges(item, this, [this]() { return projectUndoStack(); }, [this]() { return channels(); });

    //! NOTE: only the strip shows slots (see updateStripChannelItem())
    auto showAllSlotsInStrip = [this, item]() {
        if (item == m_stripChannelItem) {
            item->showAllSlots();
        }
    };
    connect(item, &MixerChannelItem::outputParamsReceived, this, showAllSlotsInStrip);
    connect(item, &MixerChannelItem::fxChainParamsChanged, this, showAllSlotsInStrip);
    connect(item, &MixerChannelItem::auxSendsParamsChanged, this, showAllSlotsInStrip);

    connect(item, &MixerChannelItem::selectedChanged, this, &TrackListModel::updateStripChannelItem);

    //! NOTE: the volume and pan curves drawn on the score: shown by the fader and knob, which can't be moved then
    if (!tracks.auxIndex) {
        QPointer<MixerChannelItem> guard(item);
        playback()->automatedControlParamsChanges(tracks.trackId)
        .onResolve(this, [guard](muse::audio::AutomatedControlParamsChanges changes) {
            if (guard) {
                guard->subscribeOnAutomatedControlParamsChanges(changes);
            }
        });
    }
}

bool TrackListModel::isInstrumentEditorOpened(const Row& row) const
{
#ifdef MUSE_MODULE_VST
    const InputResourceItem* sound = row.item->inputResourceItem();
    return sound && vstPluginStateProvider()
           && vstPluginStateProvider()->isInstrumentEditorOpened(sound->params().resourceMeta.id, row.tracks.trackId);
#else
    UNUSED(row);
    return false;
#endif
}

//! NOTE: whether the row's visibility, articulation map, sound type or open windows changed (its title has no role
//! of its own)
//! NOTE: on every audio settings change (e.g. a Mixer fader moving): only the buses' titles, not the whole rows
void TrackListModel::updateAuxBusTitles()
{
    for (const Row& row : std::as_const(m_rows)) {
        if (row.tracks.auxIndex) {
            const QString title = auxBusTitle(*row.tracks.auxIndex);
            if (row.item->title() != title) {
                row.item->setTitle(title);
            }
        }
    }
}

bool TrackListModel::updateRowState(Row& row) const
{
    if (row.tracks.auxIndex) {
        const QString title = auxBusTitle(*row.tracks.auxIndex);
        if (row.item->title() != title) {
            row.item->setTitle(title);
        }
        return false;
    }

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
    const InputResourceItem* sound = row.item->inputResourceItem();
    const bool isVstInstrument = sound && sound->params().type() == AudioSourceType::Vsti;
    const bool articulationMapEditorOpened = m_articulationMaps && m_articulationMaps->isEditorOpened(row.tracks.instrumentTrackId);
    const bool instrumentEditorOpened = isInstrumentEditorOpened(row);

    if (row.partVisible == partVisible && row.hasArticulationMap == hasArticulationMap && row.isVstInstrument == isVstInstrument
        && row.articulationMapEditorOpened == articulationMapEditorOpened && row.instrumentEditorOpened == instrumentEditorOpened) {
        return false;
    }

    row.partVisible = partVisible;
    row.hasArticulationMap = hasArticulationMap;
    row.isVstInstrument = isVstInstrument;
    row.articulationMapEditorOpened = articulationMapEditorOpened;
    row.instrumentEditorOpened = instrumentEditorOpened;
    return true;
}

void TrackListModel::updateRowStates()
{
    bool changed = false;
    for (int i = 0; i < m_rows.size(); ++i) {
        if (updateRowState(m_rows[i])) {
            const QModelIndex modelIndex = index(i);
            emit dataChanged(modelIndex, modelIndex, { PartVisibleRole, HasArticulationMapRole, IsVstInstrumentRole,
                                                       ArticulationMapEditorOpenedRole, InstrumentEditorOpenedRole });
            changed = true;
        }
    }

    if (changed) {
        updateColumns();
    }
}

//! NOTE: kept rather than computed on each read: every row's delegate reads them
void TrackListModel::updateColumns()
{
    const bool hasArticulationMapColumn = std::any_of(m_rows.cbegin(), m_rows.cend(), [](const Row& row) {
        return row.hasArticulationMap || row.isVstInstrument;
    });
    const bool hasEditorColumn = std::any_of(m_rows.cbegin(), m_rows.cend(), [](const Row& row) {
        return row.item->inputResourceItem() && row.item->inputResourceItem()->hasNativeEditorSupport();
    });

    if (hasArticulationMapColumn == m_hasArticulationMapColumn && hasEditorColumn == m_hasEditorColumn) {
        return;
    }

    m_hasArticulationMapColumn = hasArticulationMapColumn;
    m_hasEditorColumn = hasEditorColumn;
    emit columnsChanged();
}

bool TrackListModel::hasArticulationMapColumn() const
{
    return m_hasArticulationMapColumn;
}

bool TrackListModel::hasEditorColumn() const
{
    return m_hasEditorColumn;
}

void TrackListModel::updateColors()
{
    const IProjectAudioSettingsPtr settings = audioSettings();
    if (!settings) {
        return;
    }

    for (const Row& row : std::as_const(m_rows)) {
        if (row.tracks.auxIndex) {
            row.item->setColor(settings->auxOutputParams(*row.tracks.auxIndex).color);
        } else if (settings->trackHasExistingOutputParams(row.tracks.instrumentTrackId)) {
            row.item->setColor(settings->trackOutputParams(row.tracks.instrumentTrackId).color);
        }
    }

    loadVideoColor();
}

MixerChannelItem* TrackListModel::videoChannelItem() const
{
    return m_videoChannelItem;
}

bool TrackListModel::hasVideo() const
{
    return m_hasVideo;
}

//! NOTE: like the Mixer's Video channel (see MixerPanelModel::buildVideoChannelItem()), for its mute, solo, color
//! and level only: its mute/solo go to the video attachment, which PlaybackController applies to the engine
void TrackListModel::rebuildVideoChannelItem()
{
    MixerChannelItem* oldItem = m_videoChannelItem;

    const TrackId trackId = controller()->videoTrackId();
    m_videoChannelItem = new MixerChannelItem(this, MixerChannelItem::Type::Video, true /*outputOnly*/, trackId);
    m_videoChannelItem->setTitle(muse::qtrc("playback", "Video"));
    connectGlobalMuteSolo(m_videoChannelItem);
    if (trackId != INVALID_TRACK_ID) {
        m_videoChannelItem->subscribeOnTrackAudioSignalChanges();
    }

    loadVideoMuteState();
    loadVideoColor();

    connect(m_videoChannelItem, &MixerChannelItem::soloMuteStateChanged, this,
            [this](const INotationSoloMuteState::SoloMuteState& state) {
        updateVideoAttachment(videoSettings(), [&state](VideoAttachmentSettings& attachment) {
            attachment.muted = state.solo ? false : state.mute;
            attachment.solo = state.solo;
        });
    });

    emit videoChannelItemChanged();

    //! NOTE: only once the view has released it
    if (oldItem) {
        oldItem->disconnect(this);
        oldItem->deleteLater();
    }
}

//! NOTE: the effective mute includes the live force-mute (another track soloed), like the Mixer's
void TrackListModel::loadVideoMuteState()
{
    const IProjectVideoSettingsPtr settings = videoSettings();
    if (!m_videoChannelItem || !settings) {
        return;
    }

    const VideoAttachmentSettings& attachment = settings->attachment();
    m_videoChannelItem->loadSoloMuteState({ attachment.muted, attachment.solo });

    const bool forceMute = controller()->isVideoForceMuted();
    m_videoChannelItem->loadMuteForceMuteState((attachment.muted && !attachment.solo) || forceMute, forceMute);
}

void TrackListModel::loadVideoColor()
{
    if (m_videoChannelItem && audioSettings()) {
        m_videoChannelItem->setColor(controller()->videoOutputParams().color);
    }
}

//! NOTE: not undoable, like the other video output params (volume, pan...) changed from the Mixer
void TrackListModel::setVideoColor(const QColor& color)
{
    AudioOutputParams params = controller()->videoOutputParams();
    params.color = color;
    controller()->setVideoOutputParams(params);
    loadVideoColor();
}

void TrackListModel::resetVideoColor()
{
    setVideoColor(QColor());
}

void TrackListModel::chooseVideoFile()
{
    const IProjectVideoSettingsPtr settings = videoSettings();
    if (!settings) {
        return;
    }

    const io::path_t currentPath = settings->attachment().path;
    const std::vector<std::string> filter {
        muse::trc("playback", "Video files") + " (*.mp4 *.mov *.m4v *.avi *.mkv *.webm)",
        muse::trc("playback", "All files") + " (*)"
    };

    const io::path_t path = interactive()->selectOpeningFileSync(muse::trc("playback", "Choose video"),
                                                                 currentPath.empty() ? io::path_t() : io::dirpath(currentPath),
                                                                 filter);
    if (path.empty()) {
        return;
    }

    VideoAttachmentSettings updated = settings->attachment();
    updated.path = path;
    //! NOTE: hit points are timed against the previous video's own footage
    updated.hitPoints.clear();
    settings->setAttachment(updated);

    playbackConfiguration()->addRecentVideoFile(path.toQString());
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

    const bool forSelection = m_rows.at(row).item->selected();

    std::vector<ChannelColorChange::Target> targets;
    for (int i = 0; i < m_rows.size(); ++i) {
        const Row& r = m_rows.at(i);
        if (forSelection ? !r.item->selected() : i != row) {
            continue;
        }

        targets.push_back({ r.tracks.instrumentTrackId, r.tracks.auxIndex });
        for (const InstrumentTrackId& trackId : r.tracks.otherInstrumentTrackIds) {
            targets.push_back({ trackId, std::nullopt });
        }
    }

    ChannelColorChange::apply(audioSettings(), projectUndoStack(), targets, color, actionName);
}

void TrackListModel::selectRow(int row, bool toggle, bool range)
{
    if (row < 0 || row >= m_rows.size()) {
        return;
    }

    ChannelSelection::select(channels(), m_rows.at(row).item, toggle, range, m_selectionAnchorIndex,
                             [](const MixerChannelItem*) { return true; });
}

//! NOTE: the rows of the parts with something selected in the score (a stave sharing combined part: the parts
//! it combines), none without a selection or with the whole score's elements only
void TrackListModel::selectRowsOfScoreSelection()
{
    if (!m_notation) {
        return;
    }

    std::set<ID> partIds;
    const INotationSelectionPtr selection = m_notation->interaction()->selection();
    if (selection->isRange()) {
        for (const Part* part : selection->range()->selectedParts()) {
            partIds.insert(part->id());
        }
    } else {
        for (const EngravingItem* element : selection->elements()) {
            // a tempo, a rehearsal mark, a system text... is the whole score's, though on the first staff
            if (!element || element->systemFlag()) {
                continue;
            }
            if (const Part* part = element->part()) {
                partIds.insert(part->id());
            }
        }
    }

    m_selectionAnchorIndex = -1;
    for (int i = 0; i < m_rows.size(); ++i) {
        const Row& row = m_rows.at(i);
        bool selected = false;
        if (!row.tracks.auxIndex) {
            const Part* shownPart = visibilityPart(row);
            selected = muse::contains(partIds, row.tracks.instrumentTrackId.partId)
                       || (shownPart && muse::contains(partIds, shownPart->id()));
        }

        if (row.item->selected() != selected) {
            row.item->setSelected(selected);
        }
        if (selected && m_selectionAnchorIndex < 0) {
            m_selectionAnchorIndex = i; // a Shift+click in the list extends from there
        }
    }
}

void TrackListModel::updateStripChannelItem()
{
    MixerChannelItem* item = nullptr;
    for (const Row& row : std::as_const(m_rows)) {
        if (row.item->selected()) {
            item = row.item;
            break;
        }
    }

    if (m_stripChannelItem != item) {
        m_stripChannelItem = item;
        if (item) {
            item->showAllSlots();
        }
        emit stripChannelItemChanged();
    }
}

MixerChannelItem* TrackListModel::stripChannelItem() const
{
    return m_stripChannelItem;
}

int TrackListModel::fullStripHeight() const
{
    return FULL_STRIP_HEIGHT;
}

int TrackListModel::stripHeight() const
{
    return muse::settings()->value(STRIP_HEIGHT_KEY).toInt();
}

void TrackListModel::setStripHeight(int height)
{
    if (height == stripHeight()) {
        return;
    }

    muse::settings()->setSharedValue(STRIP_HEIGHT_KEY, muse::Val(height));
    emit stripHeightChanged();
}

void TrackListModel::clearSelection()
{
    ChannelSelection::clear(channels(), m_selectionAnchorIndex);
}

void TrackListModel::setMutedForSelectedRows(bool muted)
{
    ChannelSelection::setMuted(channels(), muted);
}

void TrackListModel::setSoloForSelectedRows(bool solo)
{
    ChannelSelection::setSolo(channels(), solo);
}

//! NOTE: the listed channels: the selection's, and the global Mute/Solo's (with the shown Video row)
GlobalMuteSoloToggle::Channels TrackListModel::channels() const
{
    GlobalMuteSoloToggle::Channels channels;
    for (const Row& row : m_rows) {
        channels.push_back(row.item);
    }

    return channels;
}

GlobalMuteSoloToggle::Channels TrackListModel::globalMuteSoloChannels() const
{
    //! NOTE: not the metronome, like the Mixer's: its "mute" is the metronome being off (see
    //! PlaybackController::setTrackSoloMuteState()), so the global Mute would turn it on
    GlobalMuteSoloToggle::Channels channels = this->channels();
    if (m_videoChannelItem && m_hasVideo && videoVisible()) {
        channels.push_front(m_videoChannelItem);
    }

    return channels;
}

bool TrackListModel::globalMuteEngaged() const
{
    return GlobalMuteSoloToggle::muteEngaged(globalMuteSoloChannels());
}

bool TrackListModel::globalSoloEngaged() const
{
    return GlobalMuteSoloToggle::soloEngaged(globalMuteSoloChannels());
}

void TrackListModel::toggleGlobalMute()
{
    m_globalMuteSolo.toggleMute(globalMuteSoloChannels());
    emit globalMuteEngagedChanged();
}

void TrackListModel::toggleGlobalSolo()
{
    m_globalMuteSolo.toggleSolo(globalMuteSoloChannels());
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
    case IsVstInstrumentRole: return row.isVstInstrument;
    case ArticulationMapEditorOpenedRole: return row.articulationMapEditorOpened;
    case InstrumentEditorOpenedRole: return row.instrumentEditorOpened;
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
        { HasArticulationMapRole, "hasArticulationMap" },
        { IsVstInstrumentRole, "isVstInstrument" },
        { ArticulationMapEditorOpenedRole, "articulationMapEditorOpened" },
        { InstrumentEditorOpenedRole, "instrumentEditorOpened" }
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

IProjectVideoSettingsPtr TrackListModel::videoSettings() const
{
    const INotationProjectPtr project = context()->currentProject();
    return project ? project->videoSettings() : nullptr;
}

IProjectUndoStackPtr TrackListModel::projectUndoStack() const
{
    const INotationProjectPtr project = context()->currentProject();
    return project ? project->undoStack() : nullptr;
}
