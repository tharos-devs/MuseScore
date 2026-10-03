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

#include "notationautomationcontroller.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <optional>
#include <set>

#include <QHBoxLayout>
#include <QKeyEvent>
#include <QDoubleSpinBox>
#include <QGuiApplication>

#include "segmentcanvasinterpolation.h"
#include "midiccautomation.h"

#include "uicomponents/qml/Muse/UiComponents/polylineplot.h"

#include "engraving/iengravingconfiguration.h" // IWYU pragma: keep
#include "engraving/automation/automationdata.h"
#include "engraving/automation/dynamicvalues.h"
#include "engraving/automation/tempovalues.h"
#include "engraving/dom/masterscore.h"
#include "engraving/dom/part.h"
#include "engraving/dom/repeatlist.h"
#include "engraving/dom/segment.h"
#include "engraving/dom/spanner.h"
#include "engraving/infrastructure/eidregister.h"
#include "engraving/dom/staff.h"

#include "notation/imasternotation.h"
#include "notation/inotation.h"
#include "notation/inotationautomation.h"
#include "notation/inotationelements.h" // IWYU pragma: keep
#include "notation/inotationinteraction.h"
#include "notation/inotationselection.h"
#include "notation/inotationselectionrange.h"

#include "global/async/async.h"
#include "global/containers.h"
#include "global/realfn.h"

using namespace mu::notation;
using namespace mu::engraving;
using namespace muse::uicomponents;

using SetPoint = mu::engraving::AutomationPointEdit::SetPoint;
using MovePoint = mu::engraving::AutomationPointEdit::MovePoint;
using ErasePoint = mu::engraving::AutomationPointEdit::ErasePoint;

constexpr static qreal POLYLINE_LINE_WIDTH = 1.5;

constexpr static qreal POLYLINE_STANDARD_CENTER_RADIUS = 3.0;
constexpr static qreal POLYLINE_HOVERED_CENTER_RADIUS = 3.5;
constexpr static qreal POLYLINE_SELECTED_CENTER_RADIUS = 3.5;

constexpr static qreal POLYLINE_SELECTED_MIDDLE_RING_WIDTH = 1.5;

constexpr static int POLYLINE_SELECTED_HOVERED_ALPHA = 127;
constexpr static int POLYLINE_GENERATED_AREA_ALPHA = 51;
constexpr static int POLYLINE_EDITED_AREA_ALPHA = 102;

static bool polylinePointIndexIsValid(const PolylinePlot* polyline, int pointIdx)
{
    IF_ASSERT_FAILED(polyline) {
        return false;
    }
    return pointIdx > -1 && pointIdx < static_cast<int>(polyline->points().size());
}

// Rescale between the common dynamic value range [PPPP, FFFF] and the full [0, 1] display range
// (the staff box), so those points fill the staff
static const muse::real_t DYNAMICS_DISPLAY_RANGE_MIN = mu::engraving::ORDINARY_DYNAMIC_VALUES.at(mu::engraving::DynamicType::PPPP);
static const muse::real_t DYNAMICS_DISPLAY_RANGE_MAX = mu::engraving::ORDINARY_DYNAMIC_VALUES.at(mu::engraving::DynamicType::FFFF);

// Must match muse::audio::VOLUME_DB_MIN/MAX
static constexpr double VOLUME_RANGE_MIN_DB = -60.0;
static constexpr double VOLUME_RANGE_MAX_DB = 12.0;

// Mirrors VolumeSlider.qml's fader curve, so dragging a point feels like moving the mixer fader -
// a plain linear map would put 0dB at 83% up the lane instead of the center
static constexpr double VOLUME_LOCAL_CENTER_DB = -24.0;
static constexpr double VOLUME_LOGICAL_CENTER_DB = -12.0;
static constexpr double VOLUME_HIGH_ACCURACY_STEP = 1.5;
static constexpr double VOLUME_LOW_ACCURACY_STEP = 0.75;

// logical (actual) dB -> local (linear-in-display) dB
static double volumeLogicalDbToLocalDb(double logicalDb)
{
    if (logicalDb > VOLUME_LOGICAL_CENTER_DB) {
        const double diff = VOLUME_RANGE_MAX_DB - logicalDb;
        return VOLUME_RANGE_MAX_DB - diff * VOLUME_HIGH_ACCURACY_STEP;
    }

    const double diff = VOLUME_LOGICAL_CENTER_DB - logicalDb;
    return VOLUME_LOCAL_CENTER_DB - diff * VOLUME_LOW_ACCURACY_STEP;
}

// local (linear-in-display) dB -> logical (actual) dB
static double volumeLocalDbToLogicalDb(double localDb)
{
    if (localDb > VOLUME_LOCAL_CENTER_DB) {
        const double diff = VOLUME_RANGE_MAX_DB - localDb;
        return VOLUME_RANGE_MAX_DB - diff / VOLUME_HIGH_ACCURACY_STEP;
    }

    const double diff = VOLUME_LOCAL_CENTER_DB - localDb;
    return VOLUME_LOGICAL_CENTER_DB - diff / VOLUME_LOW_ACCURACY_STEP;
}

// Mirrors the volume fader curve above, anchored on the default tempo instead of 0dB - a plain
// linear map would put the default tempo (120bpm) at ~12% up the lane instead of the center
// This is a UI-only display cap - the score model still allows tempos up to Constants::MAX_TEMPO
static constexpr double TEMPO_RANGE_MAX_BPM = 300.0;
static const double TEMPO_LOGICAL_CENTER_BPM = mu::engraving::Constants::DEFAULT_TEMPO.toBPM().val;
static const double TEMPO_LOCAL_CENTER_BPM = TEMPO_RANGE_MAX_BPM / 2.0;
static const double TEMPO_HIGH_ACCURACY_STEP = (TEMPO_RANGE_MAX_BPM - TEMPO_LOCAL_CENTER_BPM)
                                               / (TEMPO_RANGE_MAX_BPM - TEMPO_LOGICAL_CENTER_BPM);
static const double TEMPO_LOW_ACCURACY_STEP = TEMPO_LOCAL_CENTER_BPM / TEMPO_LOGICAL_CENTER_BPM;

// logical (actual) bpm -> local (linear-in-display) bpm
static double tempoLogicalBpmToLocalBpm(double logicalBpm)
{
    if (logicalBpm > TEMPO_LOGICAL_CENTER_BPM) {
        const double diff = TEMPO_RANGE_MAX_BPM - logicalBpm;
        return TEMPO_RANGE_MAX_BPM - diff * TEMPO_HIGH_ACCURACY_STEP;
    }

    const double diff = TEMPO_LOGICAL_CENTER_BPM - logicalBpm;
    return TEMPO_LOCAL_CENTER_BPM - diff * TEMPO_LOW_ACCURACY_STEP;
}

// local (linear-in-display) bpm -> logical (actual) bpm
static double tempoLocalBpmToLogicalBpm(double localBpm)
{
    if (localBpm > TEMPO_LOCAL_CENTER_BPM) {
        const double diff = TEMPO_RANGE_MAX_BPM - localBpm;
        return TEMPO_RANGE_MAX_BPM - diff / TEMPO_HIGH_ACCURACY_STEP;
    }

    const double diff = TEMPO_LOCAL_CENTER_BPM - localBpm;
    return TEMPO_LOGICAL_CENTER_BPM - diff / TEMPO_LOW_ACCURACY_STEP;
}

// Values are stored normalized [0, 1]; Dynamics rescales that into its own sub-range for display,
// Volume and Tempo additionally apply a fader curve, other types map 1:1 onto the display range
static double automationValueToDisplay(AutomationType type, muse::real_t value)
{
    if (type == AutomationType::Dynamics) {
        const double display = (value - DYNAMICS_DISPLAY_RANGE_MIN) / (DYNAMICS_DISPLAY_RANGE_MAX - DYNAMICS_DISPLAY_RANGE_MIN);
        return std::clamp(display, 0.0, 1.0);
    }

    if (type == AutomationType::Volume) {
        const double logicalDb = VOLUME_RANGE_MIN_DB + static_cast<double>(value) * (VOLUME_RANGE_MAX_DB - VOLUME_RANGE_MIN_DB);
        const double localDb = volumeLogicalDbToLocalDb(logicalDb);
        const double display = (localDb - VOLUME_RANGE_MIN_DB) / (VOLUME_RANGE_MAX_DB - VOLUME_RANGE_MIN_DB);
        return std::clamp(display, 0.0, 1.0);
    }

    if (type == AutomationType::Tempo) {
        const double logicalBpm = mu::engraving::denormalizeTempo(value).toBPM().val;
        const double localBpm = tempoLogicalBpmToLocalBpm(logicalBpm);
        const double display = localBpm / TEMPO_RANGE_MAX_BPM;
        return std::clamp(display, 0.0, 1.0);
    }

    return std::clamp(static_cast<double>(value), 0.0, 1.0);
}

// existingValue preserves Tempo values above the display cap when the drag lands back at the same clamped edge
static muse::real_t automationValueFromDisplay(AutomationType type, double displayValue,
                                               std::optional<muse::real_t> existingValue = std::nullopt)
{
    if (type == AutomationType::Dynamics) {
        return DYNAMICS_DISPLAY_RANGE_MIN + displayValue * (DYNAMICS_DISPLAY_RANGE_MAX - DYNAMICS_DISPLAY_RANGE_MIN);
    }

    if (type == AutomationType::Volume) {
        const double localDb = VOLUME_RANGE_MIN_DB + displayValue * (VOLUME_RANGE_MAX_DB - VOLUME_RANGE_MIN_DB);
        const double logicalDb = volumeLocalDbToLogicalDb(localDb);
        const double normalized = (logicalDb - VOLUME_RANGE_MIN_DB) / (VOLUME_RANGE_MAX_DB - VOLUME_RANGE_MIN_DB);
        return muse::real_t(normalized);
    }

    if (type == AutomationType::Tempo) {
        if (existingValue && muse::RealIsEqualOrMore(displayValue, 1.0)
            && muse::RealIsEqualOrMore(automationValueToDisplay(type, *existingValue), 1.0)) {
            return *existingValue;
        }

        const double localBpm = displayValue * TEMPO_RANGE_MAX_BPM;
        const double logicalBpm = tempoLocalBpmToLogicalBpm(localBpm);
        const muse::real_t normalized = mu::engraving::normalizeTempo(mu::engraving::BeatsPerSecond::fromBPM(logicalBpm));
        return std::clamp(normalized, mu::engraving::MIN_NORMALIZED_TEMPO, mu::engraving::MAX_NORMALIZED_TEMPO);
    }

    if (type == AutomationType::MidiCC) {
        // Snap to the 128 values a MIDI controller can actually take
        return muse::real_t(std::round(std::clamp(displayValue, 0.0, 1.0) * 127.0) / 127.0);
    }

    return muse::real_t(displayValue);
}

// The Dynamics lane shows [pppp, ffff], one dynamic level apart from the next (see ORDINARY_DYNAMIC_VALUES):
// a display value as a level from pppp (0) to ffff (9), fractions in between
static const QStringList DYNAMIC_LEVEL_NAMES { "pppp", "ppp", "pp", "p", "mp", "mf", "f", "ff", "fff", "ffff" };
static constexpr double DYNAMIC_LEVEL_SNAP_DISTANCE_PX = 6.0;

