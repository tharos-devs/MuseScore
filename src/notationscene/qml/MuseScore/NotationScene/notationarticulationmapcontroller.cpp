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

#include "notationarticulationmapcontroller.h"

#include "articulationmapcolors.h"
#include "articulationmapoverlay.h"

#include <algorithm>
#include <cmath>
#include <limits>

#include <QActionGroup>
#include <QMenu>
#include <QPointer>

#include "async/async.h"
#include "global/containers.h"
#include "translation.h"

#include "engraving/dom/chord.h"
#include "engraving/dom/masterscore.h"
#include "engraving/dom/measure.h"
#include "engraving/dom/note.h"
#include "engraving/dom/part.h"
#include "engraving/dom/rest.h"
#include "engraving/dom/segment.h"
#include "engraving/dom/staff.h"
#include "engraving/dom/system.h"
#include "engraving/dom/utils.h"

#include "notation/imasternotation.h"
#include "notation/inotation.h"
#include "notation/inotationelements.h" // IWYU pragma: keep
#include "notation/inotationinteraction.h"
#include "notation/inotationplayback.h"
#include "notation/inotationselection.h"
#include "notation/inotationstyle.h"

#include "notationscene/notationcommands.h"

using namespace mu::notation;
using namespace mu::engraving;

constexpr static double ARTMAP_LANE_TOP_GAP_SP = 1.2; // below the staff's bounding box
constexpr static double ARTMAP_LANE_HEIGHT_SP = 2.4;
constexpr static double ARTMAP_SNAP_DISTANCE_PX = 5.0;

static QColor artMapEntryColor(const ExpressionMap& map, const muse::String& entryId)
{
    size_t index = 0;
    for (; index < map.entries.size(); ++index) {
        if (map.entries[index].id == entryId) {
            break;
        }
    }

    // An explicit color=#RRGGBB in the map wins; otherwise the palette, in map order
    if (index < map.entries.size() && map.entries[index].color) {
        return QColor::fromRgb(*map.entries[index].color);
    }

    return artMapPaletteColor(index);
}

static QString artMapLeafName(const muse::String& entryId)
{
    const QString id = entryId.toQString();
    const qsizetype pos = id.lastIndexOf('>');
    return (pos < 0 ? id : id.mid(pos + 1)).trimmed();
}

NotationArticulationMapController::NotationArticulationMapController(QQuickItem* overlaysParent,
                                                                     const muse::modularity::ContextPtr& iocCtx)
    : muse::Contextable(iocCtx), m_overlaysParent(overlaysParent)
{
}

void NotationArticulationMapController::init()
{
    IF_ASSERT_FAILED(articulationMaps() && currentNotation()) {
        return;
    }

    onCurrentNotationChanged();

    articulationMaps()->overlayEnabledChanged().onNotify(this, [this]() {
        if (articulationMaps()->isOverlayEnabled()) {
            rebuildAllOverlays();
        } else {
            updateOverlaysGeometry();
        }
    }, Asyncable::Mode::SetReplace);

    globalContext()->currentNotationChanged().onNotify(this, [this]() {
        onCurrentNotationChanged();
    }, Asyncable::Mode::SetReplace);
}

void NotationArticulationMapController::onCurrentNotationChanged()
{
    rebuildAllOverlays();

    if (Score* thisScore = score()) {
        // Also covers articulation map/mark edits and undo/redo: they go through the score's transactions
        thisScore->changesChannel().onReceive(this, [this, thisScore](const ScoreChanges&) {
            if (thisScore != score()) {
                return;
            }
            scheduleRebuild();
        }, Asyncable::Mode::SetReplace);
    }

    const INotationPtr notation = currentNotation();
    if (!notation) {
        return;
    }

    INotation* thisNotation = notation.get();

    notation->viewModeChanged().onNotify(this, [this, thisNotation]() {
        if (thisNotation != currentNotation().get()) {
            return;
        }
        scheduleRebuild();
    }, Asyncable::Mode::SetReplace);

    if (notation->style()) {
        notation->style()->styleChanged().onNotify(this, [this, thisNotation]() {
            if (thisNotation != currentNotation().get()) {
                return;
            }
            scheduleRebuild();
        }, Asyncable::Mode::SetReplace);
    }
}

