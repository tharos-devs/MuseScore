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
#include <QUuid>

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
#include "project/iprojectaudiosyncsettings.h"
#include "project/iprojectvideosettings.h"

#include "playback/internal/referenceaudiofile.h"

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
static const muse::Settings::Key STRIP_CONTENT_HEIGHT_KEY("playback", "playback/trackList/stripContentHeight");
//! NOTE: about the strip's controls, until a channel is shown once
static constexpr int DEFAULT_STRIP_CONTENT_HEIGHT = 330;

TrackListModel::TrackListModel(QObject* parent)
    : QAbstractListModel(parent), muse::Contextable(muse::iocCtxForQmlObject(this))
{
    muse::settings()->setDefaultValue(STRIP_HEIGHT_KEY, muse::Val(FULL_STRIP_HEIGHT));
    muse::settings()->setDefaultValue(STRIP_CONTENT_HEIGHT_KEY, muse::Val(DEFAULT_STRIP_CONTENT_HEIGHT));
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
    for (const SoundTrackRow& row : m_soundTrackRows) {
        if (row.item) {
            row.item->setParent(nullptr);
            items.push_back(row.item);
        }
    }
    if (m_metronomeChannelItem) {
        m_metronomeChannelItem->setParent(nullptr);
        items.push_back(m_metronomeChannelItem);
    }

    QMetaObject::invokeMethod(qApp, [items]() {
        for (MixerChannelItem* item : items) {
            item->deleteLater();
        }
    }, Qt::QueuedConnection);
}