static double dynamicLevelFromDisplay(double display)
{
    return display * (DYNAMIC_LEVEL_NAMES.size() - 1);
}

static double dynamicDisplayFromLevel(double level)
{
    return level / (DYNAMIC_LEVEL_NAMES.size() - 1);
}

//! NOTE: a Dynamics point's y (inverted display value), drawn to the nearest dynamic level when close to it
static qreal snappedDynamicsY(AutomationType type, qreal heightPx, qreal y, bool free)
{
    if (type != AutomationType::Dynamics || heightPx <= 0 || free) {
        return y;
    }

    const double level = dynamicLevelFromDisplay(std::clamp(1.0 - y, 0.0, 1.0));
    const double nearest = std::round(level);
    if (std::abs(dynamicDisplayFromLevel(level - nearest)) * heightPx <= DYNAMIC_LEVEL_SNAP_DISTANCE_PX) {
        return 1.0 - dynamicDisplayFromLevel(nearest);
    }

    return y;
}

//! NOTE: e.g. "f" exactly on forte's level, "f +20%" a fifth of the way from it to ff - always from the dynamic below
static QString dynamicLevelText(double level)
{
    const int lastLevel = static_cast<int>(DYNAMIC_LEVEL_NAMES.size()) - 1;
    int below = std::clamp(static_cast<int>(std::floor(level)), 0, lastLevel);
    int offsetPercent = static_cast<int>(std::lround((level - below) * 100.0));
    if (offsetPercent >= 100 && below < lastLevel) {
        ++below;
        offsetPercent = 0;
    }

    if (offsetPercent <= 0) {
        return DYNAMIC_LEVEL_NAMES.at(below);
    }

    return QString("%1 +%2%").arg(DYNAMIC_LEVEL_NAMES.at(below)).arg(offsetPercent);
}

// Formats a point's "display"-space value (as tracked live by PolylinePlot during a drag, i.e. the
// same value passed as pointMoved's y argument) into the drag tooltip's label text, in each
// automation type's own natural unit - reuses the fader-curve helpers above rather than duplicating them
static QString formattedActivePointValue(AutomationType type, double pointDomainY, int midiCc)
{
    // Point Y is stored inverted relative to the display range - "higher value == lower Y" (see the
    // "1.0 - automationValueToDisplay(...)" convention used when building/editing points above) - so
    // flip back to the real display value before formatting.
    const double displayValue = std::clamp(1.0 - pointDomainY, 0.0, 1.0);

    if (type == AutomationType::Dynamics) {
        return dynamicLevelText(dynamicLevelFromDisplay(displayValue));
    }

    if (type == AutomationType::MidiCC) {
        return QString("CC%1: %2").arg(midiCc).arg(qRound(displayValue * 127.0));
    }

    if (type == AutomationType::Volume) {
        const double localDb = VOLUME_RANGE_MIN_DB + displayValue * (VOLUME_RANGE_MAX_DB - VOLUME_RANGE_MIN_DB);
        const double logicalDb = volumeLocalDbToLogicalDb(localDb);
        return QString("%1 dB").arg(qRound(logicalDb));
    }

    if (type == AutomationType::Tempo) {
        const double localBpm = displayValue * TEMPO_RANGE_MAX_BPM;
        // Clamp to the same [MIN_TEMPO, MAX_TEMPO] floor/ceiling automationValueFromDisplay() enforces
        // when the drag is committed, so the live tooltip never shows a value the point can't actually land on
        const double logicalBpm = std::clamp(tempoLocalBpmToLogicalBpm(localBpm),
                                             mu::engraving::Constants::MIN_TEMPO.toBPM().val,
                                             mu::engraving::Constants::MAX_TEMPO.toBPM().val);
        return QString("%1 BPM").arg(qRound(logicalBpm));
    }

    // Pan: matches the Mixer's own balance display (mixerchannelitem.cpp's BALANCE_SCALING_FACTOR),
    // not MIDI CC10 - the engine's balance_t is -1.0..+1.0, shown there as a signed -100..+100 percentage
    const int panValue = qRound((displayValue - 0.5) * 2.0 * 100.0);
    return panValue > 0 ? QString("+%1").arg(panValue) : QString::number(panValue);
}

static const Segment* lastSegmentOfSystem(const System* system)
{
    const mu::engraving::SegmentType type = mu::engraving::SegmentType::Duration;
    const Segment* seg = system->firstMeasure() ? system->firstMeasure()->first(type) : nullptr;
    const Segment* last = nullptr;
    while (seg && seg->system() == system) {
        last = seg;
        seg = seg->next1MM(type);
    }
    return last;
}

// Maps an x position to a tick via linear interpolation between the nearest Duration/barline segments on either side of it
static std::optional<int> automationTickFromCanvasX(const System* system, const muse::RectF& staffCanvasRect, qreal x)
{
    const double pointCanvasX = staffCanvasRect.x() + x * staffCanvasRect.width();
    return tickFromCanvasX(system, pointCanvasX);
}

static AutomationCurveKey curveKeyFor(AutomationType type, const Staff* staff)
{
    switch (type) {
    case AutomationType::Dynamics:
        return AutomationCurveKey::staff(type, staff->id());
    case AutomationType::Tempo:
        return AutomationCurveKey::global(type);
    case AutomationType::Volume:
    case AutomationType::Pan:
    case AutomationType::MidiCC: {
        const Part* part = staff->part();
        const InstrumentTrackId trackId { part->id(), part->instrumentId() };
        return AutomationCurveKey::instrument(type, trackId);
    }
    case AutomationType::Unknown:
        break;
    }

    return AutomationCurveKey();
}

static staff_idx_t firstVisibleStaffIdx(const Score* score)
{
    for (staff_idx_t i = 0; i < score->nstaves(); ++i) {
        if (score->staff(i)->show()) {
            return i;
        }
    }

    return muse::nidx;
}

static bool isStructuralChange(const mu::engraving::ScoreChanges& changes)
{
    if (!changes.changedObjects.empty() && !changes.isValidBoundary()) {
        return true;
    }

    static const std::unordered_set<mu::engraving::ElementType> STRUCTURAL_TYPES {
        mu::engraving::ElementType::MEASURE,
        mu::engraving::ElementType::PART,
    };

    for (const mu::engraving::ElementType type : changes.changedTypes) {
        if (muse::contains(STRUCTURAL_TYPES, type)) {
            return true;
        }
    }

    return false;
}

//! NOTE: the curves whose segments can be bent - their playback honors the bend (Volume/Pan: the engine evaluates
//! the envelope, Tempo: TempoTimeline resamples ramps with it). Not Dynamics yet
static bool isBendable(AutomationType type)
{
    switch (type) {
    case AutomationType::MidiCC:
    case AutomationType::Volume:
    case AutomationType::Pan:
    case AutomationType::Tempo:
        return true;
    case AutomationType::Dynamics:
    case AutomationType::Unknown:
        break;
    }

    return false;
}

// Also the reference the area under the line is filled from - MIDI CCs keep 0, so it's always filled from the bottom
static qreal defaultValueFor(AutomationType type)
{
    if (type == AutomationType::Pan) {
        return 0.5;
    }
    return 0.0;
}

NotationAutomationController::NotationAutomationController(QQuickItem* linesParent, const muse::modularity::ContextPtr& iocCtx)
    : muse::Contextable(iocCtx), m_linesParent(linesParent)
{
}

NotationAutomationController::~NotationAutomationController()
{
    closePointValueEditor();
}

void NotationAutomationController::init()
{
    IF_ASSERT_FAILED(automation() && currentNotation()) {
        return;
    }

    onCurrentNotationChanged();

    automation()->automationModeEnabledChanged().onNotify(this, [this]() {
        closePointValueEditor();
        if (automation()->isAutomationModeEnabled() && !m_pendingChanges.isEmpty()) {
            applyAutomationChanges(m_pendingChanges);
            m_pendingChanges.clear();
        } else {
            updatePolylinesGeometry();
        }
        refreshGroupFlags(); // the selection may have changed while automation was hidden
    }, Asyncable::Mode::SetReplace /* FIXME */);

    notationConfiguration()->currentAutomationTypeChanged().onNotify(this, [this]() {
        rebuildAllPolylines();
    }, Asyncable::Mode::SetReplace /* FIXME */);

    globalContext()->currentNotationChanged().onNotify(this, [this]() {
        onCurrentNotationChanged();
    }, Asyncable::Mode::SetReplace /* FIXME */);

    notationConfiguration()->scoreInversionChanged().onNotify(this, [this]() {
        updatePolylinesColors();
    }, Asyncable::Mode::SetReplace /* FIXME */);

    notationConfiguration()->isOnlyInvertInDarkThemeChanged().onNotify(this, [this]() {
        updatePolylinesColors();
    }, Asyncable::Mode::SetReplace /* FIXME */);

    uiConfiguration()->currentThemeChanged().onNotify(this, [this]() {
        updatePolylinesColors();
    }, Asyncable::Mode::SetReplace /* FIXME */);

    engravingConfiguration()->selectionColorChanged().onReceive(this, [this](voice_idx_t idx, const muse::draw::Color&) {
        // voice 1 color is used for the centre of selected points, "all voices color" is used for the area under lines
        if (idx == 0 || idx == mu::engraving::VOICES) {
            updatePolylinesColors();
        }
    }, Asyncable::Mode::SetReplace /* FIXME */);
}

NotationAutomationController::SysStaffToPolylinesMap NotationAutomationController::createPolylinesForSystem(const System* system)
{
    IF_ASSERT_FAILED(system && m_linesParent && score()) {
        return {};
    }

    SysStaffToPolylinesMap map;

    staff_idx_t staffIdx = system->firstVisibleStaff();
    while (staffIdx != muse::nidx) {
        PolylinePlot* polyline = createPolylineForStaff(system, staffIdx);
        if (polyline) {
            map.emplace(SysStaffKey(system, staffIdx), PolylinesSet({ polyline }));
        }
        staffIdx = system->nextVisibleStaff(staffIdx);
    }

    return map;
}