void NotationArticulationMapController::scheduleRebuild()
{
    if (m_rebuildScheduled) {
        return;
    }
    m_rebuildScheduled = true;

    // The score may still be mid-layout when changesChannel fires, and playback only records the
    // resolved articulations once it has re-rendered - defer to the next event loop iteration
    muse::async::Async::call(this, [this]() {
        m_rebuildScheduled = false;
        if (articulationMaps() && articulationMaps()->isOverlayEnabled()) {
            rebuildAllOverlays();
        }
    });
}

void NotationArticulationMapController::rebuildAllOverlays()
{
    for (const auto& [key, data] : m_overlaysByStaff) {
        if (data.overlay->isDragging()) {
            // Rebuilding would delete the chip being held - catch up once the drag ends instead
            m_rebuildAfterDrag = true;
            return;
        }
    }

    m_rebuildAfterDrag = false;

    if (!score() || !articulationMaps() || !articulationMaps()->isOverlayEnabled()) {
        for (const auto& [key, data] : m_overlaysByStaff) {
            delete data.overlay;
        }
        m_overlaysByStaff.clear();
        return;
    }

    OverlaysMap newOverlays;

    for (const System* system : score()->systems()) {
        staff_idx_t staffIdx = system->firstVisibleStaff();
        while (staffIdx != muse::nidx) {
            createOverlayForStaff(system, staffIdx, newOverlays);
            staffIdx = system->nextVisibleStaff(staffIdx);
        }
    }

    for (const auto& [key, data] : m_overlaysByStaff) {
        delete data.overlay;
    }

    m_overlaysByStaff = std::move(newOverlays);

    updateOverlaysGeometry();
}