void TrackListModel::componentComplete()
{
    m_auxBusEditor = AuxBusEditor::shared(iocContext());

    for (SoundTrackChannel& channel : makeSoundTrackChannels(iocContext())) {
        m_soundTrackRows.push_back({ std::move(channel), nullptr, false, std::nullopt });
    }

    controller()->playbackInitedChanged().onReceive(this, [this](bool) {
        scheduleReload();
    });

    //! NOTE: a sound track comes and goes with its source (and once its audio is decoded)
    auto rebuildChangedSoundTrackItems = [this]() {
        for (SoundTrackRow& row : m_soundTrackRows) {
            if (row.item && row.item->trackId() != row.channel.trackId()) {
                rebuildSoundTrackChannelItem(row);
            }
        }
    };

    controller()->trackAdded().onReceive(this, [this, rebuildChangedSoundTrackItems](const TrackId) {
        scheduleReload();
        rebuildChangedSoundTrackItems();
    });

    controller()->trackRemoved().onReceive(this, [this, rebuildChangedSoundTrackItems](const TrackId) {
        scheduleReload();
        rebuildChangedSoundTrackItems();
    });

    for (const SoundTrackRow& row : m_soundTrackRows) {
        row.channel.muteStateChanged().onReceive(this, [this, type = row.channel.type](bool, bool) {
            if (const SoundTrackRow* soundTrackRow = this->soundTrackRow(type); soundTrackRow && soundTrackRow->item) {
                soundTrackRow->channel.loadMuteState(soundTrackRow->item);
            }
        });
    }

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

    for (SoundTrackRow& row : m_soundTrackRows) {
        if (row.settingsChanged) {
            row.settingsChanged->disconnect(this);
            row.settingsChanged.reset();
        }
    }

    m_notation = currentNotation();
    m_masterNotation = nullptr;
    m_audioSettings = audioSettings();
    m_articulationMaps = m_notation ? m_notation->masterNotation()->articulationMaps() : nullptr;

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
            requestFirstSelectedRowShown();
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

    connectSoundTrackSettings();

    for (SoundTrackRow& row : m_soundTrackRows) {
        row.hasSource = row.channel.hasSource();
        rebuildSoundTrackChannelItem(row);
    }
    emit soundTrackSourcesChanged();

    if (m_audioSettings) {
        //! NOTE: a color or a bus's name changed from the Mixer, the Timeline or here
        m_audioSettings->settingsChanged().onNotify(this, [this]() {
            updateColors();
            updateAuxBusTitles();
            loadFolders();
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

    const INotationProjectPtr project = context()->project();
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

bool TrackListModel::referenceAudioVisible() const
{
    return isCategoryVisible(ChannelCategory::ReferenceAudio);
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

        const bool wasSelected = std::any_of(selectedTracks.cbegin(), selectedTracks.cend(), [&partTracks](const PartTracks& tracks) {
            return isSameChannel(tracks, partTracks);
        });
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
    m_folders = m_audioSettings ? m_audioSettings->trackListFolders() : TrackListFolders();
    rebuildDisplayRows();

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

    //! NOTE: the folders' Mute/Solo show their tracks'
    connect(item, &MixerChannelItem::mutedChanged, this, &TrackListModel::notifyFolderRowsChanged);
    connect(item, &MixerChannelItem::soloChanged, this, &TrackListModel::notifyFolderRowsChanged);
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
            const int displayRow = displayRowOfChannel(i);
            if (displayRow >= 0) {
                const QModelIndex modelIndex = index(displayRow);
                emit dataChanged(modelIndex, modelIndex, { PartVisibleRole, HasArticulationMapRole, IsVstInstrumentRole,
                                                           ArticulationMapEditorOpenedRole, InstrumentEditorOpenedRole });
            }
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

    if (m_metronomeChannelItem && settings->trackHasExistingOutputParams(m_metronomeChannelItem->instrumentTrackId())) {
        m_metronomeChannelItem->setColor(settings->trackOutputParams(m_metronomeChannelItem->instrumentTrackId()).color);
    }

    loadSoundTrackColors();
}

const TrackListModel::SoundTrackRow* TrackListModel::soundTrackRow(MixerChannelItem::Type type) const
{
    for (const SoundTrackRow& row : m_soundTrackRows) {
        if (row.channel.type == type) {
            return &row;
        }
    }

    return nullptr;
}

MixerChannelItem* TrackListModel::videoChannelItem() const
{
    const SoundTrackRow* row = soundTrackRow(MixerChannelItem::Type::Video);
    return row ? row->item : nullptr;
}

bool TrackListModel::hasVideo() const
{
    const SoundTrackRow* row = soundTrackRow(MixerChannelItem::Type::Video);
    return row && row->hasSource;
}

MixerChannelItem* TrackListModel::referenceAudioChannelItem() const
{
    const SoundTrackRow* row = soundTrackRow(MixerChannelItem::Type::ReferenceAudio);
    return row ? row->item : nullptr;
}

bool TrackListModel::hasReferenceAudio() const
{
    const SoundTrackRow* row = soundTrackRow(MixerChannelItem::Type::ReferenceAudio);
    return row && row->hasSource;
}

//! NOTE: a source loaded/removed, its mute/solo changed (here, in the Mixer, or in its own UI: the Video panel and
//! the Timeline, the Audio sync window)
void TrackListModel::connectSoundTrackSettings()
{
    for (SoundTrackRow& row : m_soundTrackRows) {
        row.settingsChanged = row.channel.settingsChanged();
        if (!row.settingsChanged) {
            continue;
        }

        row.settingsChanged->onNotify(this, [this, type = row.channel.type]() {
            updateSoundTrackSources();
            if (const SoundTrackRow* soundTrackRow = this->soundTrackRow(type); soundTrackRow && soundTrackRow->item) {
                soundTrackRow->channel.loadMuteState(soundTrackRow->item);
            }
        });
    }
}

void TrackListModel::updateSoundTrackSources()
{
    bool changed = false;
    for (SoundTrackRow& row : m_soundTrackRows) {
        const bool hasSource = row.channel.hasSource();
        if (row.hasSource != hasSource) {
            row.hasSource = hasSource;
            changed = true;
        }
    }

    if (changed) {
        emit soundTrackSourcesChanged();
        emit globalMuteEngagedChanged();
        emit globalSoloEngagedChanged();
    }
}

//! NOTE: like the Mixer's sound track channel (see MixerPanelModel::buildSoundTrackChannelItem()), for its mute,
//! solo, color and level only: its mute/solo go to its source's settings, which PlaybackController applies to the
//! engine
void TrackListModel::rebuildSoundTrackChannelItem(SoundTrackRow& row)
{
    MixerChannelItem* oldItem = row.item;

    const TrackId trackId = row.channel.trackId();
    MixerChannelItem* item = new MixerChannelItem(this, row.channel.type, true /*outputOnly*/, trackId);
    row.item = item;
    item->setTitle(row.channel.title());
    connectGlobalMuteSolo(item);
    if (trackId != INVALID_TRACK_ID) {
        item->subscribeOnTrackAudioSignalChanges();
    }

    //! NOTE: the effective mute includes the live force-mute (another track soloed), like the Mixer's
    row.channel.loadMuteState(item);
    if (audioSettings()) {
        item->setColor(row.channel.outputParams().color);
    }

    connect(item, &MixerChannelItem::soloMuteStateChanged, this,
            [channel = row.channel](const INotationSoloMuteState::SoloMuteState& state) {
        channel.setUserSoloMuteState(state);
    });

    emit soundTrackChannelItemsChanged();

    //! NOTE: only once the view has released it
    if (oldItem) {
        oldItem->disconnect(this);
        oldItem->deleteLater();
    }
}

void TrackListModel::loadSoundTrackColors()
{
    if (!audioSettings()) {
        return;
    }

    for (const SoundTrackRow& row : m_soundTrackRows) {
        if (row.item) {
            row.item->setColor(row.channel.outputParams().color);
        }
    }
}

void TrackListModel::setSoundTrackColor(MixerChannelItem* item, const QColor& color)
{
    if (const SoundTrackRow* row = item ? soundTrackRow(item->type()) : nullptr) {
        row->channel.setColor(color);
        loadSoundTrackColors();
    }
}

void TrackListModel::resetSoundTrackColor(MixerChannelItem* item)
{
    setSoundTrackColor(item, QColor());
}

void TrackListModel::setMetronomeColor(const QColor& color)
{
    applyMetronomeColor(color, TranslatableString("undoableAction", "Change Mixer channel color"));
}

void TrackListModel::resetMetronomeColor()
{
    applyMetronomeColor(QColor(), TranslatableString("undoableAction", "Reset Mixer channel color"));
}

void TrackListModel::applyMetronomeColor(const QColor& color, const TranslatableString& actionName)
{
    if (!m_metronomeChannelItem) {
        return;
    }

    ChannelColorChange::apply(audioSettings(), projectUndoStack(), { { m_metronomeChannelItem->instrumentTrackId(), std::nullopt } },
                              color, actionName);
}

void TrackListModel::chooseVideoFile()
{
    const INotationProjectPtr project = context()->project();
    const IProjectVideoSettingsPtr settings = project ? project->videoSettings() : nullptr;
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

void TrackListModel::chooseReferenceAudioFile()
{
    if (const INotationProjectPtr project = context()->project()) {
        audiosync::chooseReferenceAudio(interactive().get(), playbackConfiguration().get(), controller().get(), project);
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

void TrackListModel::setTrackColor(int row, const QColor& color)
{
    applyTrackColor(row, color, TranslatableString("undoableAction", "Change Mixer channel color"));
}

void TrackListModel::resetTrackColor(int row)
{
    applyTrackColor(row, QColor(), TranslatableString("undoableAction", "Reset Mixer channel color"));
}

//! NOTE: the whole part: its other instruments' tracks too
void TrackListModel::applyTrackColor(int displayRow, const QColor& color, const TranslatableString& actionName)
{
    const int row = channelRowAt(displayRow);
    if (row < 0) {
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

void TrackListModel::selectRow(int displayRow, bool toggle, bool range)
{
    const int row = channelRowAt(displayRow);
    if (row < 0) {
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

//! NOTE: the same track (its part's) or bus, whatever its row's channel item
bool TrackListModel::isSameChannel(const PartTracks& tracks1, const PartTracks& tracks2)
{
    if (tracks1.auxIndex || tracks2.auxIndex) {
        return tracks1.auxIndex == tracks2.auxIndex;
    }

    return tracks1.instrumentTrackId == tracks2.instrumentTrackId;
}

//! NOTE: the first selected row's channel; with none selected, the one shown last (found again in the rebuilt rows),
//! or the first instrument track's: the strip is never empty while there's a channel
void TrackListModel::updateStripChannelItem()
{
    const Row* shown = nullptr;
    for (const Row& row : std::as_const(m_rows)) {
        if (row.item->selected()) {
            shown = &row;
            break;
        }
    }

    for (const Row& row : std::as_const(m_rows)) {
        if (shown) {
            break;
        }
        if (m_stripTracks && isSameChannel(row.tracks, *m_stripTracks)) {
            shown = &row;
        }
    }

    for (const Row& row : std::as_const(m_rows)) {
        if (shown) {
            break;
        }
        if (!row.tracks.auxIndex) {
            shown = &row;
        }
    }

    if (!shown && !m_rows.isEmpty()) {
        shown = &m_rows.front();
    }

    MixerChannelItem* item = shown ? shown->item : nullptr;
    if (shown) {
        m_stripTracks = shown->tracks;
    }

    const bool selected = item && item->selected();
    if (m_stripChannelItem != item || m_stripChannelSelected != selected) {
        if (item && m_stripChannelItem != item) {
            item->showAllSlots();
        }
        m_stripChannelItem = item;
        m_stripChannelSelected = selected;
        emit stripChannelItemChanged();
    }
}

bool TrackListModel::stripChannelSelected() const
{
    return m_stripChannelSelected;
}

//! NOTE: the first selected track or bus's row, or its collapsed folder's
void TrackListModel::requestFirstSelectedRowShown()
{
    for (int i = 0; i < m_rows.size(); ++i) {
        if (!m_rows.at(i).item->selected()) {
            continue;
        }

        const int folder = folderOfChannel(i);
        for (size_t row = 0; row < m_displayRows.size(); ++row) {
            const DisplayRow& displayRow = m_displayRows.at(row);
            if (displayRow.channelRow == i || (folder >= 0 && displayRow.folder == folder && m_folders.at(folder).collapsed)) {
                emit rowShowRequested(static_cast<int>(row));
                return;
            }
        }
        return;
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

int TrackListModel::stripContentHeight() const
{
    return muse::settings()->value(STRIP_CONTENT_HEIGHT_KEY).toInt();
}

void TrackListModel::setStripContentHeight(int height)
{
    if (height <= 0 || height == stripContentHeight()) {
        return;
    }

    muse::settings()->setSharedValue(STRIP_CONTENT_HEIGHT_KEY, muse::Val(height));
    emit stripContentHeightChanged();
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

//! NOTE: the listed channels: the selection's, and the global Mute/Solo's (with the shown sound tracks' rows)
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
    for (auto it = m_soundTrackRows.crbegin(); it != m_soundTrackRows.crend(); ++it) {
        if (it->item && it->hasSource && isCategoryVisible(it->channel.category)) {
            channels.push_front(it->item);
        }
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
    if (!index.isValid() || index.row() >= static_cast<int>(m_displayRows.size())) {
        return QVariant();
    }

    const DisplayRow& displayRow = m_displayRows.at(index.row());

    if (displayRow.folder >= 0) {
        const TrackListFolder& folder = m_folders.at(displayRow.folder);
        switch (role) {
        case ChannelItemRole: return QVariant::fromValue<MixerChannelItem*>(nullptr);
        case IsFolderRole: return true;
        case FolderNameRole: return folder.name.toQString();
        case FolderCollapsedRole: return folder.collapsed;
        case FolderMutedRole: return isFolderMuted(displayRow.folder);
        case FolderSoloedRole: return isFolderSoloed(displayRow.folder);
        case InFolderRole: return false;
        case PartVisibleRole: return true;
        default: return QVariant();
        }
    }

    const Row& row = m_rows.at(displayRow.channelRow);

    switch (role) {
    case IsFolderRole: return false;
    case FolderNameRole: return QString();
    case FolderCollapsedRole: return false;
    case FolderMutedRole: return false;
    case FolderSoloedRole: return false;
    case InFolderRole: return displayRow.inFolder;
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
    return static_cast<int>(m_displayRows.size());
}

QHash<int, QByteArray> TrackListModel::roleNames() const
{
    static const QHash<int, QByteArray> roles {
        { ChannelItemRole, "channelItem" },
        { PartVisibleRole, "partVisible" },
        { HasArticulationMapRole, "hasArticulationMap" },
        { IsVstInstrumentRole, "isVstInstrument" },
        { ArticulationMapEditorOpenedRole, "articulationMapEditorOpened" },
        { InstrumentEditorOpenedRole, "instrumentEditorOpened" },
        { IsFolderRole, "isFolder" },
        { FolderNameRole, "folderName" },
        { FolderCollapsedRole, "folderCollapsed" },
        { FolderMutedRole, "folderMuted" },
        { FolderSoloedRole, "folderSoloed" },
        { InFolderRole, "inFolder" }
    };

    return roles;
}

// ---- folders

String TrackListModel::channelKey(const PartTracks& tracks)
{
    if (tracks.auxIndex) {
        return u"aux:" + String::number(static_cast<int>(*tracks.auxIndex));
    }

    return u"part:" + String::fromStdString(std::to_string(tracks.instrumentTrackId.partId.toUint64()));
}

//! NOTE: the rows outside folders in the score's order (then the buses), each folder before the row it's anchored to
//! (at the end when that one isn't there or is in a folder itself), followed by its tracks unless it's collapsed
void TrackListModel::rebuildDisplayRows()
{
    m_displayRows.clear();

    std::vector<bool> emitted(m_folders.size(), false);
    auto emitFolder = [this, &emitted](int folder) {
        emitted[folder] = true;
        m_displayRows.push_back({ -1, folder, false });
        if (m_folders.at(folder).collapsed) {
            return;
        }
        for (int i = 0; i < m_rows.size(); ++i) {
            if (folderOfChannel(i) == folder) {
                m_displayRows.push_back({ i, -1, true });
            }
        }
    };

    for (int i = 0; i < m_rows.size(); ++i) {
        if (folderOfChannel(i) >= 0) {
            continue;
        }

        const String key = channelKey(m_rows.at(i).tracks);
        for (size_t folder = 0; folder < m_folders.size(); ++folder) {
            if (!emitted[folder] && m_folders.at(folder).before == key) {
                emitFolder(static_cast<int>(folder));
            }
        }

        m_displayRows.push_back({ i, -1, false });
    }

    for (size_t folder = 0; folder < m_folders.size(); ++folder) {
        if (!emitted[folder]) {
            emitFolder(static_cast<int>(folder));
        }
    }
}

void TrackListModel::loadFolders()
{
    const TrackListFolders folders = m_audioSettings ? m_audioSettings->trackListFolders() : TrackListFolders();
    if (folders == m_folders) {
        return;
    }

    beginResetModel();
    m_folders = folders;
    rebuildDisplayRows();
    endResetModel();

    emit rowCountChanged();
}

void TrackListModel::applyFolders(const TrackListFolders& folders, const TranslatableString& actionName)
{
    const IProjectAudioSettingsPtr settings = m_audioSettings;
    if (!settings) {
        return;
    }

    const TrackListFolders before = settings->trackListFolders();
    if (before == folders) {
        return;
    }

    settings->setTrackListFolders(folders);

    if (const IProjectUndoStackPtr undoStack = projectUndoStack()) {
        undoStack->push(actionName,
                        [settings, folders]() { settings->setTrackListFolders(folders); },
                        [settings, before]() { settings->setTrackListFolders(before); });
    }
}

int TrackListModel::channelRowAt(int row) const
{
    return row >= 0 && row < static_cast<int>(m_displayRows.size()) ? m_displayRows.at(row).channelRow : -1;
}

int TrackListModel::folderAt(int row) const
{
    return row >= 0 && row < static_cast<int>(m_displayRows.size()) ? m_displayRows.at(row).folder : -1;
}

int TrackListModel::displayRowOfChannel(int channelRow) const
{
    for (size_t i = 0; i < m_displayRows.size(); ++i) {
        if (m_displayRows.at(i).channelRow == channelRow) {
            return static_cast<int>(i);
        }
    }

    return -1;
}

int TrackListModel::folderOfChannel(int channelRow) const
{
    const String key = channelKey(m_rows.at(channelRow).tracks);
    for (size_t folder = 0; folder < m_folders.size(); ++folder) {
        if (muse::contains(m_folders.at(folder).members, key)) {
            return static_cast<int>(folder);
        }
    }

    return -1;
}

//! NOTE: a new folder, with its own id
static TrackListFolder newFolder(const String& name)
{
    TrackListFolder folder;
    folder.id = String::fromQString(QUuid::createUuid().toString(QUuid::WithoutBraces));
    folder.name = name;
    return folder;
}

//! NOTE: "Folder 1", "Folder 2"...: the first number not taken
static String nextFolderName(const TrackListFolders& folders)
{
    auto isTaken = [&folders](const String& name) {
        return std::any_of(folders.cbegin(), folders.cend(), [&name](const TrackListFolder& folder) {
            return folder.name == name;
        });
    };

    int number = 1;
    while (isTaken(muse::mtrc("playback", "Folder %1").arg(number))) {
        ++number;
    }

    return muse::mtrc("playback", "Folder %1").arg(number);
}

//! NOTE: above the first selected track or bus, or first
void TrackListModel::addFolder()
{
    for (int i = 0; i < m_rows.size(); ++i) {
        if (!m_rows.at(i).item->selected()) {
            continue;
        }

        //! NOTE: not shown in a collapsed folder: above that folder
        const int folder = folderOfChannel(i);
        for (size_t row = 0; row < m_displayRows.size(); ++row) {
            const DisplayRow& displayRow = m_displayRows.at(row);
            if (displayRow.channelRow == i || (folder >= 0 && displayRow.folder == folder)) {
                addFolderAbove(static_cast<int>(row));
                return;
            }
        }
    }

    addFolderAbove(0);
}

//! NOTE: above its folder when it's in one
void TrackListModel::addFolderAbove(int row)
{
    int beforeRow = std::max(0, row);
    const int channelRow = channelRowAt(row);
    const int containingFolder = channelRow >= 0 ? folderOfChannel(channelRow) : -1;
    if (containingFolder >= 0) {
        for (size_t i = 0; i < m_displayRows.size(); ++i) {
            if (m_displayRows.at(i).folder == containingFolder) {
                beforeRow = static_cast<int>(i);
                break;
            }
        }
    }

    TrackListFolders folders = m_folders;
    insertFolderBefore(folders, newFolder(nextFolderName(m_folders)), beforeRow);
    applyFolders(folders, TranslatableString("undoableAction", "Add Track list folder"));
}

//! NOTE: before the top-level row beforeRow (as shown now; the end: rowCount()). Before a folder: the same anchor, just
//! before it among the folders sharing it; before a track or bus: after the folders already before it
void TrackListModel::insertFolderBefore(TrackListFolders& folders, TrackListFolder folder, int beforeRow) const
{
    const DisplayRow* target = beforeRow >= 0 && beforeRow < static_cast<int>(m_displayRows.size())
                               ? &m_displayRows.at(beforeRow) : nullptr;

    if (target && target->folder >= 0) {
        const TrackListFolder& other = m_folders.at(target->folder);
        folder.before = other.before;
        folders.insert(std::find_if(folders.begin(), folders.end(), [&other](const TrackListFolder& f) {
            return f.id == other.id;
        }), folder);
        return;
    }

    folder.before = target ? channelKey(m_rows.at(target->channelRow).tracks) : String();
    folders.push_back(folder);
}

void TrackListModel::renameFolder(int row, const QString& name)
{
    const int folder = folderAt(row);
    const String newName = String::fromQString(name.simplified());
    if (folder < 0 || newName.empty() || newName == m_folders.at(folder).name) {
        return;
    }

    TrackListFolders folders = m_folders;
    folders[folder].name = newName;
    applyFolders(folders, TranslatableString("undoableAction", "Rename Track list folder"));
}

//! NOTE: its tracks stay, out of any folder
void TrackListModel::removeFolder(int row)
{
    const int folder = folderAt(row);
    if (folder < 0) {
        return;
    }

    TrackListFolders folders = m_folders;
    folders.erase(folders.begin() + folder);
    applyFolders(folders, TranslatableString("undoableAction", "Remove Track list folder"));
}

//! NOTE: not an undo step (like a collapsed folder of the Timeline or the map editor)
void TrackListModel::toggleFolderCollapsed(int row)
{
    const int folder = folderAt(row);
    if (folder < 0 || !m_audioSettings) {
        return;
    }

    TrackListFolders folders = m_folders;
    folders[folder].collapsed = !folders[folder].collapsed;
    m_audioSettings->setTrackListFolders(folders);
}

//! NOTE: the row's channel, or the selection's when it's selected
std::vector<int> TrackListModel::channelRowsToMove(int channelRow) const
{
    std::vector<int> result;
    if (m_rows.at(channelRow).item->selected()) {
        for (int i = 0; i < m_rows.size(); ++i) {
            if (m_rows.at(i).item->selected()) {
                result.push_back(i);
            }
        }
    } else {
        result.push_back(channelRow);
    }

    return result;
}

void TrackListModel::moveRowToFolder(int row, int folderRow)
{
    const int channelRow = channelRowAt(row);
    const int targetFolder = folderRow < 0 ? -1 : folderAt(folderRow);
    if (channelRow < 0 || (folderRow >= 0 && targetFolder < 0)) {
        return;
    }

    applyFolders(foldersWithChannelsMoved(m_folders, channelRowsToMove(channelRow), targetFolder),
                 targetFolder >= 0 ? TranslatableString("undoableAction", "Move to Track list folder")
                 : TranslatableString("undoableAction", "Remove from Track list folder"));
}

//! NOTE: a new folder where the first of them is, with them
void TrackListModel::moveRowToNewFolder(int row)
{
    const int channelRow = channelRowAt(row);
    if (channelRow < 0) {
        return;
    }

    const std::vector<int> moved = channelRowsToMove(channelRow);

    TrackListFolders folders = m_folders;
    TrackListFolder folder = newFolder(nextFolderName(folders));
    //! NOTE: re-anchored to the next row outside folders once they're in it (see foldersWithChannelsMoved())
    folder.before = folderOfChannel(moved.front()) < 0 ? channelKey(m_rows.at(moved.front()).tracks) : String();
    folders.push_back(folder);

    applyFolders(foldersWithChannelsMoved(folders, moved, static_cast<int>(folders.size()) - 1),
                 TranslatableString("undoableAction", "Move to Track list folder"));
}

//! NOTE: targetFolder -1: out of any folder
TrackListFolders TrackListModel::foldersWithChannelsMoved(TrackListFolders folders, const std::vector<int>& moved,
                                                          int targetFolder) const
{
    std::vector<String> movedKeys;
    for (int i : moved) {
        const String key = channelKey(m_rows.at(i).tracks);
        movedKeys.push_back(key);
        for (TrackListFolder& folder : folders) {
            muse::remove(folder.members, key);
        }
        if (targetFolder >= 0) {
            folders[targetFolder].members.push_back(key);
        }
    }

    //! NOTE: a folder anchored to a track now in a folder stays where it was: before the next row outside folders
    if (targetFolder >= 0) {
        auto inAFolder = [&folders](const String& key) {
            return std::any_of(folders.cbegin(), folders.cend(), [&key](const TrackListFolder& folder) {
                return muse::contains(folder.members, key);
            });
        };

        for (TrackListFolder& folder : folders) {
            if (!muse::contains(movedKeys, folder.before)) {
                continue;
            }

            int anchorRow = -1;
            for (int i = 0; i < m_rows.size(); ++i) {
                if (channelKey(m_rows.at(i).tracks) == folder.before) {
                    anchorRow = i;
                    break;
                }
            }

            folder.before = String();
            for (int i = anchorRow + 1; anchorRow >= 0 && i < m_rows.size(); ++i) {
                const String key = channelKey(m_rows.at(i).tracks);
                if (!inAFolder(key)) {
                    folder.before = key;
                    break;
                }
            }
        }
    }

    return folders;
}

bool TrackListModel::canMoveFolderBefore(int row, int beforeRow) const
{
    const int folder = folderAt(row);
    if (folder < 0 || beforeRow < 0 || beforeRow > static_cast<int>(m_displayRows.size())) {
        return false;
    }

    if (beforeRow == static_cast<int>(m_displayRows.size())) {
        return true;
    }

    const DisplayRow& target = m_displayRows.at(beforeRow);
    return !target.inFolder && target.folder != folder;
}

void TrackListModel::moveFolder(int row, int beforeRow)
{
    if (!canMoveFolderBefore(row, beforeRow)) {
        return;
    }

    const int folder = folderAt(row);
    TrackListFolders folders = m_folders;
    const TrackListFolder moved = folders.at(folder);
    folders.erase(folders.begin() + folder);

    insertFolderBefore(folders, moved, beforeRow);
    applyFolders(folders, TranslatableString("undoableAction", "Move Track list folder"));
}

GlobalMuteSoloToggle::Channels TrackListModel::folderChannels(int folder) const
{
    GlobalMuteSoloToggle::Channels result;
    for (int i = 0; i < m_rows.size(); ++i) {
        if (folderOfChannel(i) == folder) {
            result.push_back(m_rows.at(i).item);
        }
    }

    return result;
}

//! NOTE: like the rows' Solo buttons: not an Aux FX bus's
bool TrackListModel::canFolderSolo(const MixerChannelItem* item)
{
    return item->type() != MixerChannelItem::Type::Aux || item->isGroupBus();
}

bool TrackListModel::isFolderMuted(int folder) const
{
    return GlobalMuteSoloToggle::muteEngaged(folderChannels(folder));
}

bool TrackListModel::isFolderSoloed(int folder) const
{
    return GlobalMuteSoloToggle::soloEngaged(folderChannels(folder));
}

void TrackListModel::notifyFolderRowsChanged()
{
    for (size_t row = 0; row < m_displayRows.size(); ++row) {
        if (m_displayRows.at(row).folder >= 0) {
            const QModelIndex modelIndex = index(static_cast<int>(row));
            emit dataChanged(modelIndex, modelIndex, { FolderMutedRole, FolderSoloedRole });
        }
    }
}

void TrackListModel::toggleFolderMute(int row)
{
    const int folder = folderAt(row);
    if (folder < 0) {
        return;
    }

    const bool muted = isFolderMuted(folder);
    for (MixerChannelItem* item : folderChannels(folder)) {
        //! NOTE: muted by another track's solo: left alone (see ChannelSelection::setMuted())
        if (muted ? !item->forceMute() : !(item->muted() && item->forceMute())) {
            item->setMuted(!muted);
        }
    }
}

void TrackListModel::toggleFolderSolo(int row)
{
    const int folder = folderAt(row);
    if (folder < 0) {
        return;
    }

    const bool soloed = isFolderSoloed(folder);
    for (MixerChannelItem* item : folderChannels(folder)) {
        if (canFolderSolo(item)) {
            item->setUserSolo(!soloed);
        }
    }
}

static const QString ADD_FOLDER_ITEM_ID("addNewFolder");
static const QString MOVE_TO_FOLDER_ITEM_PREFIX("moveToFolder:");
static const QString NEW_FOLDER_ITEM_ID("moveToNewFolder");
static const QString ADD_FX_CHANNEL_ITEM_ID("addFxChannel");
static const QString ADD_GROUP_CHANNEL_ITEM_ID("addGroupChannel");
static const QString ADD_FX_CHANNEL_FOR_SELECTED_ITEM_ID("addFxChannelForSelectedTracks");
static const QString ADD_GROUP_CHANNEL_FOR_SELECTED_ITEM_ID("addGroupChannelForSelectedTracks");
static const QString DELETE_CHANNEL_ITEM_ID("deleteChannel");

//! NOTE: like the Mixer's channel menus: the "for selected tracks" ones on an instrument track only, "Delete channel" on a
//! bus (not the Reverb one, see IPlaybackController::removeAuxBus)
QVariantList TrackListModel::auxBusMenuItems(int row) const
{
    const int channelRow = channelRowAt(row);
    const MixerChannelItem* item = channelRow >= 0 ? m_rows.at(channelRow).item : nullptr;
    if (!item || !m_auxBusEditor) {
        return {};
    }

    const bool isInstrument = item->type() == MixerChannelItem::Type::PrimaryInstrument
                              || item->type() == MixerChannelItem::Type::SecondaryInstrument;
    if (!isInstrument && item->type() != MixerChannelItem::Type::Aux) {
        return {};
    }

    const bool canAdd = m_auxBusEditor->canAddAuxBus();
    QVariantList items {
        QVariantMap { { "id", ADD_FX_CHANNEL_ITEM_ID }, { "title", muse::qtrc("playback", "Add FX channel") }, { "enabled", canAdd } },
        QVariantMap { { "id", ADD_GROUP_CHANNEL_ITEM_ID }, { "title", muse::qtrc("playback", "Add Group channel") },
            { "enabled", canAdd } },
    };

    if (isInstrument) {
        items << QVariantMap { { "id", "" }, { "title", "" } }; // separator
        items << QVariantMap { { "id", ADD_FX_CHANNEL_FOR_SELECTED_ITEM_ID },
            { "title", muse::qtrc("playback", "Add FX channel for selected tracks") }, { "enabled", canAdd } };
        items << QVariantMap { { "id", ADD_GROUP_CHANNEL_FOR_SELECTED_ITEM_ID },
            { "title", muse::qtrc("playback", "Add Group channel for selected tracks") }, { "enabled", canAdd } };
    } else {
        items << QVariantMap { { "id", "" }, { "title", "" } }; // separator
        items << QVariantMap { { "id", DELETE_CHANNEL_ITEM_ID }, { "title", muse::qtrc("playback", "Delete channel") },
            { "enabled", !item->isReverbBus() } };
    }

    return items;
}

bool TrackListModel::handleAuxBusMenuItem(int row, const QString& itemId)
{
    if (!m_auxBusEditor) {
        return false;
    }

    if (itemId == ADD_FX_CHANNEL_ITEM_ID || itemId == ADD_GROUP_CHANNEL_ITEM_ID) {
        m_auxBusEditor->addAuxBus(itemId == ADD_GROUP_CHANNEL_ITEM_ID);
        return true;
    }

    if (itemId == ADD_FX_CHANNEL_FOR_SELECTED_ITEM_ID || itemId == ADD_GROUP_CHANNEL_FOR_SELECTED_ITEM_ID) {
        QList<TrackId> trackIds;
        for (const Row& row : m_rows) {
            const bool isInstrument = row.item->type() == MixerChannelItem::Type::PrimaryInstrument
                                      || row.item->type() == MixerChannelItem::Type::SecondaryInstrument;
            if (isInstrument && row.item->selected()) {
                trackIds.push_back(row.item->trackId());
            }
        }

        m_auxBusEditor->addAuxBusForTracks(itemId == ADD_GROUP_CHANNEL_FOR_SELECTED_ITEM_ID, trackIds);
        return true;
    }

    if (itemId == DELETE_CHANNEL_ITEM_ID) {
        const int channelRow = channelRowAt(row);
        const MixerChannelItem* item = channelRow >= 0 ? m_rows.at(channelRow).item : nullptr;
        if (item && item->type() == MixerChannelItem::Type::Aux) {
            m_auxBusEditor->deleteAuxBus(item->auxBusIndex());
        }
        return true;
    }

    return false;
}

QVariantList TrackListModel::folderMenuItems(int row) const
{
    return {
        QVariantMap { { "id", ADD_FOLDER_ITEM_ID }, { "title", muse::qtrc("playback", "Add new folder") } },
        moveToFolderMenu(row)
    };
}

QVariantMap TrackListModel::moveToFolderMenu(int row) const
{
    const int channelRow = channelRowAt(row);
    const int current = channelRow >= 0 ? folderOfChannel(channelRow) : -1;

    QVariantList items;
    for (size_t folder = 0; folder < m_folders.size(); ++folder) {
        items << QVariantMap {
            { "id", MOVE_TO_FOLDER_ITEM_PREFIX + QString::number(folder) },
            { "title", m_folders.at(folder).name.toQString() },
            { "checkable", true },
            { "checked", static_cast<int>(folder) == current }
            };
    }

    if (!items.isEmpty()) {
        items << QVariantMap { { "id", "" }, { "title", "" } }; // separator
    }

    items << QVariantMap { { "id", NEW_FOLDER_ITEM_ID }, { "title", muse::qtrc("playback", "New folder") } };

    if (current >= 0) {
        items << QVariantMap { { "id", MOVE_TO_FOLDER_ITEM_PREFIX + "-1" }, { "title", muse::qtrc("playback", "Remove from folder") } };
    }

    return QVariantMap {
        { "id", "moveToFolderMenu" },
        { "title", muse::qtrc("playback", "Move to folder") },
        { "subitems", items }
    };
}

bool TrackListModel::handleMoveToFolderMenuItem(int row, const QString& itemId)
{
    if (itemId == ADD_FOLDER_ITEM_ID) {
        addFolderAbove(row);
        return true;
    }

    if (itemId == NEW_FOLDER_ITEM_ID) {
        moveRowToNewFolder(row);
        return true;
    }

    if (!itemId.startsWith(MOVE_TO_FOLDER_ITEM_PREFIX)) {
        return false;
    }

    const int folder = itemId.mid(MOVE_TO_FOLDER_ITEM_PREFIX.size()).toInt();
    if (folder < 0) {
        moveRowToFolder(row, -1);
        return true;
    }

    for (size_t i = 0; i < m_displayRows.size(); ++i) {
        if (m_displayRows.at(i).folder == folder) {
            moveRowToFolder(row, static_cast<int>(i));
            break;
        }
    }

    return true;
}

INotationPtr TrackListModel::currentNotation() const
{
    return context()->currentNotation();
}

IProjectAudioSettingsPtr TrackListModel::audioSettings() const
{
    const INotationProjectPtr project = context()->project();
    return project ? project->audioSettings() : nullptr;
}

IProjectUndoStackPtr TrackListModel::projectUndoStack() const
{
    const INotationProjectPtr project = context()->project();
    return project ? project->undoStack() : nullptr;
}