muse::uicomponents::PolylinePlot* NotationAutomationController::createPolylineForStaff(const System* system, staff_idx_t staffIdx)
{
    IF_ASSERT_FAILED(system && m_linesParent && score()) {
        return nullptr;
    }

    const Staff* staff = score()->staff(staffIdx);
    const SysStaff* sysStaff = system->staff(staffIdx);
    if (!staff || !sysStaff || !staff->isPrimaryStaff()) {
        return nullptr;
    }

    const AutomationCurveKey curveKey = currentCurveKeyFor(staff);
    if (curveKey.type == AutomationType::MidiCC && !midicc::isVstInstrument(globalContext()->currentProject(), staff->part())) {
        // MIDI CCs only reach VST instruments
        return nullptr;
    }

    if (curveKey.isGlobal() && staffIdx != firstVisibleStaffIdx(score())) {
        // Score-scoped automation is only drawn on the score's first staff
        return nullptr;
    }

    if (curveKey.trackId().has_value() && !staff->isTop()) {
        // Instrument-scoped automation is only drawn on the instrument's first staff
        return nullptr;
    }

    const Measure* firstMeasure = system->firstMeasure();
    const Segment* firstSeg = firstMeasure ? firstMeasure->first(mu::engraving::SegmentType::Duration) : nullptr;
    const Segment* lastSeg = lastSegmentOfSystem(system);

    // TODO: Staves can have multiple polylines due to horizontal frames, at the moment we're
    // providing a single polyline over the entire staff...
    PolylinePlot* polyline = new PolylinePlot(m_linesParent);

    const muse::RectF staffCanvasRect = sysStaff->bbox().translated(system->canvasPos());
    const QVector<PointData> pointsData = pointsDataInStaff(system, staff, staffCanvasRect);

    const SysStaffKey key(system, staffIdx);
    m_pointsDataByStaff[key] = pointsData;

    //! NOTE: There can't be a 1-to-1 match between the number of points in the automation model and
    //! points on the polyline. A point with equal in/out values (i.e. a "BOTH" point) is represented
    //! as 1 polyline point, whereas a point with different in/out values will be represented with 2
    //! separate polyline points...
    QVector<QPointF> pointsForPolyline;
    pointsForPolyline.reserve(pointsData.size());
    for (const PointData& pointData : pointsData) {
        pointsForPolyline.emplace_back(pointData.qPointF);
    }
    polyline->setPoints(pointsForPolyline);
    // Typed values (Cmd+click): not on dynamics, placed by dragging, drawn to their levels
    polyline->setPointValueEditEnabled(curveKey.type != AutomationType::Dynamics && curveKey.type != AutomationType::Unknown);
    applyPointFlags(polyline, key);
    applyPolylineStyle(polyline, key);
    polyline->setVisible(false);

    // Points can't be dragged past the system's first/last segment
    const qreal minX = firstSeg ? (firstSeg->canvasX() - staffCanvasRect.x()) / staffCanvasRect.width() : 0.0;
    const qreal maxX = lastSeg ? (lastSeg->canvasX() + lastSeg->width() - staffCanvasRect.x()) / staffCanvasRect.width() : 1.0;

    QObject::connect(polyline, &muse::uicomponents::PolylinePlot::pointMoved,
                     [this, key, polyline, minX, maxX](int pointIdx, qreal x, qreal y, bool completed) {
        IF_ASSERT_FAILED(polylinePointIndexIsValid(polyline, pointIdx)) {
            return;
        }

        const auto pointsDataIt = m_pointsDataByStaff.find(key);
        IF_ASSERT_FAILED(pointsDataIt != m_pointsDataByStaff.end() && pointIdx < pointsDataIt->second.size()) {
            return;
        }

        const PointData& oldPointData = pointsDataIt->second[pointIdx];

        // Alt is usually let go just before the mouse button: the release keeps what the drag was doing
        const bool altHeld = QGuiApplication::queryKeyboardModifiers() & Qt::AltModifier;
        const bool freeDrag = altHeld || (completed && m_freeDynamicsDrag);
        m_freeDynamicsDrag = completed ? false : altHeld;
        y = snappedDynamicsY(currentAutomationType(), polyline->height(), y, freeDrag);

        // Pressing on the line inserts a pending point (not in the curve yet, see pointAdded below) that
        // PolylinePlot lets the user drag right away - press-and-drag must create it at the drop position
        const bool isPendingPoint = oldPointData.polylinePointIndex < 0;

        const mu::engraving::AutomationPoint* automationPoint = automationPointAt(key, oldPointData.tick);
        const bool editRestricted = !isPendingPoint && isScoreDrivenPoint(automationPoint);
        const qreal clampedX = editRestricted ? oldPointData.qPointF.x() : std::clamp(x, minX, maxX);

        // A point of the range selection: the whole selection moves with it, vertically only
        if (m_groupDrag.active || (!isPendingPoint && isGroupSelected(key, oldPointData))) {
            if (!m_groupDrag.active) {
                startGroupDrag();
            }

            const auto originsIt = m_groupDrag.origins.find(key);
            const qreal originY = originsIt != m_groupDrag.origins.end() && pointIdx < originsIt->second.size()
                                  ? originsIt->second.at(pointIdx).y() : oldPointData.qPointF.y();
            const qreal delta = y - originY;

            if (completed) {
                commitGroupDrag(delta);
            } else {
                previewGroupDrag(delta);
            }
            return;
        }

        const auto setPreviewPoint = [this, polyline, pointIdx, key](const QPointF& point) {
            QVector<QPointF> points = polyline->points();
            points.replace(pointIdx, point);
            polyline->setPoints(points);
            applyPolylineColorsUnderLine(polyline, key);
            polyline->update(); // TODO: pass update rect?
        };

        if (completed) {
            if (isPendingPoint) {
                requestAddPoint(key, clampedX, y);
                return;
            }

            if (!requestEditPoint(oldPointData, key, clampedX, y)) {
                // Edit was rejected - snap the point back to where it actually is instead of
                // leaving the live-drag preview stuck at the rejected position
                setPreviewPoint(oldPointData.qPointF);
            }
            return;
        }

        // Live drag preview
        setPreviewPoint({ clampedX, y });
    });

    // Shown only while the mouse is pressed (see PolylinePlot::paint()'s m_pressed gate) - fires as
    // soon as a point is pressed, even before any drag movement, so a plain click (to check a point's
    // value without necessarily moving it) shows the tooltip too, not just an active drag. Deliberately
    // NOT shown on hover alone - with closely-spaced points that would be too noisy/intrusive.
    QObject::connect(polyline, &muse::uicomponents::PolylinePlot::activePointChanged, [this, polyline]() {
        polyline->setActivePointLabel(polyline->hasActivePoint()
                                      ? formattedActivePointValue(currentAutomationType(), polyline->activePointValue(),
                                                                  notationConfiguration()->currentAutomationMidiCc())
                                      : QString());
    });

    QObject::connect(polyline, &muse::uicomponents::PolylinePlot::pointAdded,
                     [this, key, polyline, system, staffCanvasRect](qreal x, qreal y, bool completed) {
        // A plain click on the line creates the point right away: drawn to the dynamics' levels like a drag
        y = snappedDynamicsY(currentAutomationType(), polyline->height(), y, QGuiApplication::queryKeyboardModifiers() & Qt::AltModifier);

        if (completed) {
            requestAddPoint(key, x, y);
            return;
        }

        const std::optional<int> tick = automationTickFromCanvasX(system, staffCanvasRect, x);
        if (!tick) {
            return;
        }

        QVector<PointData>& pointsData = m_pointsDataByStaff[key];
        int insertIdx = 0;
        while (insertIdx < pointsData.size() && pointsData.at(insertIdx).tick < *tick) {
            ++insertIdx;
        }
        pointsData.insert(insertIdx, PointData(-1, *tick, { x, y }, PointData::PointType::BOTH));

        QVector<QPointF> points = polyline->points();
        points.insert(insertIdx, { x, y });
        polyline->setPoints(points);
        applyPointFlags(polyline, key);
        applyPolylineColorsUnderLine(polyline, key);
    });

    QObject::connect(polyline, &muse::uicomponents::PolylinePlot::pointRemoved,
                     [this, key](int pointIdx, bool completed) {
        if (!completed) {
            return;
        }
        const auto pointsDataIt = m_pointsDataByStaff.find(key);
        IF_ASSERT_FAILED(pointsDataIt != m_pointsDataByStaff.end() && pointIdx >= 0 && pointIdx < pointsDataIt->second.size()) {
            return;
        }
        requestRemovePoint(pointsDataIt->second.at(pointIdx), key);
    });

    QObject::connect(polyline, &muse::uicomponents::PolylinePlot::dragCancelled, [this]() {
        cancelGroupDrag();
    });

    // Every press ends here: a group drag not committed by then (e.g. a click with a little jitter) is dropped
    QObject::connect(polyline, &muse::uicomponents::PolylinePlot::interactionFinished, [this]() {
        cancelGroupDrag();
    });

    QObject::connect(polyline, &muse::uicomponents::PolylinePlot::pointValueEditRequested,
                     [this, key, polyline](int pointIdx, const QPointF& positionPx) {
        // Not on dynamics: their points are placed by dragging, drawn to the dynamics' own levels
        const AutomationType type = currentAutomationType();
        if (type == AutomationType::Unknown || type == AutomationType::Dynamics || !polylinePointIndexIsValid(polyline, pointIdx)) {
            return;
        }

        const QPointF globalPos = polyline->mapToGlobal(positionPx);

        // The editor runs outside of the polyline's mouse event handler
        muse::async::Async::call(this, [this, key, pointIdx, globalPos]() {
            showPointValueEditor(key, pointIdx, globalPos);
        });
    });

    // Previewed by PolylinePlot itself while dragging, committed on release
    QObject::connect(polyline, &muse::uicomponents::PolylinePlot::segmentBendMoved,
                     [this, key](int segmentIdx, qreal value, bool completed) {
        if (completed) {
            requestSegmentBend(key, segmentIdx, value);
        }
    });

    return polyline;
}

QVector<NotationAutomationController::PointData> NotationAutomationController::pointsDataInStaff(const System* system,
                                                                                                 const mu::engraving::Staff* staff,
                                                                                                 const muse::RectF& sysStaffCanvasRect)