void NotationArticulationMapController::createOverlayForStaff(const System* system, staff_idx_t staffIdx, OverlaysMap& newOverlays)
{
    const Staff* staff = score()->staff(staffIdx);
    const SysStaff* sysStaff = system->staff(staffIdx);
    if (!staff || !sysStaff || !staff->isPrimaryStaff()) {
        return;
    }

    // Only an instrument's first staff drives its articulations (see appendArticulationMapEvent)
    const Part* part = staff->part();
    if (!part || part->staves().empty() || part->staves().front() != staff) {
        return;
    }

    const ArticulationMapDataConstPtr mapData = articulationMaps()->data();
    const IMasterNotationPtr masterNotation = globalContext()->currentMasterNotation();
    const INotationPlaybackPtr playback = masterNotation ? masterNotation->playback() : nullptr;
    if (!mapData || mapData->maps().empty() || !playback) {
        return;
    }

    const MasterScore* masterScore = score()->masterScore();

    StaffOverlayData data;
    const ExpressionMap* map = nullptr;

    const track_idx_t strack = staffIdx * VOICES;
    const track_idx_t etrack = strack + VOICES;

    const auto toMasterChordRest = [masterScore](ChordRest* chordRest) -> ChordRest* {
        if (chordRest->score() == masterScore) {
            return chordRest;
        }
        EngravingItem* linked = chordRest->findLinkedInScore(masterScore);
        return linked && linked->isChordRest() ? toChordRest(linked) : nullptr;
    };

    for (const Segment* seg = system->firstMeasure() ? system->firstMeasure()->first(SegmentType::ChordRest) : nullptr;
         seg && seg->system() == system; seg = seg->next1MM(SegmentType::ChordRest)) {
        bool chordFound = false;

        // One lane per staff: the first voice with a chord at this position speaks for it
        for (track_idx_t track = strack; track < etrack; ++track) {
            EngravingItem* item = seg->element(track);
            if (!item || !item->isChord()) {
                continue;
            }

            Chord* chord = toChord(item);
            ChordRest* masterChord = toMasterChordRest(chord);
            if (!masterChord || !masterChord->isChord()) {
                continue;
            }

            // Nothing resolved (a map without a default, before the first mark): still a note a mark can be
            // placed on, like a rest - it plays no articulation yet
            const std::optional<ResolvedArticulation> resolved = playback->resolvedArticulation(masterChord->track(),
                                                                                                masterChord->tick().ticks());

            if (!map) {
                data.trackId = makeInstrumentTrackId(masterChord);
                map = mapData->map(data.trackId);
                if (!map) {
                    return;
                }
            }

            ChordEntry entry;
            entry.chord = chord;
            entry.masterChord = masterChord;
            entry.canvasX = chord->canvasX();
            if (resolved) {
                entry.entryId = resolved->entryId;
                entry.source = resolved->source;
            }
            if (masterChord->eid().isValid()) {
                entry.mark = mapData->mark(masterChord->eid());
            }
            data.chords.push_back(std::move(entry));
            chordFound = true;
            break;
        }

        if (chordFound) {
            continue;
        }

        // Silent position: a mark can be placed on its (visible) rest, to change the articulation
        // ahead of the next chord - the first voice speaks for it
        for (track_idx_t track = strack; track < etrack; ++track) {
            EngravingItem* item = seg->element(track);
            if (!item || !item->isRest() || toRest(item)->isGap() || !item->visible()) {
                continue;
            }

            ChordRest* rest = toChordRest(item);
            ChordRest* masterRest = toMasterChordRest(rest);
            if (!masterRest) {
                continue;
            }

            // A staff with rests only (e.g. before its first note) can get marks too
            if (!map) {
                data.trackId = makeInstrumentTrackId(masterRest);
                map = mapData->map(data.trackId);
                if (!map) {
                    return;
                }
            }

            ChordEntry entry;
            entry.chord = rest;
            entry.masterChord = masterRest;
            entry.canvasX = rest->canvasX();
            if (masterRest->eid().isValid()) {
                entry.mark = mapData->mark(masterRest->eid());
            }
            if (entry.mark) {
                entry.entryId = entry.mark->entryId;
                entry.source = ResolvedArticulation::Source::OwnMark;
            }
            data.chords.push_back(std::move(entry));
            break;
        }
    }

    if (data.chords.empty() || !map) {
        return;
    }

    // A rest without a mark doesn't play anything: it shows the articulation in effect around it - the latest one
    // that stays in effect (not a "this note only" mark nor a score articulation, which only apply to their chord),
    // or at the start of the system the next one, if it's one that was already in effect
    using Source = ResolvedArticulation::Source;
    const auto staysInEffect = [](const ChordEntry& entry) {
        if (entry.entryId.empty()) {
            return false;
        }
        if (entry.chord->isRest()) {
            return true;
        }
        if (entry.source == Source::Alias) {
            return false;
        }
        return entry.source != Source::OwnMark || (entry.mark && entry.mark->scope == ArticulationMark::Scope::Latched);
    };

    std::optional<muse::String> inEffect;
    for (size_t i = 0; i < data.chords.size(); ++i) {
        ChordEntry& entry = data.chords[i];
        if (!entry.chord->isRest() || entry.mark) {
            if (staysInEffect(entry)) {
                inEffect = entry.entryId;
            }
            continue;
        }

        if (inEffect) {
            entry.entryId = *inEffect;
            entry.source = Source::Latched;
            continue;
        }

        for (size_t j = i + 1; j < data.chords.size(); ++j) {
            const ChordEntry& next = data.chords[j];
            if (next.chord->isRest() && !next.mark) {
                continue;
            }
            if (next.source == Source::Latched || next.source == Source::Default) {
                entry.entryId = next.entryId;
                entry.source = Source::Latched;
            }
            break;
        }
    }

    const double spatium = data.chords.front().chord->spatium();
    const muse::RectF staffCanvasRect = sysStaff->bbox().translated(system->canvasPos());
    data.bandRect = muse::RectF(staffCanvasRect.x(), staffCanvasRect.bottom() + ARTMAP_LANE_TOP_GAP_SP * spatium,
                                staffCanvasRect.width(), ARTMAP_LANE_HEIGHT_SP * spatium);

    const auto toN = [&data](double canvasX) {
        return (canvasX - data.bandRect.x()) / std::max(1.0, data.bandRect.width());
    };

    QVector<ArticulationMapOverlay::ChipData> chips;
    QVector<ArticulationMapOverlay::LineData> lines;
    muse::String previousEntryId;
    bool hasPrevious = false;

    for (size_t i = 0; i < data.chords.size(); ++i) {
        const ChordEntry& entry = data.chords[i];
        const QColor color = artMapEntryColor(*map, entry.entryId);

        // Nothing known to be in effect (rests only so far in this system): no line
        if (!entry.entryId.empty()) {
            const double nextX = i + 1 < data.chords.size() ? data.chords[i + 1].canvasX : data.bandRect.right();
            lines.push_back({ toN(entry.canvasX), toN(nextX), color });
        }

        // A rest without a mark only continues the line, a note with no articulation yet has nothing to show
        if ((entry.chord->isRest() && !entry.mark) || entry.entryId.empty()) {
            continue;
        }

        const bool isOwnMark = entry.source == Source::OwnMark;
        const bool isAlias = entry.source == Source::Alias;
        const bool changed = !hasPrevious || entry.entryId != previousEntryId;
        previousEntryId = entry.entryId;
        hasPrevious = true;

        // Consecutive notes playing the same articulation share one chip, the line shows it continues
        if (!isOwnMark && !changed) {
            continue;
        }

        ArticulationMapOverlay::ChipData chip;
        chip.chordXN = toN(entry.canvasX);
        chip.xN = chip.chordXN;
        if (isOwnMark && entry.mark && entry.mark->tickOffset != 0) {
            chip.xN = toN(entry.canvasX + entry.mark->tickOffset / ticksPerCanvasUnit(entry.chord));
        }
        chip.label = artMapLeafName(entry.entryId);
        chip.fullName = entry.entryId.toQString();
        chip.color = color;
        chip.kind = isOwnMark ? ArticulationMapOverlay::ChipData::Kind::Mark
                    : isAlias ? ArticulationMapOverlay::ChipData::Kind::Alias
                    : ArticulationMapOverlay::ChipData::Kind::Inherited;
        chip.singleChord = isOwnMark && entry.mark && entry.mark->scope == ArticulationMark::Scope::SingleChord;

        chips.push_back(chip);
        data.chipChords.push_back(i);
    }

    const SysStaffKey key { system, staffIdx };

    ArticulationMapOverlay* overlay = nullptr;
    const auto oldIt = m_overlaysByStaff.find(key);
    if (oldIt != m_overlaysByStaff.end()) {
        overlay = oldIt->second.overlay;
        m_overlaysByStaff.erase(oldIt);
    } else {
        overlay = new ArticulationMapOverlay(m_overlaysParent);
        overlay->setVisible(false);

        QObject::connect(overlay, &ArticulationMapOverlay::clicked, [this, key](qreal xN, const QPointF& globalPos) {
            onClicked(key, xN, globalPos);
        });
        QObject::connect(overlay, &ArticulationMapOverlay::chipDragged, [this, key](int chipIndex, qreal deltaXN, bool completed) {
            onChipDragged(key, chipIndex, deltaXN, completed);
        });
        QObject::connect(overlay, &ArticulationMapOverlay::dragCancelled, [this, key](int chipIndex) {
            onDragCancelled(key, chipIndex);
        });
    }

    overlay->setContent(chips, lines);

    QVector<qreal> chordXNs;
    for (const ChordEntry& chordEntry : data.chords) {
        chordXNs << toN(chordEntry.canvasX);
    }
    overlay->setChordPositions(chordXNs);

    data.overlay = overlay;
    newOverlays[key] = std::move(data);
}

