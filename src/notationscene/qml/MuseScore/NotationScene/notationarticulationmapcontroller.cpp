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

    for (const Segment* seg = system->firstMeasure() ? system->firstMeasure()->first(SegmentType::ChordRest) : nullptr;
         seg && seg->system() == system; seg = seg->next1(SegmentType::ChordRest)) {
        // One lane per staff: the first voice with a chord at this position speaks for it
        for (track_idx_t track = strack; track < etrack; ++track) {
            EngravingItem* item = seg->element(track);
            if (!item || !item->isChord()) {
                continue;
            }

            Chord* chord = toChord(item);
            Chord* masterChord = chord->score() == masterScore ? chord : static_cast<Chord*>(chord->findLinkedInScore(masterScore));
            if (!masterChord) {
                continue;
            }

            const std::optional<ResolvedArticulation> resolved = playback->resolvedArticulation(masterChord->track(),
                                                                                                masterChord->tick().ticks());
            if (!resolved) {
                continue;
            }

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
            entry.entryId = resolved->entryId;
            entry.source = resolved->source;
            if (masterChord->eid().isValid()) {
                entry.mark = mapData->mark(masterChord->eid());
            }
            data.chords.push_back(std::move(entry));
            break;
        }
    }

    if (data.chords.empty() || !map) {
        return;
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

    for (size_t i = 0; i < data.chords.size(); ++i) {
        const ChordEntry& entry = data.chords[i];
        const QColor color = artMapEntryColor(*map, entry.entryId);

        const double nextX = i + 1 < data.chords.size() ? data.chords[i + 1].canvasX : data.bandRect.right();
        lines.push_back({ toN(entry.canvasX), toN(nextX), color });

        using Source = ResolvedArticulation::Source;
        const bool isOwnMark = entry.source == Source::OwnMark;
        const bool isAlias = entry.source == Source::Alias;
        const bool changed = i == 0 || entry.entryId != previousEntryId;
        previousEntryId = entry.entryId;

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

        QObject::connect(overlay, &ArticulationMapOverlay::clicked, [this, key](int chipIndex, qreal xN, const QPointF& globalPos) {
            onClicked(key, chipIndex, xN, globalPos);
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

void NotationArticulationMapController::onClicked(const SysStaffKey& key, int chipIndex, qreal xN, const QPointF& globalPos)
{
    const auto dataIt = m_overlaysByStaff.find(key);
    if (dataIt == m_overlaysByStaff.end() || dataIt->second.chords.empty()) {
        return;
    }
    const StaffOverlayData& data = dataIt->second;

    size_t chordIndex = 0;
    if (chipIndex >= 0 && static_cast<size_t>(chipIndex) < data.chipChords.size()) {
        chordIndex = data.chipChords[chipIndex];
    } else {
        // An empty part of the lane: the nearest chord
        const double canvasX = data.bandRect.x() + xN * data.bandRect.width();
        double bestDistance = std::numeric_limits<double>::max();
        for (size_t i = 0; i < data.chords.size(); ++i) {
            const double distance = std::abs(data.chords[i].canvasX - canvasX);
            if (distance < bestDistance) {
                bestDistance = distance;
                chordIndex = i;
            }
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

    const std::vector<Chord*> targets = targetMasterChords(data, chordIndex);
    const std::optional<ArticulationMark> ownMark = entry.mark;

    // A mark over a notated articulation (e.g. staccato played staccatissimo) is most likely meant
    // for that note only; anywhere else, it most likely starts a new passage
    const ArticulationMark::Scope defaultScope = ownMark ? ownMark->scope
                                                 : entry.source == ResolvedArticulation::Source::Alias
                                                 ? ArticulationMark::Scope::SingleChord
                                                 : ArticulationMark::Scope::Latched;

    QMenu menu;
    std::map<QString, QMenu*> submenus;

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
        QObject::connect(action, &QAction::triggered, [this, targets, entryId, ownMark, defaultScope]() {
            ArticulationMark mark;
            mark.entryId = entryId;
            mark.scope = defaultScope;
            mark.tickOffset = ownMark ? ownMark->tickOffset : 0;
            setMarks(targets, mark, muse::TranslatableString("undoableAction", "Set articulation"));
        });
    }

    menu.addSeparator();

    if (ownMark) {
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

        QAction* removeAction = menu.addAction(muse::qtrc("notation", "Remove articulation change"));
        QObject::connect(removeAction, &QAction::triggered, [this, targets]() {
            setMarks(targets, std::nullopt, muse::TranslatableString("undoableAction", "Remove articulation change"));
        });
    } else {
        QAction* hint = menu.addAction(defaultScope == ArticulationMark::Scope::SingleChord
                                       ? muse::qtrc("notation", "Applies to this note only")
                                       : muse::qtrc("notation", "Applies until the next change"));
        hint->setEnabled(false);
    }

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

std::vector<Chord*> NotationArticulationMapController::targetMasterChords(const StaffOverlayData& data, size_t chordIndex) const
{
    Chord* clicked = data.chords[chordIndex].masterChord;

    const INotationPtr notation = currentNotation();
    const INotationSelectionPtr selection = notation && notation->interaction() ? notation->interaction()->selection() : nullptr;
    if (!selection) {
        return { clicked };
    }

    // Clicking one of several selected chords applies to all of them
    std::vector<Chord*> selectedChords;
    for (const Note* note : selection->notes()) {
        Chord* chord = note->chord();
        if (!chord || chord->staffIdx() != data.chords[chordIndex].chord->staffIdx()) {
            continue;
        }

        Chord* masterChord = chord->score()->isMaster() ? chord : static_cast<Chord*>(chord->findLinkedInScore(chord->masterScore()));
        if (masterChord && !muse::contains(selectedChords, masterChord)) {
            selectedChords.push_back(masterChord);
        }
    }

    if (selectedChords.size() > 1 && muse::contains(selectedChords, clicked)) {
        return selectedChords;
    }

    return { clicked };
}

void NotationArticulationMapController::setMarks(const std::vector<Chord*>& masterChords, const std::optional<ArticulationMark>& mark,
                                                 const muse::TranslatableString& actionName)
{
    if (masterChords.empty() || !articulationMaps()) {
        return;
    }

    EditArticulationMapChanges changes;
    for (const Chord* chord : masterChords) {
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

void NotationArticulationMapController::auditionChord(const Chord* chord)
{
    if (!chord || playbackController()->isPlaying()) {
        return;
    }

    playbackController()->playElements({ chord });
}

double NotationArticulationMapController::ticksPerCanvasUnit(const Chord* chord) const
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
    const Chord* chord = data.chords[chordIndex].chord;
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