const
{
    QVector<PointData> points;
    IF_ASSERT_FAILED(system && staff && score() && automationData() && sysStaffCanvasRect.width() > 0) {
        return points;
    }

    const AutomationType type = currentAutomationType();
    const mu::engraving::AutomationCurveKey key = currentCurveKeyFor(staff);
    const mu::engraving::AutomationCurve& curve = displayedCurve(key);

    const int systemStartTick = system->first()->tick().ticks();
    const int systemEndTick = system->last()->endTick().ticks();

    // Neighbors outside the system can't be placed via their own segment: it belongs to another system
    // (in Page view, typically the next line, back on the left of the page). They're only needed for the
    // line to enter/leave the system with the right slope (or bend), so they're laid out by extrapolating this
    // system's tick-to-x ratio past its edges...
    const Measure* firstMeasure = system->firstMeasure();
    const Segment* firstSeg = firstMeasure ? firstMeasure->first(mu::engraving::SegmentType::Duration) : nullptr;
    const Segment* lastSeg = lastSegmentOfSystem(system);
    const double systemStartX = firstSeg ? firstSeg->canvasX() : sysStaffCanvasRect.x();
    const double systemEndX = lastSeg ? lastSeg->canvasX() + lastSeg->width() : sysStaffCanvasRect.x() + sysStaffCanvasRect.width();
    const double pxPerTick = systemEndTick > systemStartTick ? (systemEndX - systemStartX) / (systemEndTick - systemStartTick) : 0.0;

    const auto toStaffX = [&](double canvasX) {
        return (canvasX - sysStaffCanvasRect.x()) / sysStaffCanvasRect.width();
    };

    const auto displayY = [type](mu::engraving::real_t value) {
        // Point in/out values are rescaled to the display range - higher value == lower Y...
        return 1.0 - automationValueToDisplay(type, value);
    };

    struct OutsidePoint {
        int tick = 0;
        QPointF point;
    };
    std::optional<OutsidePoint> leftOutside;
    std::optional<OutsidePoint> rightOutside;

    // The curve is stored in expanded (repeats unrolled) utick space, the score shows each tick once: each point
    // is shown at its tick on the first pass through it (edits are mirrored to every pass, see utickForTick)
    struct TickPoint {
        int tick = 0;
        mu::engraving::real_t resolvedIn = 0.;
        mu::engraving::real_t outValue = 0.;
    };
    std::vector<TickPoint> tickPoints;
    for (auto curveIt = curve.cbegin(); curveIt != curve.cend(); ++curveIt) {
        if (const std::optional<int> tick = firstPassTickForUtick(curveIt->first)) {
            tickPoints.push_back({ *tick, mu::engraving::resolveInValue(curve, curveIt), curveIt->second.value.outValue });
        }
    }
    std::stable_sort(tickPoints.begin(), tickPoints.end(), [](const TickPoint& a, const TickPoint& b) { return a.tick < b.tick; });

    for (const TickPoint& tickPoint : tickPoints) {
        const int tick = tickPoint.tick;
        const mu::engraving::real_t resolvedIn = tickPoint.resolvedIn;
        const mu::engraving::real_t outValue = tickPoint.outValue;

        if (tick < systemStartTick) {
            // The value the line leaves this point with
            leftOutside = OutsidePoint { tick, QPointF(toStaffX(systemStartX - (systemStartTick - tick) * pxPerTick),
                                                       displayY(outValue)) };
            continue;
        }

        if (tick >= systemEndTick) {
            // The value the line arrives at this point with
            rightOutside = OutsidePoint { tick, QPointF(toStaffX(systemEndX + (tick - systemEndTick) * pxPerTick),
                                                        displayY(resolvedIn)) };
            break;
        }

        const Segment* seg = score()->tick2leftSegmentMM(Fraction::fromTicks(tick));
        if (!seg) { //! FIXME: fix automation curve on measure repeats
            continue;
        }

        // The point's tick may not exactly match that of a segment. For this reason we can only calculate the x position
        // of our points based on a "tickRatio". This ratio is based on the "tick difference" between the point tick and
        // the segment's tick, and the duration of the segment (in ticks)...
        const int tickDiff = tick - seg->tick().ticks();
        const double tickRatio = static_cast<double>(tickDiff) / seg->ticks().ticks();
        const double pointXInStaff = toStaffX(seg->canvasX() + tickRatio * seg->width());

        if (resolvedIn == outValue) {
            points.emplace_back(PointData(-1, tick, QPointF(pointXInStaff, displayY(resolvedIn)), PointData::PointType::BOTH));
        } else {
            points.emplace_back(PointData(-1, tick, QPointF(pointXInStaff, displayY(resolvedIn)), PointData::PointType::IN));
            points.emplace_back(PointData(-1, tick, QPointF(pointXInStaff, displayY(outValue)), PointData::PointType::OUT));
        }
    }

    // ...and marked outside, for PolylinePlot to hide them
    if (leftOutside) {
        points.prepend(PointData(-1, leftOutside->tick, leftOutside->point, PointData::PointType::BOTH, true));
    }

    if (rightOutside) {
        points.append(PointData(-1, rightOutside->tick, rightOutside->point, PointData::PointType::BOTH, true));
    }

    for (int i = 0; i < points.size(); ++i) {
        points[i].polylinePointIndex = i;
    }

    return points;
}

void NotationAutomationController::applyPolylineStyle(PolylinePlot* polyline, const SysStaffKey& key) const
{
    IF_ASSERT_FAILED(polyline) {
        return;
    }

    polyline->setLineWidth(POLYLINE_LINE_WIDTH);
    polyline->setDrawBackground(false);

    polyline->setBaselineN(defaultValueFor(currentAutomationType()));

    // Bends apply to the values as played: shown through the same scale as the points (a point's y is
    // 1 - its display value, see pointsDataInStaff), which isn't linear for Volume (fader curve) and Tempo
    const AutomationType type = currentAutomationType();
    if (type == AutomationType::Volume || type == AutomationType::Tempo) {
        polyline->setValueMapping(
            [type](qreal y) { return static_cast<qreal>(automationValueFromDisplay(type, 1.0 - y)); },
            [type](qreal value) { return 1.0 - automationValueToDisplay(type, muse::real_t(value)); });
    } else {
        polyline->setValueMapping({}, {});
    }

    polyline->setGhostPointsEnabled(false);
    polyline->setSelectedPointsEnabled(true);

    PolylinePointStyle* standard = polyline->standardPointStyle();
    standard->setCenterRadius(POLYLINE_STANDARD_CENTER_RADIUS);
    standard->setOutlineWidth(POLYLINE_LINE_WIDTH);

    standard->setCenterRadiusHovered(POLYLINE_HOVERED_CENTER_RADIUS);
    standard->setOutlineWidthHovered(POLYLINE_LINE_WIDTH);

    PolylinePointStyle* selected = polyline->selectedPointStyle();
    selected->setCenterRadius(POLYLINE_SELECTED_CENTER_RADIUS);
    selected->setMiddleRingWidth(POLYLINE_SELECTED_MIDDLE_RING_WIDTH);
    selected->setOutlineWidth(POLYLINE_LINE_WIDTH);

    selected->setCenterRadiusHovered(POLYLINE_SELECTED_CENTER_RADIUS);
    selected->setMiddleRingWidthHovered(POLYLINE_SELECTED_MIDDLE_RING_WIDTH);
    selected->setOutlineWidthHovered(POLYLINE_LINE_WIDTH);

    applyPolylineColors(polyline, key);
}

void NotationAutomationController::applyPolylineColors(PolylinePlot* polyline, const SysStaffKey& key) const
{
    IF_ASSERT_FAILED(polyline) {
        return;
    }

    const QColor lineColor = inversionRelativeColor(muse::ui::FONT_PRIMARY_COLOR);
    polyline->setLineColor(lineColor);

    const QColor foregroundColor = notationConfiguration()->foregroundColor();

    PolylinePointStyle* standard = polyline->standardPointStyle();
    standard->setCenterColor(foregroundColor);
    standard->setOutlineColor(lineColor);

    standard->setCenterColorHovered(inversionRelativeColor(muse::ui::BUTTON_COLOR));
    standard->setOutlineColorHovered(lineColor);

    QColor selectionColor = engravingConfiguration()->selectionColor().toQColor();

    PolylinePointStyle* selected = polyline->selectedPointStyle();
    selected->setCenterColor(selectionColor);
    selected->setMiddleRingColor(foregroundColor);
    selected->setOutlineColor(lineColor);

    selectionColor.setAlpha(POLYLINE_SELECTED_HOVERED_ALPHA);
    selected->setCenterColorHovered(selectionColor);
    selected->setMiddleRingColorHovered(foregroundColor);
    selected->setOutlineColorHovered(lineColor);

    // The drag tooltip's chip needs to stay legible against whatever the score's own background
    // currently is (light/dark/high-contrast paper, or a user-customized color) - matches the
    // note-velocity drag tooltip's identical luminance-based approach for visual consistency.
    const QColor background = notationConfiguration() ? notationConfiguration()->backgroundColor() : QColor(Qt::white);
    const double luminance = 0.299 * background.red() + 0.587 * background.green() + 0.114 * background.blue();
    if (luminance > 128.0) {
        polyline->setValueLabelColors(QColor(40, 40, 40, 235), QColor(255, 255, 255));
    } else {
        polyline->setValueLabelColors(QColor(235, 235, 235, 235), QColor(20, 20, 20));
    }

    applyPolylineColorsUnderLine(polyline, key);
}

void NotationAutomationController::applyPolylineColorsUnderLine(PolylinePlot* polyline, const SysStaffKey& key) const
{
    IF_ASSERT_FAILED(polyline) {
        return;
    }

    const auto pointsDataIt = m_pointsDataByStaff.find(key);
    IF_ASSERT_FAILED(pointsDataIt != m_pointsDataByStaff.end()) {
        return;
    }

    // TODO: Cache these colors?
    const QColor allVoicesColor = engravingConfiguration()->selectionColor(mu::engraving::VOICES).toQColor();

    QColor generatedColor = allVoicesColor;
    generatedColor.setAlpha(POLYLINE_GENERATED_AREA_ALPHA);

    QColor editedColor = allVoicesColor;
    editedColor.setAlpha(POLYLINE_EDITED_AREA_ALPHA);

    const QVector<PointData>& pointsData = pointsDataIt->second;

    QVector<QColor> colorsUnderLine;
    colorsUnderLine.reserve(pointsData.size() + 1); // +1 for the "trailing color" (see below)

    bool prevPointGenerated = true;
    for (const PointData& pointData : pointsData) {
        //! NOTE: The following can be null for newly created (always non-generated) points because they're not in the model yet
        const mu::engraving::AutomationPoint* automationPoint = automationPointAt(key, pointData.tick);
        const bool currPointGenerated = automationPoint && automationPoint->generated;

        // Colors either side of an edited point should use the "edited color"...
        const bool useEditedColor = !prevPointGenerated || !currPointGenerated;
        colorsUnderLine.emplace_back(useEditedColor ? editedColor : generatedColor);

        prevPointGenerated = currPointGenerated;
    }

    // This is the trailing color (after the last point) - it always follows the color of the last point...
    colorsUnderLine.emplace_back(prevPointGenerated ? generatedColor : editedColor);

    polyline->setColorsUnderLine(colorsUnderLine);
}

QColor NotationAutomationController::inversionRelativeColor(const muse::ui::ThemeStyleKey& key) const
{
    // This method is necessary because automation colors are relative to the score inversion as opposed to the current UI theme. In an
    // inverted score we use dark theme colors, and in a non-inverted score we use light theme colors...

    // TODO: High contrast colors should actually be fully customizable (issue #34154)
    const bool isHighContrast = uiConfiguration()->isHighContrast();
    const muse::ui::ThemeCode lightTheme = isHighContrast ? muse::ui::HIGH_CONTRAST_WHITE_THEME_CODE : muse::ui::LIGHT_THEME_CODE;
    const muse::ui::ThemeCode darkTheme = isHighContrast ? muse::ui::HIGH_CONTRAST_BLACK_THEME_CODE : muse::ui::DARK_THEME_CODE;

    const bool inverted = notationConfiguration()->shouldInvertScore();

    const muse::ui::ThemeList& themes = uiConfiguration()->themes();
    for (const muse::ui::ThemeInfo& theme : themes) {
        // Set line colors based on score inversion as opposed to current UI themes...
        const bool foundLightTheme = !inverted && theme.codeKey == lightTheme;
        const bool foundDarkTheme = inverted && theme.codeKey == darkTheme;
        if (foundLightTheme || foundDarkTheme) {
            return theme.values[key].toString();
        }
    }

    ASSERT_X("Error scanning themes");

    return QColor();
}

void NotationAutomationController::updatePolylinesGeometry()
{
    const bool visible = automation() && automation()->isAutomationModeEnabled();

    for (const auto& [key, polylines] : m_stavesToLinesMap) {
        IF_ASSERT_FAILED(key.isValid() && !polylines.empty()) {
            continue;
        }

        // TODO: Staves can have multiple polylines due to horizontal frames, at the moment we're
        // providing a single polyline over the entire staff...
        PolylinePlot* polyline = *polylines.begin();
        polyline->setVisible(visible);
        if (!visible) {
            continue;
        }

        const SysStaff* sysStaff = key.system->staff(key.staffIdx);
        IF_ASSERT_FAILED(sysStaff) {
            continue;
        }

        //! NOTE: Here we should only update properties of the polyline that change relative to the view matrix. Polyline points are
        //! placed relative to the polylines themselves, and thus do not need to be modified in here...
        muse::RectF staffCanvasRect = sysStaff->bbox().translated(key.system->canvasPos());
        staffCanvasRect = m_viewMatrix.map(staffCanvasRect);

        polyline->setWidth(staffCanvasRect.width());
        polyline->setHeight(staffCanvasRect.height());
        polyline->setX(staffCanvasRect.x());
        polyline->setY(staffCanvasRect.y());

        applyPolylineColors(polyline, key);
    }
}