void NotationArticulationMapController::updateOverlaysGeometry()
{
    const bool visible = articulationMaps() && articulationMaps()->isOverlayEnabled();

    for (const auto& [key, data] : m_overlaysByStaff) {
        data.overlay->setVisible(visible);
        if (!visible) {
            continue;
        }

        const muse::RectF screenRect = m_viewMatrix.map(data.bandRect);
        data.overlay->setWidth(screenRect.width());
        data.overlay->setHeight(screenRect.height());
        data.overlay->setX(screenRect.x());
        data.overlay->setY(screenRect.y());
    }
}

void NotationArticulationMapController::setViewMatrix(const muse::draw::Transform& viewMatrix)
{
    if (viewMatrix == m_viewMatrix) {
        return;
    }
    m_viewMatrix = viewMatrix;

    if (articulationMaps() && articulationMaps()->isOverlayEnabled()) {
        updateOverlaysGeometry();
    }
}

void NotationArticulationMapController::onClicked(const SysStaffKey& key, qreal xN, const QPointF& globalPos)
{
    const auto dataIt = m_overlaysByStaff.find(key);
    if (dataIt == m_overlaysByStaff.end() || dataIt->second.chords.empty()) {
        return;
    }
    const StaffOverlayData& data = dataIt->second;

    // The note under the lane's target marker (the lane chose it, see ArticulationMapOverlay::targetChordXN())
    size_t chordIndex = 0;
    const double canvasX = data.bandRect.x() + xN * data.bandRect.width();
    double bestDistance = std::numeric_limits<double>::max();
    for (size_t i = 0; i < data.chords.size(); ++i) {
        const double distance = std::abs(data.chords[i].canvasX - canvasX);
        if (distance < bestDistance) {
            bestDistance = distance;
            chordIndex = i;
        }
    }

    // The menu runs its own event loop - don't start it from inside the overlay's mouse event handler
    muse::async::Async::call(this, [this, key, chordIndex, globalPos]() {
        showMenu(key, chordIndex, globalPos);
    });
}