void NotationAutomationController::updatePolylinesColors()
{
    for (const auto& [key, polylines] : m_stavesToLinesMap) {
        IF_ASSERT_FAILED(key.isValid() && !polylines.empty()) {
            continue;
        }
        // TODO: Staves can have multiple polylines due to horizontal frames, at the moment we're
        // providing a single polyline over the entire staff...
        PolylinePlot* polyline = *polylines.begin();
        applyPolylineColors(polyline, key);
    }
}

void NotationAutomationController::setViewMatrix(const muse::draw::Transform& viewMatrix)
{
    if (viewMatrix == m_viewMatrix) {
        return;
    }
    m_viewMatrix = viewMatrix;

    if (automation() && automation()->isAutomationModeEnabled()) {
        updatePolylinesGeometry();
    }
}

void NotationAutomationController::onCurrentNotationChanged()
{
    m_pendingChanges.clear();
    m_pendingScoreState = PendingScoreState();
    rebuildAllPolylines();

    if (automationData()) {
        automationData()->changed().onReceive(this, [this](const mu::engraving::AutomationChanges& changes) {
            mergePendingChanges(changes);
            scheduleUpdate();
        }, Asyncable::Mode::SetReplace /* FIXME */);
    }

    if (score()) {
        score()->changesChannel().onReceive(this, [this](const mu::engraving::ScoreChanges& changes) {
            mergePendingScoreChanges(changes);
            scheduleUpdate();
        }, Asyncable::Mode::SetReplace /* FIXME */);
    }

    // A MIDI CC take being recorded is drawn live over its curve (see displayedCurve)
    if (automation()) {
        automation()->recordingPreviewChanged().onReceive(this, [this](const mu::engraving::AutomationChanges& changes) {
            mergePendingChanges(changes);
            scheduleUpdate();
        }, Asyncable::Mode::SetReplace /* FIXME */);
    }

    if (currentNotation()) {
        currentNotation()->viewModeChanged().onNotify(this, [this]() {
            rebuildAllPolylines();
        }, Asyncable::Mode::SetReplace /* FIXME */);
    }

    // Points inside MuseScore's range selection are shown selected (and move together)
    if (currentNotation()) {
        currentNotation()->interaction()->selectionChanged().onNotify(this, [this]() {
            refreshGroupFlags();
        }, Asyncable::Mode::SetReplace /* FIXME */);
    }

    // MIDI CC curves are only drawn on VST instruments, so switching an instrument's sound source changes which staves get one
    const project::INotationProjectPtr project = globalContext()->currentProject();
    if (project && project->audioSettings()) {
        project->audioSettings()->trackInputParamsChanged().onReceive(this, [this](const InstrumentTrackId&) {
            if (currentAutomationType() == AutomationType::MidiCC) {
                scheduleRebuild();
            }
        }, Asyncable::Mode::SetReplace /* FIXME */);
    }
}

void NotationAutomationController::scheduleRebuild()
{
    if (m_rebuildScheduled) {
        return;
    }
    m_rebuildScheduled = true;

    muse::async::Async::call(this, [this]() {
        m_rebuildScheduled = false;
        rebuildAllPolylines();
    });
}

void NotationAutomationController::mergePendingScoreChanges(const mu::engraving::ScoreChanges& changes)
{
    const bool firstChange = !m_pendingScoreState.hasChanges;
    m_pendingScoreState.hasChanges = true;
    m_pendingScoreState.structural = m_pendingScoreState.structural || isStructuralChange(changes);

    if (!changes.isValidBoundary()) {
        m_pendingScoreState.boundary = std::nullopt;
        return;
    }
    if (!firstChange && !m_pendingScoreState.boundary) {
        return;
    }

    const TickStaffRange changeRange { changes.tickFrom, changes.tickTo, changes.staffIdxFrom, changes.staffIdxTo };
    TickStaffRange range = m_pendingScoreState.boundary.value_or(changeRange);
    range.tickFrom = std::min(range.tickFrom, changeRange.tickFrom);
    range.tickTo = std::max(range.tickTo, changeRange.tickTo);
    range.staffIdxFrom = std::min(range.staffIdxFrom, changeRange.staffIdxFrom);
    range.staffIdxTo = std::max(range.staffIdxTo, changeRange.staffIdxTo);
    m_pendingScoreState.boundary = range;
}

void NotationAutomationController::scheduleUpdate()
{
    if (m_updateScheduled) {
        return;
    }
    m_updateScheduled = true;

    muse::async::Async::call(this, [this]() {
        m_updateScheduled = false;
        processPendingChanges();
    });
}

void NotationAutomationController::processPendingChanges()
{
    if (!m_pendingScoreState.hasChanges && m_pendingChanges.isEmpty()) {
        return;
    }
    const PendingScoreState scoreState = m_pendingScoreState;
    m_pendingScoreState = PendingScoreState();

    const bool automationVisible = automation() && automation()->isAutomationModeEnabled();

    if (scoreState.structural) {
        if (!automationVisible) {
            // Nothing visible right now; defer the rebuild until automation mode is enabled again
            m_pendingChanges.isFullReset = true;
            return;
        }
        rebuildAllPolylines();
        m_pendingChanges.clear();
        return;
    }

    if (!automationVisible) {
        // Nothing visible right now; m_pendingChanges keeps accumulating for next time
        return;
    }

    if (!m_pendingChanges.isEmpty()) {
        applyAutomationChanges(m_pendingChanges);
        m_pendingChanges.clear();
        return;
    }

    // No automation-data change and nothing structural - just layout drift
    // (e.g. measure widths shifted); refresh point positions using the batch's own range
    for (const auto& [key, polylines] : m_stavesToLinesMap) {
        IF_ASSERT_FAILED(key.isValid()) {
            continue;
        }
        const Staff* staff = score()->staff(key.staffIdx);
        if (!staff) {
            continue;
        }

        const int systemStartTick = key.system->first()->tick().ticks();
        const int systemEndTick = key.system->last()->endTick().ticks();
        if (scoreState.boundary) {
            const TickStaffRange& range = *scoreState.boundary;
            if (staff->idx() < range.staffIdxFrom || staff->idx() > range.staffIdxTo) {
                continue;
            }
            if (systemEndTick < range.tickFrom || systemStartTick > range.tickTo) {
                continue;
            }
        }

        updateStaffPoints(key);
    }

    updatePolylinesGeometry();
}

void NotationAutomationController::rebuildAllPolylines()
{
    // Its point (and system) are about to go
    closePointValueEditor();

    // TODO: More efficient if we don't clear/recreate the polylines every time...
    for (const auto& [staff, polylines] : m_stavesToLinesMap) {
        for (PolylinePlot* polyline : polylines) {
            delete polyline;
        }
    }
    m_stavesToLinesMap.clear();
    m_pointsDataByStaff.clear();
    m_groupDrag = GroupDrag(); // its polylines are gone

    if (!score()) {
        // Happens on close...
        return;
    }

    for (const System* system : score()->systems()) {
        SysStaffToPolylinesMap systemPolylines = createPolylinesForSystem(system);
        m_stavesToLinesMap.merge(systemPolylines);

        // merge() leaves an element whose key is already there behind: such a polyline would never be positioned nor
        // deleted - stuck on screen. Keys are unique per system, so this is only a safety net
        for (const auto& [_, polylines] : systemPolylines) {
            for (PolylinePlot* polyline : polylines) {
                delete polyline;
            }
        }
    }

    updatePolylinesGeometry();
}

//! NOTE: the whole system is recomputed - a system only holds a handful of points, and its edge points
//! (see pointsDataInStaff) depend on its neighbors, whatever range actually changed
void NotationAutomationController::updateStaffPoints(const SysStaffKey& key)
{
    auto mapIt = m_stavesToLinesMap.find(key);
    IF_ASSERT_FAILED(key.isValid() && mapIt != m_stavesToLinesMap.end() && !mapIt->second.empty()) {
        return;
    }
    PolylinePlot* polyline = *mapIt->second.begin();

    const Staff* staff = score() ? score()->staff(key.staffIdx) : nullptr;
    const SysStaff* sysStaff = key.system ? key.system->staff(key.staffIdx) : nullptr;
    IF_ASSERT_FAILED(staff && sysStaff) {
        return;
    }

    const muse::RectF staffCanvasRect = sysStaff->bbox().translated(key.system->canvasPos());
    QVector<PointData>& pointsData = m_pointsDataByStaff[key];
    pointsData = pointsDataInStaff(key.system, staff, staffCanvasRect);

    QVector<QPointF> points;
    points.reserve(pointsData.size());
    for (const PointData& pointData : pointsData) {
        points.push_back(pointData.qPointF);
    }
    polyline->setPoints(points);
    applyPointFlags(polyline, key);
    applyPolylineColorsUnderLine(polyline, key);
    polyline->update();
}

//! NOTE: per point: locked - generated from the score (e.g. a tempo marking's), so it can't be removed or moved in
//! time: where one overlaps a point of the user's own, a click must pick the user's; hidden - outside of the system.
//! Per segment: its bend, for the bendable curves
void NotationAutomationController::applyPointFlags(PolylinePlot* polyline, const SysStaffKey& key) const
{
    const auto pointsDataIt = m_pointsDataByStaff.find(key);
    if (pointsDataIt == m_pointsDataByStaff.end()) {
        return;
    }

    const QVector<PointData>& pointsData = pointsDataIt->second;

    QVector<bool> hidden;
    hidden.reserve(pointsData.size());
    for (const PointData& pointData : pointsData) {
        hidden.push_back(pointData.outside);
    }
    polyline->setHiddenPoints(hidden);

    // One lookup per point, shared by the bends and the locked flags. A pending point (being added right now) isn't
    // in the curve yet, but belongs to the user
    std::vector<const mu::engraving::AutomationPoint*> automationPoints;
    automationPoints.reserve(pointsData.size());
    for (const PointData& pointData : pointsData) {
        automationPoints.push_back(pointData.polylinePointIndex < 0 ? nullptr : automationPointAt(key, pointData.tick));
    }

    const AutomationType type = currentAutomationType();
    QVector<muse::uicomponents::SegmentBend> bends;
    if (isBendable(type) && pointsData.size() > 1) {
        bends.reserve(pointsData.size() - 1);
        for (int i = 1; i < pointsData.size(); ++i) {
            // The bend of a segment belongs to the point it arrives at (its in value's ease)
            muse::uicomponents::SegmentBend bend;
            const PointData& arrival = pointsData.at(i);
            const mu::engraving::AutomationPoint* point = arrival.pointType == PointData::PointType::OUT
                                                          ? nullptr // the jump of a point with different in/out values
                                                          : automationPoints.at(i);
            if (point) {
                if (const std::optional<mu::engraving::AutomationPoint::Ease> ease = mu::engraving::ease(*point)) {
                    bend.t = ease->t.raw();
                    bend.value = ease->value.raw();

                    // A tempo past the top of the display range is shown clamped there: its bend would be drawn and
                    // dragged over another value range than the one played
                    const bool isPastDisplayRange = type == AutomationType::Tempo
                                                    && (pointsData.at(i - 1).qPointF.y() <= 0.0 || arrival.qPointF.y() <= 0.0);
                    bend.editable = !isScoreDrivenPoint(point) && !isPastDisplayRange;
                }
            }
            bends.push_back(bend);
        }
    }
    polyline->setSegmentBends(bends);

    QVector<bool> locked;
    locked.reserve(pointsData.size());
    for (int i = 0; i < pointsData.size(); ++i) {
        locked.push_back(pointsData.at(i).polylinePointIndex >= 0 && isScoreDrivenPoint(automationPoints.at(i)));
    }
    polyline->setLockedPoints(locked);

    QVector<bool> groupSelected;
    groupSelected.reserve(pointsData.size());
    for (int i = 0; i < pointsData.size(); ++i) {
        groupSelected.push_back(isGroupSelectable(key, pointsData.at(i), automationPoints.at(i)));
    }
    polyline->setGroupSelectedPoints(groupSelected);
}

void NotationAutomationController::applyGroupFlags(PolylinePlot* polyline, const SysStaffKey& key) const
{
    const auto pointsDataIt = m_pointsDataByStaff.find(key);
    if (!polyline || pointsDataIt == m_pointsDataByStaff.end()) {
        return;
    }

    QVector<bool> groupSelected;
    groupSelected.reserve(pointsDataIt->second.size());
    for (const PointData& pointData : pointsDataIt->second) {
        groupSelected.push_back(isGroupSelected(key, pointData));
    }
    polyline->setGroupSelectedPoints(groupSelected);
}

//! NOTE: only while automation is shown - enabling it refreshes them
void NotationAutomationController::refreshGroupFlags()
{
    if (!automation() || !automation()->isAutomationModeEnabled()) {
        return;
    }

    for (const auto& [key, polylines] : m_stavesToLinesMap) {
        if (!polylines.empty()) {
            applyGroupFlags(*polylines.begin(), key);
        }
    }
}

//! NOTE: whether MuseScore's range selection covers this tick of this staff's curve
bool NotationAutomationController::isInRangeSelection(const Staff* staff, int tick) const
{
    const INotationPtr notation = currentNotation();
    const INotationSelectionPtr selection = notation ? notation->interaction()->selection() : nullptr;
    if (!selection || !selection->isRange() || !staff) {
        return false;
    }

    const INotationSelectionRangePtr range = selection->range();
    if (tick < range->startTick().ticks() || tick >= range->endTick().ticks()) {
        return false;
    }

    const AutomationCurveKey key = currentCurveKeyFor(staff);
    if (key.isGlobal()) {
        return true; // e.g. Tempo: any selected staff
    }

    const staff_idx_t from = range->startStaffIndex();
    const staff_idx_t to = range->endStaffIndex(); // exclusive
    if (key.trackId()) {
        // An instrument's curve: any of its staves
        for (const Staff* partStaff : staff->part()->staves()) {
            if (partStaff->idx() >= from && partStaff->idx() < to) {
                return true;
            }
        }
        return false;
    }

    return staff->idx() >= from && staff->idx() < to;
}

//! NOTE: a point of the user's own inside the range selection. Not a tempo past the top of the display range: shown
//! clamped there, moving it by the group's offset would drop its actual value
bool NotationAutomationController::isGroupSelectable(const SysStaffKey& key, const PointData& pointData,
                                                     const mu::engraving::AutomationPoint* point) const
{
    if (pointData.outside || pointData.polylinePointIndex < 0 || isScoreDrivenPoint(point)) {
        return false;
    }

    if (currentAutomationType() == AutomationType::Tempo && pointData.qPointF.y() <= 0.0) {
        return false;
    }

    const Staff* staff = score() ? score()->staff(key.staffIdx) : nullptr;
    return isInRangeSelection(staff, pointData.tick);
}

bool NotationAutomationController::isGroupSelected(const SysStaffKey& key, const PointData& pointData) const
{
    if (pointData.outside || pointData.polylinePointIndex < 0) {
        return false;
    }

    return isGroupSelectable(key, pointData, automationPointAt(key, pointData.tick));
}

//! NOTE: the selection can't change during the drag: its points are gathered once
void NotationAutomationController::startGroupDrag()
{
    m_groupDrag = GroupDrag();
    m_groupDrag.active = true;

    for (const auto& [key, polylines] : m_stavesToLinesMap) {
        const auto pointsDataIt = m_pointsDataByStaff.find(key);
        if (polylines.empty() || pointsDataIt == m_pointsDataByStaff.end()) {
            continue;
        }

        std::vector<int> selected;
        for (int i = 0; i < pointsDataIt->second.size(); ++i) {
            if (isGroupSelected(key, pointsDataIt->second.at(i))) {
                selected.push_back(i);
            }
        }

        const QVector<QPointF> points = (*polylines.begin())->points();
        if (!selected.empty() && points.size() == pointsDataIt->second.size()) {
            m_groupDrag.origins.emplace(key, points);
            m_groupDrag.selected.emplace(key, std::move(selected));
        }
    }
}

//! NOTE: the same vertical offset for every selected point, on every system, each clamped to the display range
void NotationAutomationController::previewGroupDrag(qreal delta)
{
    for (const auto& [key, selected] : m_groupDrag.selected) {
        const auto linesIt = m_stavesToLinesMap.find(key);
        if (linesIt == m_stavesToLinesMap.end() || linesIt->second.empty()) {
            continue;
        }

        QVector<QPointF> points = m_groupDrag.origins.at(key);
        for (const int i : selected) {
            points[i].setY(std::clamp(points[i].y() + delta, 0.0, 1.0));
        }

        PolylinePlot* polyline = *linesIt->second.begin();
        polyline->setPoints(points);
        applyPolylineColorsUnderLine(polyline, key);
        polyline->update();
    }
}

void NotationAutomationController::commitGroupDrag(qreal delta)
{
    const AutomationType type = currentAutomationType();

    // Several polyline points can stand for the same curve point (its in and out values)
    std::map<AutomationCurveKey, std::map<int /*utick*/, mu::engraving::AutomationPoint> > editedPoints;

    for (const auto& [key, selected] : m_groupDrag.selected) {
        const auto pointsDataIt = m_pointsDataByStaff.find(key);
        const Staff* staff = score() ? score()->staff(key.staffIdx) : nullptr;
        const QVector<QPointF>& origins = m_groupDrag.origins.at(key);
        if (!staff || pointsDataIt == m_pointsDataByStaff.end() || origins.size() != pointsDataIt->second.size()) {
            continue; // rebuilt under the drag
        }

        const AutomationCurveKey curveKey = currentCurveKeyFor(staff);
        for (const int i : selected) {
            const PointData& pointData = pointsDataIt->second.at(i);
            const mu::engraving::AutomationPoint* existing = automationPointAt(key, pointData.tick);
            if (!existing) {
                continue;
            }

            const bool isIn = pointData.pointType == PointData::PointType::IN;
            const mu::engraving::real_t referenceValue = isIn
                                                         ? mu::engraving::resolveInValue(automationData()->curve(curveKey),
                                                                                         automationData()->curve(curveKey).find(
                                                                                             utickForTick(pointData.tick)))
                                                         : existing->value.outValue;
            const double display = 1.0 - std::clamp(origins.at(i).y() + delta, 0.0, 1.0);
            const mu::engraving::real_t value = automationValueFromDisplay(type, display, referenceValue);
            if (muse::RealIsEqual(value, referenceValue)) {
                continue; // unchanged (e.g. already at the edge): left as it is
            }

            const int utick = utickForTick(pointData.tick);
            auto it = editedPoints[curveKey].try_emplace(utick, *existing).first;
            setEditedValue(it->second, pointData.pointType, value);
        }
    }

    std::vector<std::pair<AutomationCurveKey, mu::engraving::AutomationPointEdits> > editsByCurve;
    for (const auto& [curveKey, points] : editedPoints) {
        mu::engraving::AutomationPointEdits edits;
        for (const auto& [utick, point] : points) {
            edits.push_back({ utick, SetPoint { point } });
        }
        if (!edits.empty()) {
            editsByCurve.emplace_back(curveKey, std::move(edits));
        }
    }

    // Nothing to write: back to the points as they are (no rebuild here - this runs inside the dragged polyline's
    // own mouse event handling)
    if (editsByCurve.empty() || !automation()) {
        cancelGroupDrag();
        return;
    }

    m_groupDrag = GroupDrag();
    automation()->editPoints(editsByCurve);
}

void NotationAutomationController::cancelGroupDrag()
{
    if (!m_groupDrag.active) {
        return;
    }

    const GroupDrag groupDrag = std::move(m_groupDrag);
    m_groupDrag = GroupDrag();

    for (const auto& [key, points] : groupDrag.origins) {
        const auto it = m_stavesToLinesMap.find(key);
        if (it != m_stavesToLinesMap.end() && !it->second.empty()) {
            PolylinePlot* polyline = *it->second.begin();
            polyline->setPoints(points);
            applyPolylineColorsUnderLine(polyline, key);
        }
    }
}

void NotationAutomationController::mergePendingChanges(const mu::engraving::AutomationChanges& changes)
{
    if (changes.isFullReset) {
        m_pendingChanges.isFullReset = true;
        return;
    }
    for (const mu::engraving::AutomationCurveKey& key : changes.affectedKeys) {
        m_pendingChanges.extend(key, changes.tickFrom, changes.tickTo);
    }
}

void NotationAutomationController::applyAutomationChanges(const mu::engraving::AutomationChanges& changes)
{
    if (changes.isFullReset || !score()) {
        rebuildAllPolylines();
        return;
    }

    std::set<muse::ID> affectedStaffIds;
    std::set<mu::engraving::InstrumentTrackId> affectedTrackIds;
    bool globalAffected = false;
    for (const mu::engraving::AutomationCurveKey& key : changes.affectedKeys) {
        if (const std::optional<muse::ID> staffId = key.staffId()) {
            affectedStaffIds.insert(*staffId);
        } else if (const std::optional<mu::engraving::InstrumentTrackId> trackId = key.trackId()) {
            affectedTrackIds.insert(*trackId);
        } else if (key.isGlobal()) {
            globalAffected = true;
        }
    }

    // Only touch the staves that were actually affected and whose system overlaps the changed tick
    // range, and only recompute points within that range, rather than the whole score or even the
    // whole staff
    const bool hasRepeats = score()->masterScore()->expandedRepeatList().size() > 1;

    for (const auto& [key, polylines] : m_stavesToLinesMap) {
        IF_ASSERT_FAILED(key.isValid()) {
            continue;
        }
        const Staff* staff = score()->staff(key.staffIdx);
        if (!staff) {
            continue;
        }
        const bool staffAffected = affectedStaffIds.find(staff->id()) != affectedStaffIds.end();
        const mu::engraving::InstrumentTrackId staffTrackId { staff->part()->id(), staff->part()->instrumentId() };
        const bool trackAffected = affectedTrackIds.find(staffTrackId) != affectedTrackIds.end();
        const bool globalCurveAffected = globalAffected && currentCurveKeyFor(staff).isGlobal();
        if (!staffAffected && !trackAffected && !globalCurveAffected) {
            continue;
        }
        // A system also depends on its neighbor points outside of it (they shape its edges): its range of
        // influence reaches them, or is unbounded on a side without any neighbor (a new point could become one)
        const System* system = key.system;
        const int systemStartTick = system->first()->tick().ticks();
        const int systemEndTick = system->last()->endTick().ticks();
        int influenceFrom = std::numeric_limits<int>::min();
        int influenceTo = std::numeric_limits<int>::max();
        const auto pointsDataIt = m_pointsDataByStaff.find(key);
        if (pointsDataIt != m_pointsDataByStaff.end() && !pointsDataIt->second.isEmpty()) {
            if (pointsDataIt->second.front().tick < systemStartTick) {
                influenceFrom = pointsDataIt->second.front().tick;
            }
            if (pointsDataIt->second.back().tick >= systemEndTick) {
                influenceTo = pointsDataIt->second.back().tick;
            }
        }
        // changes' range is in expanded utick space: with repeats, it doesn't map onto a single score tick range
        if (hasRepeats || (influenceTo >= changes.tickFrom && influenceFrom <= changes.tickTo)) {
            updateStaffPoints(key);
        }
    }

    updatePolylinesGeometry();
}