void NotationArticulationMapController::showMenu(const SysStaffKey& key, size_t chordIndex, const QPointF& globalPos)
{
    const auto dataIt = m_overlaysByStaff.find(key);
    if (dataIt == m_overlaysByStaff.end() || chordIndex >= dataIt->second.chords.size()) {
        return;
    }
    const StaffOverlayData& data = dataIt->second;
    const ChordEntry& entry = data.chords[chordIndex];

    const ArticulationMapDataConstPtr mapData = articulationMaps()->data();
    const ExpressionMap* map = mapData ? mapData->map(data.trackId) : nullptr;
    if (!map) {
        return;
    }

    const std::vector<ChordRest*> targets = targetMasterChords(data, chordIndex);
    const std::optional<ArticulationMark> ownMark = entry.mark;
    const bool isRest = entry.chord->isRest();
    const InstrumentTrackId trackId = data.trackId;

    // A mark over a notated articulation (e.g. staccato played staccatissimo) is most likely meant
    // for that note only; anywhere else, it most likely starts a new passage - always on a rest
    const ArticulationMark::Scope defaultScope = isRest ? ArticulationMark::Scope::Latched
                                                 : ownMark ? ownMark->scope
                                                 : entry.source == ResolvedArticulation::Source::Alias
                                                 ? ArticulationMark::Scope::SingleChord
                                                 : ArticulationMark::Scope::Latched;

    const auto placeArticulation = [this, targets, ownMark, defaultScope, trackId](const muse::String& entryId) {
        ArticulationMark mark;
        mark.entryId = entryId;
        mark.scope = defaultScope;
        mark.tickOffset = ownMark ? ownMark->tickOffset : 0;
        m_lastPlacedEntryIdByTrack[trackId] = entryId;
        setMarks(targets, mark, muse::TranslatableString("undoableAction", "Set articulation"));
    };

    QMenu menu;
    std::map<QString, QMenu*> submenus;

    // The articulation last given to a note of this track, to repeat it in one click
    const auto lastPlacedIt = m_lastPlacedEntryIdByTrack.find(trackId);
    const ExpressionMapEntry* lastPlaced = lastPlacedIt != m_lastPlacedEntryIdByTrack.end() ? map->entry(lastPlacedIt->second) : nullptr;
    if (lastPlaced) {
        // With its folders, e.g. "Long > Con vibrato" (entry ids are stored that way, see ArticulationMapParser)
        // Neither checkable nor checked, and in italics: it is a shortcut, not a second copy of the
        // articulation listed (and checked) below
        QAction* lastPlacedAction = menu.addAction(lastPlaced->id.toQString());
        QFont font = lastPlacedAction->font();
        font.setItalic(true);
        lastPlacedAction->setFont(font);

        const muse::String entryId = lastPlaced->id;
        QObject::connect(lastPlacedAction, &QAction::triggered, [placeArticulation, entryId]() {
            placeArticulation(entryId);
        });

        menu.addSeparator();
    }

    for (const ExpressionMapEntry& mapEntry : map->entries) {
        if (mapEntry.disabled) {
            continue;
        }

        QMenu* parentMenu = &menu;
        const QStringList path = mapEntry.id.toQString().split('>');
        QString pathSoFar;
        for (qsizetype i = 0; i + 1 < path.size(); ++i) {
            pathSoFar += path[i].trimmed() + ">";
            QMenu*& submenu = submenus[pathSoFar];
            if (!submenu) {
                submenu = parentMenu->addMenu(path[i].trimmed());
            }
            parentMenu = submenu;
        }

        QAction* action = parentMenu->addAction(path.last().trimmed());
        action->setCheckable(true);
        action->setChecked(mapEntry.id == entry.entryId);

        const muse::String entryId = mapEntry.id;
        QObject::connect(action, &QAction::triggered, [placeArticulation, entryId]() {
            placeArticulation(entryId);
        });
    }

    menu.addSeparator();

    if (ownMark && !isRest) {
        QAction* singleChordAction = menu.addAction(muse::qtrc("notation", "This note only"));
        singleChordAction->setCheckable(true);
        singleChordAction->setChecked(ownMark->scope == ArticulationMark::Scope::SingleChord);
        QObject::connect(singleChordAction, &QAction::triggered, [this, targets, ownMark](bool checked) {
            ArticulationMark mark = *ownMark;
            mark.scope = checked ? ArticulationMark::Scope::SingleChord : ArticulationMark::Scope::Latched;
            setMarks(targets, mark, muse::TranslatableString("undoableAction", "Change articulation scope"));
        });

        if (ownMark->tickOffset != 0) {
            QAction* resetOffsetAction = menu.addAction(muse::qtrc("notation", "Reset position"));
            QObject::connect(resetOffsetAction, &QAction::triggered, [this, targets, ownMark]() {
                ArticulationMark mark = *ownMark;
                mark.tickOffset = 0;
                setMarks(targets, mark, muse::TranslatableString("undoableAction", "Reset articulation position"));
            });
        }
    }

    if (ownMark) {
        QAction* removeAction = menu.addAction(muse::qtrc("notation", "Remove articulation change"));
        QObject::connect(removeAction, &QAction::triggered, [this, targets]() {
            setMarks(targets, std::nullopt, muse::TranslatableString("undoableAction", "Remove articulation change"));
        });
    } else {
        QAction* hint = menu.addAction(isRest ? muse::qtrc("notation", "Applies from the next note until the next change")
                                       : defaultScope == ArticulationMark::Scope::SingleChord
                                       ? muse::qtrc("notation", "Applies to this note only")
                                       : muse::qtrc("notation", "Applies until the next change"));
        hint->setEnabled(false);
    }

    menu.addSeparator();

    QAction* editMapAction = menu.addAction(muse::qtrc("notation", "Edit articulation map…"));
    QObject::connect(editMapAction, &QAction::triggered, [this, trackId]() {
        muse::rcommand::CommandQuery query(EDIT_ARTICULATION_MAP_COMMAND);
        query.addParam("partId", muse::Val(std::to_string(trackId.partId.toUint64())));
        query.addParam("instrumentId", muse::Val(trackId.instrumentId.toStdString()));
        commandDispatcher()->dispatch(query);
    });

    // Keep showing which note the choice applies to while the menu is open (the choice may rebuild the lane)
    QPointer<ArticulationMapOverlay> overlay = data.overlay;
    if (overlay) {
        overlay->setPinnedTargetX((entry.canvasX - data.bandRect.x()) / std::max(1.0, data.bandRect.width()));
    }

    menu.exec(globalPos.toPoint());

    if (overlay) {
        overlay->setPinnedTargetX(-1.0);
    }
}

std::vector<ChordRest*> NotationArticulationMapController::targetMasterChords(const StaffOverlayData& data, size_t chordIndex) const
{
    ChordRest* clicked = data.chords[chordIndex].masterChord;
    if (clicked->isRest()) {
        return { clicked };
    }

    const INotationPtr notation = currentNotation();
    const INotationSelectionPtr selection = notation && notation->interaction() ? notation->interaction()->selection() : nullptr;
    if (!selection) {
        return { clicked };
    }

    // Clicking one of several selected chords applies to all of them
    std::vector<ChordRest*> selectedChords;
    for (const Note* note : selection->notes()) {
        Chord* chord = note->chord();
        if (!chord || chord->staffIdx() != data.chords[chordIndex].chord->staffIdx()) {
            continue;
        }

        ChordRest* masterChord = chord->score()->isMaster() ? chord : static_cast<Chord*>(chord->findLinkedInScore(chord->masterScore()));
        if (masterChord && !muse::contains(selectedChords, masterChord)) {
            selectedChords.push_back(masterChord);
        }
    }

    if (selectedChords.size() > 1 && muse::contains(selectedChords, clicked)) {
        return selectedChords;
    }

    return { clicked };
}

void NotationArticulationMapController::setMarks(const std::vector<ChordRest*>& masterChords, const std::optional<ArticulationMark>& mark,
                                                 const muse::TranslatableString& actionName)
{
    if (masterChords.empty() || !articulationMaps()) {
        return;
    }

    EditArticulationMapChanges changes;
    for (const ChordRest* chord : masterChords) {
        EID chordId = chord->eid();
        if (!chordId.isValid()) {
            chordId = chord->assignNewEID();
        }
        changes.marks.emplace(chordId, mark);
    }

    articulationMaps()->edit(changes, actionName);

    if (mark) {
        auditionChord(masterChords.front());
    }
}