namespace {
//! NOTE: Return/Enter or a click elsewhere (the editor losing focus) commits the typed value, Escape closes the editor
//! without changing anything. It's a window of its own that takes the keyboard: the score's shortcuts belong to the
//! main window (and are also kept off while typing, through ShortcutOverride)
class PointValueEditorFilter : public QObject
{
public:
    PointValueEditorFilter(QWidget* editor, QDoubleSpinBox* spinBox, std::function<void(double)> commit)
        : QObject(editor), m_editor(editor), m_spinBox(spinBox), m_commit(std::move(commit)) {}

protected:
    bool eventFilter(QObject* watched, QEvent* event) override
    {
        switch (event->type()) {
        case QEvent::ShortcutOverride:
            event->accept();
            return true;
        case QEvent::KeyPress: {
            const int key = static_cast<QKeyEvent*>(event)->key();
            if (key == Qt::Key_Return || key == Qt::Key_Enter) {
                finish(true);
                return true;
            }
            if (key == Qt::Key_Escape) {
                finish(false);
                return true;
            }
            break;
        }
        case QEvent::WindowDeactivate:
            if (watched == m_editor) {
                finish(true);
            }
            break;
        default:
            break;
        }

        return QObject::eventFilter(watched, event);
    }

private:
    void finish(bool commit)
    {
        if (m_finished) {
            return;
        }
        m_finished = true;

        if (commit) {
            m_spinBox->interpretText();
            m_commit(m_spinBox->value());
        }

        m_editor->close();
    }

    QWidget* m_editor = nullptr;
    QDoubleSpinBox* m_spinBox = nullptr;
    std::function<void(double)> m_commit;
    bool m_finished = false;
};
}

//! NOTE: a point's value as typed by the user, in its type's own unit - the same as the drag tooltip's
//! (see formattedActivePointValue()) - and its conversions from/to the display range [0, 1]
struct PointValueField {
    double min = 0.0;
    double max = 1.0;
    int decimals = 0;
    QString prefix;
    QString suffix;
    std::function<double(double)> fromDisplay;
    std::function<double(double)> toDisplay;
};

static PointValueField pointValueField(AutomationType type, int midiCc)
{
    PointValueField field;

    switch (type) {
    case AutomationType::Tempo:
        // Up to the lane's display cap, like a drag
        field.min = mu::engraving::Constants::MIN_TEMPO.toBPM().val;
        field.max = TEMPO_RANGE_MAX_BPM;
        field.suffix = QStringLiteral(" BPM");
        field.fromDisplay = [](double display) { return tempoLocalBpmToLogicalBpm(display * TEMPO_RANGE_MAX_BPM); };
        field.toDisplay = [](double value) { return tempoLogicalBpmToLocalBpm(value) / TEMPO_RANGE_MAX_BPM; };
        break;
    case AutomationType::Volume:
        field.min = VOLUME_RANGE_MIN_DB;
        field.max = VOLUME_RANGE_MAX_DB;
        field.decimals = 1;
        field.suffix = QStringLiteral(" dB");
        field.fromDisplay = [](double display) {
            return volumeLocalDbToLogicalDb(VOLUME_RANGE_MIN_DB + display * (VOLUME_RANGE_MAX_DB - VOLUME_RANGE_MIN_DB));
        };
        field.toDisplay = [](double value) {
            return (volumeLogicalDbToLocalDb(value) - VOLUME_RANGE_MIN_DB) / (VOLUME_RANGE_MAX_DB - VOLUME_RANGE_MIN_DB);
        };
        break;
    case AutomationType::Pan:
        // The Mixer's balance: -100 (left) to +100 (right)
        field.min = -100.0;
        field.max = 100.0;
        field.fromDisplay = [](double display) { return (display - 0.5) * 200.0; };
        field.toDisplay = [](double value) { return value / 200.0 + 0.5; };
        break;
    case AutomationType::MidiCC:
        field.max = MAX_MIDI_CC;
        field.prefix = QString("CC%1: ").arg(midiCc);
        field.fromDisplay = [](double display) { return display * 127.0; };
        field.toDisplay = [](double value) { return value / 127.0; };
        break;
    case AutomationType::Dynamics: // not typed (see the pointValueEditRequested handler)
    case AutomationType::Unknown:
        field.fromDisplay = [](double display) { return display; };
        field.toDisplay = [](double value) { return value; };
        break;
    }

    return field;
}

static const QString POINT_VALUE_EDITOR_FILTER_NAME = QStringLiteral("pointValueEditorFilter");

//! NOTE: without committing anything: its filter (which commits when it loses focus) goes first
void NotationAutomationController::closePointValueEditor()
{
    if (!m_pointValueEditor) {
        return;
    }

    delete m_pointValueEditor->findChild<QObject*>(POINT_VALUE_EDITOR_FILTER_NAME);
    if (QWidget* editor = qobject_cast<QWidget*>(m_pointValueEditor.data())) {
        editor->close();
    }
    m_pointValueEditor = nullptr;
}

void NotationAutomationController::showPointValueEditor(const SysStaffKey& key, int pointIdx, const QPointF& globalPos)
{
    const auto pointsDataIt = m_pointsDataByStaff.find(key);
    if (pointsDataIt == m_pointsDataByStaff.end() || pointIdx < 0 || pointIdx >= pointsDataIt->second.size()) {
        return;
    }

    const PointData pointData = pointsDataIt->second.at(pointIdx);
    if (pointData.polylinePointIndex < 0) {
        return;
    }

    const PointValueField field = pointValueField(currentAutomationType(), notationConfiguration()->currentAutomationMidiCc());

    // Point Y is inverted relative to the display range (see formattedActivePointValue())
    const double currentDisplay = std::clamp(1.0 - pointData.qPointF.y(), 0.0, 1.0);
    const double scale = std::pow(10.0, field.decimals);
    const double currentValue = std::clamp(std::round(field.fromDisplay(currentDisplay) * scale) / scale, field.min, field.max);

    closePointValueEditor();

    QWidget* editor = new QWidget(nullptr, Qt::Tool | Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint);
    editor->setAttribute(Qt::WA_DeleteOnClose);
    m_pointValueEditor = editor;

    QHBoxLayout* layout = new QHBoxLayout(editor);
    layout->setContentsMargins(2, 2, 2, 2);

    QDoubleSpinBox* spinBox = new QDoubleSpinBox(editor);
    spinBox->setDecimals(field.decimals);
    spinBox->setRange(field.min, field.max);
    spinBox->setValue(currentValue);
    spinBox->setPrefix(field.prefix);
    spinBox->setSuffix(field.suffix);
    layout->addWidget(spinBox);

    PointValueEditorFilter* filter = new PointValueEditorFilter(editor, spinBox, [this, key, pointData, currentValue, field](double typed) {
        const double value = std::clamp(typed, field.min, field.max);
        if (muse::RealIsEqual(value, currentValue)) {
            return;
        }

        const double display = std::clamp(field.toDisplay(value), 0.0, 1.0);

        // The curve may have been rebuilt while the editor was open: edit the same point, if it's still there
        const auto it = m_pointsDataByStaff.find(key);
        if (it == m_pointsDataByStaff.end()) {
            return;
        }

        for (const PointData& current : it->second) {
            if (current.tick == pointData.tick && current.pointType == pointData.pointType && current.polylinePointIndex >= 0) {
                requestEditPoint(current, key, current.qPointF.x(), 1.0 - display);
                return;
            }
        }
    });
    filter->setObjectName(POINT_VALUE_EDITOR_FILTER_NAME);
    editor->installEventFilter(filter);
    spinBox->installEventFilter(filter);

    editor->adjustSize();
    editor->move(globalPos.toPoint() + QPoint(12, -editor->height() / 2));
    editor->show();
    editor->raise();
    editor->activateWindow();
    spinBox->setFocus(Qt::PopupFocusReason);
    spinBox->selectAll();
}

bool NotationAutomationController::requestEditPoint(const PointData& oldPointData, const SysStaffKey& key, qreal x, qreal y)
{
    if (isRecordingPreviewShown()) {
        return false;
    }

    // STEP 1 - Check that all of our parameters are valid...
    const PointData::PointType pointType = oldPointData.pointType;
    IF_ASSERT_FAILED(key.isValid() && pointType != PointData::PointType::UNKNOWN) {
        return false;
    }
    const System* system = key.system;
    const SysStaff* sysStaff = system ? system->staff(key.staffIdx) : nullptr;
    const Staff* staff = score() ? score()->staff(key.staffIdx) : nullptr;
    IF_ASSERT_FAILED(sysStaff && staff) {
        return false;
    }

    // STEP 2 - Determine the new tick value based on the x parameter...
    const muse::RectF staffCanvasRect = sysStaff->bbox().translated(system->canvasPos());
    const std::optional<int> newTickOpt = automationTickFromCanvasX(system, staffCanvasRect, x);
    const int newTick = newTickOpt.value_or(oldPointData.tick);
    const bool tickChanged = newTick != oldPointData.tick;

    // STEP 3 - Fetch the point being edited...
    const mu::engraving::AutomationCurveKey curveKey = currentCurveKeyFor(staff);

    const mu::engraving::AutomationCurve& curve = automationData()->curve(curveKey);
    const int oldUtick = utickForTick(oldPointData.tick);
    const int newUtick = utickForTick(newTick);
    const auto existingIt = curve.find(oldUtick);
    IF_ASSERT_FAILED(existingIt != curve.end()) {
        return false;
    }
    const mu::engraving::AutomationPoint& existingPoint = existingIt->second;
    const mu::engraving::real_t existingInValue = mu::engraving::resolveInValue(curve, existingIt);

    //! NOTE: Point in/out values are rescaled to the display range - higher value == lower Y...
    const mu::engraving::real_t referenceValue = pointType == PointData::PointType::IN ? existingInValue : existingPoint.value.outValue;
    const mu::engraving::real_t newValue = automationValueFromDisplay(currentAutomationType(), 1.0 - y, referenceValue);

    // STEP 4 - Update the point's value, and move it to the new tick if necessary...

    //! NOTE: Moving a BOTH point is the simplest case - we can update its value then simply change the tick.
    //! Moving IN/OUT points is slightly more complex. In this case we need to set the in/out values to be
    //! equal at oldTick (effectively converting the original point to a BOTH point) and create a new point
    //! at newTick...

    if (!tickChanged || pointType == PointData::PointType::BOTH) {
        mu::engraving::AutomationPoint editedPoint = existingPoint;
        setEditedValue(editedPoint, pointType, newValue);

        mu::engraving::AutomationPointEdits edits {
            { newUtick, MovePoint { editedPoint, oldUtick } }
        };

        editAutomationPoints(curveKey, edits);

        return true;
    }

    // oldTick becomes a flat BOTH point, so its inValue is frozen to outValue at edit time (it no
    // longer live-tracks outValue if it's edited again later)
    const mu::engraving::AutomationPoint::Ease originalEase
        = mu::engraving::ease(existingPoint).value_or(mu::engraving::AutomationPoint::Ease::none());

    mu::engraving::AutomationPoint updatedOldPoint = existingPoint;
    if (pointType == PointData::PointType::OUT) {
        updatedOldPoint.value.outValue = existingInValue;
    }
    updatedOldPoint.value.inValue = mu::engraving::AutomationPoint::ExplicitArrival { updatedOldPoint.value.outValue, originalEase };
    updatedOldPoint.generated = false;

    mu::engraving::AutomationPoint newPoint;
    newPoint.value.outValue = newValue;
    newPoint.value.inValue = mu::engraving::AutomationPoint::ExplicitArrival { newPoint.value.outValue, originalEase };
    if (existingPoint.itemId && AutomationData::isLinkedItemInScore(score(), *existingPoint.itemId)) {
        newPoint.itemId = existingPoint.itemId;
    }

    mu::engraving::AutomationPointEdits edits {
        { oldUtick, SetPoint { updatedOldPoint } },
        { newUtick, SetPoint { newPoint } }
    };

    editAutomationPoints(curveKey, edits);

    return true;
}