void NotationArticulationMapController::auditionChord(const ChordRest* chord)
{
    if (!chord || !chord->isChord() || playbackController()->isPlaying()) {
        return;
    }

    playbackController()->playElements({ chord });
}

double NotationArticulationMapController::ticksPerCanvasUnit(const ChordRest* chord) const
{
    const Measure* measure = chord ? chord->measure() : nullptr;
    if (!measure || measure->width() <= 0.0) {
        return 1.0;
    }

    return measure->ticks().ticks() / measure->width();
}

//! NOTE: a change can be anticipated up to the previous chord, or delayed up to half of its own chord
int NotationArticulationMapController::clampTickOffset(const StaffOverlayData& data, size_t chordIndex, int tickOffset) const
{
    const ChordRest* chord = data.chords[chordIndex].chord;
    const int tick = chord->tick().ticks();

    const int previousTick = chordIndex > 0 ? data.chords[chordIndex - 1].chord->tick().ticks()
                             : chord->measure()->tick().ticks();
    const int minOffset = std::min(0, previousTick - tick + 1);
    const int maxOffset = std::max(0, chord->actualTicks().ticks() / 2);

    return std::clamp(tickOffset, minOffset, maxOffset);
}

void NotationArticulationMapController::onChipDragged(const SysStaffKey& key, int chipIndex, qreal deltaXN, bool completed)
{
    const auto dataIt = m_overlaysByStaff.find(key);
    if (dataIt == m_overlaysByStaff.end() || chipIndex < 0 || static_cast<size_t>(chipIndex) >= dataIt->second.chipChords.size()) {
        return;
    }
    const StaffOverlayData& data = dataIt->second;

    const size_t chordIndex = data.chipChords[chipIndex];
    const ChordEntry& entry = data.chords[chordIndex];

    // A change on a rest takes effect at the next chord: there's nothing to fine-tune
    if (entry.chord->isRest()) {
        if (completed) {
            onDragCancelled(key, chipIndex);
        }
        return;
    }

    // Moving an automatic articulation (score articulation, default, resumed latched mark) turns it
    // into a mark of the same articulation for this chord only: nothing else about playback changes
    ArticulationMark startMark;
    if (entry.mark) {
        startMark = *entry.mark;
    } else {
        startMark.entryId = entry.entryId;
        startMark.scope = ArticulationMark::Scope::SingleChord;
        startMark.tickOffset = 0;
    }

    const double ticksPerUnit = ticksPerCanvasUnit(entry.chord);
    const double startOffsetCanvas = startMark.tickOffset / ticksPerUnit;
    const double offsetCanvas = startOffsetCanvas + deltaXN * data.bandRect.width();

    // Snap back onto the chord itself when released close to it
    const double snapCanvas = ARTMAP_SNAP_DISTANCE_PX / std::max(1e-9, m_viewMatrix.m11());
    int tickOffset = std::abs(offsetCanvas) < snapCanvas ? 0 : static_cast<int>(std::lround(offsetCanvas * ticksPerUnit));
    tickOffset = clampTickOffset(data, chordIndex, tickOffset);

    if (!completed) {
        const double previewCanvasX = entry.canvasX + tickOffset / ticksPerUnit;
        data.overlay->setChipPreviewX(chipIndex, (previewCanvasX - data.bandRect.x()) / std::max(1.0, data.bandRect.width()));
        return;
    }

    if (tickOffset == startMark.tickOffset) {
        onDragCancelled(key, chipIndex);
        return;
    }

    // The edit below rebuilds the lanes anyway
    m_rebuildAfterDrag = false;

    ArticulationMark mark = startMark;
    mark.tickOffset = tickOffset;
    setMarks({ entry.masterChord }, mark, muse::TranslatableString("undoableAction", "Move articulation change"));
}