void NotationAutomationController::setEditedValue(mu::engraving::AutomationPoint& point, PointData::PointType pointType,
                                                  mu::engraving::real_t value) const
{
    const mu::engraving::AutomationPoint::Ease preservedEase
        = mu::engraving::ease(point).value_or(mu::engraving::AutomationPoint::Ease::none());
    if (pointType == PointData::PointType::IN) {
        // The user explicitly chose this arrival value; it no longer follows whatever precedes it
        point.value.inValue = mu::engraving::AutomationPoint::ExplicitArrival { value, preservedEase };
    } else if (pointType == PointData::PointType::BOTH) {
        point.value.outValue = value;
        point.value.inValue = mu::engraving::AutomationPoint::ExplicitArrival { point.value.outValue, preservedEase };
    } else {
        point.value.outValue = value;
    }
    point.generated = false;
    if (point.itemId && !AutomationData::isLinkedItemInScore(score(), *point.itemId)) {
        point.itemId.reset(); // its item was deleted: now just a point of the user's own
    }
}

bool NotationAutomationController::requestAddPoint(const SysStaffKey& key, qreal x, qreal y)
{
    if (isRecordingPreviewShown()) {
        return false;
    }

    IF_ASSERT_FAILED(key.isValid()) {
        return false;
    }

    const System* system = key.system;
    const SysStaff* sysStaff = system ? system->staff(key.staffIdx) : nullptr;
    const Staff* staff = score() ? score()->staff(key.staffIdx) : nullptr;
    IF_ASSERT_FAILED(sysStaff && staff) {
        return false;
    }

    const muse::RectF staffCanvasRect = sysStaff->bbox().translated(system->canvasPos());
    const std::optional<int> newTick = automationTickFromCanvasX(system, staffCanvasRect, x);
    if (!newTick) {
        return false;
    }

    mu::engraving::AutomationPoint newPoint;
    newPoint.value.outValue = automationValueFromDisplay(currentAutomationType(), 1.0 - y);
    newPoint.value.inValue = mu::engraving::AutomationPoint::ExplicitArrival { newPoint.value.outValue,
                                                                               mu::engraving::AutomationPoint::Ease::none() };
    newPoint.generated = false;

    const mu::engraving::AutomationCurveKey curveKey = currentCurveKeyFor(staff);

    mu::engraving::AutomationPointEdits edits {
        { utickForTick(*newTick), SetPoint { newPoint } }
    };

    editAutomationPoints(curveKey, edits);

    return true;
}

bool NotationAutomationController::requestSegmentBend(const SysStaffKey& key, int segmentIndex, qreal value)
{
    if (isRecordingPreviewShown()) {
        return false;
    }

    const auto pointsDataIt = m_pointsDataByStaff.find(key);
    const Staff* staff = score() ? score()->staff(key.staffIdx) : nullptr;
    IF_ASSERT_FAILED(pointsDataIt != m_pointsDataByStaff.end() && staff && segmentIndex >= 0
                     && segmentIndex + 1 < pointsDataIt->second.size()) {
        return false;
    }

    const PointData& arrival = pointsDataIt->second.at(segmentIndex + 1);
    const mu::engraving::AutomationPoint* point = automationPointAt(key, arrival.tick);
    if (!point || isScoreDrivenPoint(point)) {
        return false;
    }

    const auto* explicitArrival = std::get_if<mu::engraving::AutomationPoint::ExplicitArrival>(&point->value.inValue);
    if (!explicitArrival) {
        return false;
    }

    mu::engraving::AutomationPoint bentPoint = *point;
    mu::engraving::AutomationPoint::Ease ease = explicitArrival->ease;
    ease.value = muse::real_t(std::clamp(static_cast<double>(value), 0.0, 1.0));
    if (ease.isNone()) {
        ease = mu::engraving::AutomationPoint::Ease::none(); // straight again
    }
    bentPoint.value.inValue = mu::engraving::AutomationPoint::ExplicitArrival { explicitArrival->value, ease };
    bentPoint.generated = false;

    mu::engraving::AutomationPointEdits edits {
        { utickForTick(arrival.tick), SetPoint { bentPoint } }
    };

    editAutomationPoints(currentCurveKeyFor(staff), edits);

    return true;
}

bool NotationAutomationController::requestRemovePoint(const PointData& pointData, const SysStaffKey& key)
{
    if (isRecordingPreviewShown()) {
        return false;
    }

    IF_ASSERT_FAILED(key.isValid()) {
        return false;
    }

    const mu::engraving::AutomationPoint* automationPoint = automationPointAt(key, pointData.tick);
    if (isScoreDrivenPoint(automationPoint)) {
        return false;
    }

    const Staff* staff = score() ? score()->staff(key.staffIdx) : nullptr;
    IF_ASSERT_FAILED(staff) {
        return false;
    }

    const mu::engraving::AutomationCurveKey curveKey = currentCurveKeyFor(staff);

    mu::engraving::AutomationPointEdits edits {
        { utickForTick(pointData.tick), ErasePoint {} }
    };

    editAutomationPoints(curveKey, edits);

    return true;
}

void NotationAutomationController::editAutomationPoints(const mu::engraving::AutomationCurveKey& key,
                                                        mu::engraving::AutomationPointEdits& edits)
{
    const INotationAutomationPtr notationAutomation = automation();
    IF_ASSERT_FAILED(notationAutomation) {
        return;
    }

    notationAutomation->editPoints(key, edits);
}

const mu::engraving::AutomationPoint* NotationAutomationController::automationPointAt(const SysStaffKey& key, int tick) const
{
    const Staff* staff = score() ? score()->staff(key.staffIdx) : nullptr;
    IF_ASSERT_FAILED(staff && automationData()) {
        return nullptr;
    }

    const mu::engraving::AutomationCurveKey curveKey = currentCurveKeyFor(staff);
    const mu::engraving::AutomationCurve& curve = automationData()->curve(curveKey);
    const auto it = curve.find(utickForTick(tick));
    if (it == curve.end()) {
        return nullptr;
    }

    return &it->second;
}

//! NOTE: automation curves live in expanded (repeats unrolled) utick space, while the score shows - and the user
//! edits - each tick once. A tick maps to its utick on the first pass through it; ScoreAutomationController mirrors
//! every edit to the other passes
int NotationAutomationController::utickForTick(int tick) const
{
    const mu::engraving::RepeatList& repeatList = score()->masterScore()->expandedRepeatList();
    for (const mu::engraving::RepeatSegment* segment : repeatList) {
        if (tick >= segment->tick && tick < segment->endTick()) {
            return tick + (segment->utick - segment->tick);
        }
    }

    // e.g. right at the end of the score
    return repeatList.empty() ? tick : tick + (repeatList.back()->utick - repeatList.back()->tick);
}

//! NOTE: the tick of a curve point, when that utick is on the first pass through its tick (otherwise nullopt:
//! it's a mirrored copy, shown through the first pass one)
std::optional<int> NotationAutomationController::firstPassTickForUtick(int utick) const
{
    const mu::engraving::RepeatList& repeatList = score()->masterScore()->expandedRepeatList();
    if (repeatList.empty()) {
        return utick;
    }

    auto segmentIt = repeatList.findRepeatSegmentFromUTick(utick);
    if (segmentIt == repeatList.cend()) {
        segmentIt = std::prev(repeatList.cend()); // e.g. right at the end of the score
    }

    const int tick = utick - ((*segmentIt)->utick - (*segmentIt)->tick);
    return utickForTick(tick) == utick ? std::optional(tick) : std::nullopt;
}

//! NOTE: a point that follows the score rather than the user: generated, or still tied to an item of the score
//! (e.g. a tempo marking) - such a point can't be removed or moved in time
bool NotationAutomationController::isScoreDrivenPoint(const mu::engraving::AutomationPoint* point) const
{
    return !point || AutomationData::isScoreDrivenPoint(score(), *point);
}

AutomationType NotationAutomationController::currentAutomationType() const
{
    return notationConfiguration()->currentAutomationType();
}

AutomationCurveKey NotationAutomationController::currentCurveKeyFor(const Staff* staff) const
{
    AutomationCurveKey key = curveKeyFor(currentAutomationType(), staff);
    if (key.type == AutomationType::MidiCC) {
        key.controller = static_cast<uint8_t>(notationConfiguration()->currentAutomationMidiCc());
    }
    return key;
}

INotationAutomationPtr NotationAutomationController::automation() const
{
    const IMasterNotationPtr masterNotation = globalContext()->currentMasterNotation();
    return masterNotation ? masterNotation->automation() : nullptr;
}

INotationPtr NotationAutomationController::currentNotation() const
{
    return globalContext()->currentNotation();
}

//! NOTE: while a MIDI CC take is being recorded, its curves show a preview, not the stored points: not editable
bool NotationAutomationController::isRecordingPreviewShown() const
{
    const INotationAutomationPtr notationAutomation = automation();
    return notationAutomation && !notationAutomation->recordingPreviews().empty();
}

//! NOTE: the stored curve, or, while a MIDI CC take is being recorded into it, the curve as it will be once written
const mu::engraving::AutomationCurve& NotationAutomationController::displayedCurve(const mu::engraving::AutomationCurveKey& key) const
{
    if (const INotationAutomationPtr notationAutomation = automation()) {
        const auto previewIt = notationAutomation->recordingPreviews().find(key);
        if (previewIt != notationAutomation->recordingPreviews().cend()) {
            return previewIt->second;
        }
    }

    return automationData()->curve(key);
}

mu::engraving::AutomationDataConstPtr NotationAutomationController::automationData() const
{
    const INotationAutomationPtr notationAutomation = automation();
    return notationAutomation ? notationAutomation->automationData() : nullptr;
}

mu::engraving::Score* NotationAutomationController::score() const
{
    return currentNotation() ? currentNotation()->elements()->msScore() : nullptr;
}