void NotationArticulationMapController::onDragCancelled(const SysStaffKey& key, int chipIndex)
{
    if (m_rebuildAfterDrag) {
        m_rebuildAfterDrag = false;
        scheduleRebuild();
        return;
    }

    const auto dataIt = m_overlaysByStaff.find(key);
    if (dataIt == m_overlaysByStaff.end() || chipIndex < 0 || chipIndex >= dataIt->second.overlay->chips().size()) {
        return;
    }

    const ArticulationMapOverlay::ChipData& chip = dataIt->second.overlay->chips().at(chipIndex);
    const ChordEntry& entry = dataIt->second.chords[dataIt->second.chipChords[chipIndex]];
    const double offsetCanvas = entry.mark ? entry.mark->tickOffset / ticksPerCanvasUnit(entry.chord) : 0.0;
    dataIt->second.overlay->setChipPreviewX(chipIndex, chip.chordXN + offsetCanvas / std::max(1.0, dataIt->second.bandRect.width()));
}

INotationArticulationMapsPtr NotationArticulationMapController::articulationMaps() const
{
    const IMasterNotationPtr masterNotation = globalContext()->currentMasterNotation();
    return masterNotation ? masterNotation->articulationMaps() : nullptr;
}

INotationPtr NotationArticulationMapController::currentNotation() const
{
    return globalContext()->currentNotation();
}

Score* NotationArticulationMapController::score() const
{
    return currentNotation() ? currentNotation()->elements()->msScore() : nullptr;
}
