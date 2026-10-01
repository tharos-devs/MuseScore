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
#include "midiccrecorder.h"

#include <cmath>
#include <vector>

#include "engraving/dom/masterscore.h"
#include "engraving/dom/part.h"
#include "engraving/dom/repeatlist.h"
#include "engraving/dom/staff.h"
#include "notation/inotationautomation.h"
#include "notation/inotationelements.h"
#include "notation/inotationinteraction.h"
#include "notation/inotationplayback.h"
#include "notation/inotationselection.h"
#include "notation/inotationselectionrange.h"
#include "project/iprojectaudiosettings.h"

#include "log.h"

using namespace mu::playback;
using namespace mu::engraving;
using namespace muse;

//! NOTE: MIDI CC1/7/11 are always offered in the MIDI CC menus, the others only once added to the score
static const std::vector<uint8_t> PREDEFINED_CONTROLLERS { 1, 7, 11 };

//! NOTE: a recorded value is only kept as a point where the straight line between its neighbours would be
//! more than this away from it (2 MIDI steps: inaudible, but a fast controller movement doesn't keep every value)
static constexpr double THINNING_TOLERANCE = 2.0 / 127.0;

//! NOTE: beyond this, the engine's last position report is stale (e.g. playback just stopped): not refined further
static constexpr qint64 MAX_POSITION_REFINEMENT_MS = 250;

static constexpr int PREVIEW_REFRESH_INTERVAL_MS = 40;

//! NOTE: a reported position this far past the previous one (they come every few tens of milliseconds) is a jump
static constexpr double POSITION_JUMP_SECS = 0.5;

static AutomationPoint takePoint(double value)
{
    AutomationPoint point;
    point.value.outValue = value;
    point.value.inValue = AutomationPoint::ExplicitArrival { point.value.outValue, AutomationPoint::Ease::none() };
    point.generated = false;
    return point;
}

//! NOTE: the value a curve plays at a utick (the first point's value applies before it)
static double curveValueAt(const AutomationCurve& curve, int utick)
{
    if (curve.empty()) {
        return 0.;
    }

    auto next = curve.upper_bound(utick);
    if (next == curve.cbegin()) {
        return next->second.value.outValue;
    }

    const auto it = std::prev(next);
    if (next == curve.cend() || it->first == utick) {
        return it->second.value.outValue;
    }

    const real_t t = real_t(utick - it->first) / real_t(next->first - it->first);
    return mpe::evaluateAt(next->second.value, it->second.value.outValue, t);
}

namespace {
struct TakePoint {
    int utick = 0;
    double value = 0.;
};

void thinPoints(const std::vector<TakePoint>& points, size_t from, size_t to, std::vector<bool>& keep)
{
    if (to <= from + 1) {
        return;
    }

    const TakePoint& a = points[from];
    const TakePoint& b = points[to];

    size_t farthest = from;
    double maxDistance = 0.;

    for (size_t i = from + 1; i < to; ++i) {
        const double t = b.utick == a.utick ? 0. : double(points[i].utick - a.utick) / double(b.utick - a.utick);
        const double distance = std::abs(points[i].value - (a.value + t * (b.value - a.value)));
        if (distance > maxDistance) {
            maxDistance = distance;
            farthest = i;
        }
    }

    if (maxDistance <= THINNING_TOLERANCE) {
        return;
    }

    keep[farthest] = true;
    thinPoints(points, from, farthest, keep);
    thinPoints(points, farthest, to, keep);
}

std::vector<TakePoint> takePoints(const std::map<int, double>& values)
{
    std::vector<TakePoint> points;
    points.reserve(values.size());
    for (const auto& [utick, value] : values) {
        points.push_back({ utick, value });
    }
    return points;
}

//! NOTE: Ramer-Douglas-Peucker on the values: drops the points a straight line already plays
std::vector<TakePoint> thinned(const std::map<int, double>& values)
{
    std::vector<TakePoint> points = takePoints(values);

    if (points.size() <= 2) {
        return points;
    }

    std::vector<bool> keep(points.size(), false);
    keep.front() = true;
    keep.back() = true;
    thinPoints(points, 0, points.size() - 1, keep);

    std::vector<TakePoint> result;
    for (size_t i = 0; i < points.size(); ++i) {
        if (keep[i]) {
            result.push_back(points[i]);
        }
    }

    return result;
}
}

MidiCcRecorder::MidiCcRecorder(const modularity::ContextPtr& iocCtx)
    : Contextable(iocCtx)
{
}

void MidiCcRecorder::init()
{
    m_previewTimer.setSingleShot(true);
    m_previewTimer.setInterval(PREVIEW_REFRESH_INTERVAL_MS);
    m_previewTimer.callOnTimeout([this]() {
        publishPreviews();
    });

    midiInPort()->eventReceived().onReceive(this, [this](const midi::tick_t, const midi::Event& event) {
        onMidiEventReceived(event);
    });

    playbackController()->isPlayingChanged().onReceive(this, [this](bool isPlaying) {
        // Each start (and resume) begins a new segment, from the first position reported after it
        m_lastReportedPosition.reset();
        m_sinceLastReportedPosition.invalidate();
        ++m_segmentGeneration;

        if (!isPlaying) {
            commitTake();
        }
    });

    globalContext()->playbackState()->playbackPositionChanged().onReceive(this, [this](secs_t position) {
        onPlaybackPositionChanged(position);
    });

    globalContext()->currentMasterNotationChanged().onNotify(this, [this]() {
        // The previous score is gone: its take can't be written anymore
        clearPreviews();
        clearTake();
        setArmed(false);
    });
}

bool MidiCcRecorder::isArmed() const
{
    return m_target.has_value();
}

void MidiCcRecorder::toggleArmed()
{
    if (isArmed()) {
        commitTake();
        setArmed(false);
        return;
    }

    m_target = resolveTarget();
    if (!m_target) {
        interactive()->info(muse::trc("playback", "Cannot record MIDI CC"),
                            muse::trc("playback", "Select a staff played by a VST instrument first: the MIDI CCs received "
                                                  "from the MIDI input device are recorded into its curves during playback."));
        return;
    }

    m_armedChanged.notify();
}

muse::async::Notification MidiCcRecorder::armedChanged() const
{
    return m_armedChanged;
}

void MidiCcRecorder::setArmed(bool armed)
{
    if (armed == isArmed()) {
        return;
    }

    if (!armed) {
        m_target.reset();
    }

    m_armedChanged.notify();
}

std::optional<MidiCcRecorder::Target> MidiCcRecorder::resolveTarget() const
{
    const notation::INotationPtr notation = globalContext()->currentNotation();
    const notation::IMasterNotationPtr masterNotation = globalContext()->currentMasterNotation();
    const project::INotationProjectPtr project = globalContext()->currentProject();
    if (!notation || !masterNotation || !project || !project->audioSettings()) {
        return std::nullopt;
    }

    const notation::INotationSelectionPtr selection = notation->interaction()->selection();
    if (!selection || selection->isNone()) {
        return std::nullopt;
    }

    std::optional<staff_idx_t> staffIdx;
    if (selection->isRange()) {
        staffIdx = selection->range()->startStaffIndex();
    } else if (!selection->elements().empty()) {
        staffIdx = selection->elements().front()->staffIdx();
    }

    // The selection may be in a part: its staves share their ids with the master score's ones
    const Staff* staff = staffIdx ? notation->elements()->msScore()->staff(*staffIdx) : nullptr;
    const Staff* masterStaff = staff ? masterNotation->masterScore()->staffById(staff->id()) : nullptr;
    const Part* part = masterStaff ? masterStaff->part() : nullptr;
    if (!part) {
        return std::nullopt;
    }

    //! NOTE: MIDI CC curves belong to the part's first instrument, and only reach VST instruments
    const InstrumentTrackId trackId { part->id(), part->instrumentId() };
    if (project->audioSettings()->trackInputParams(trackId).type() != audio::AudioSourceType::Vsti) {
        return std::nullopt;
    }

    return Target { trackId, masterStaff->idx() };
}

void MidiCcRecorder::onPlaybackPositionChanged(secs_t position)
{
    if (m_lastReportedPosition) {
        const double delta = double(position - *m_lastReportedPosition);
        if (delta < 0. || delta > POSITION_JUMP_SECS) {
            ++m_segmentGeneration;
        }
    }

    m_lastReportedPosition = position;
    m_sinceLastReportedPosition.restart();
}

void MidiCcRecorder::onMidiEventReceived(const midi::Event& event)
{
    if (!m_target || event.opcode() != midi::Event::Opcode::ControlChange) {
        return;
    }

    if (!event.isChannelVoice() && !event.isChannelVoice20()) {
        return;
    }

    const notation::IMasterNotationPtr masterNotation = globalContext()->currentMasterNotation();
    if (!masterNotation) {
        return;
    }

    const uint8_t controller = event.index();
    if (controller > 127) {
        return;
    }

    const double value = event.isChannelVoice20() ? double(event.data()) / 4294967295.0 : double(event.data()) / 127.0;

    // Heard right away (while playing, it also replaces the existing curve from now on - see VstSequencer)
    mpe::ControllerChangeEvent controllerEvent;
    controllerEvent.type = mpe::ControllerChangeEvent::ControlChange;
    controllerEvent.controller = controller;
    controllerEvent.val = static_cast<float>(value);
    masterNotation->playback()->triggerControllers({ controllerEvent }, m_target->masterStaffIdx, 0);

    if (!playbackController()->isPlaying()) {
        return;
    }

    const std::optional<int> utick = currentUtick();
    if (!utick) {
        return;
    }

    std::vector<TakeSegment>& segments = m_take[controller];
    auto generationIt = m_controllerSegmentGeneration.find(controller);
    const bool isNewSegment = segments.empty() || generationIt == m_controllerSegmentGeneration.end()
                              || generationIt->second != m_segmentGeneration || *utick < segments.back().crbegin()->first;
    if (isNewSegment) {
        segments.emplace_back();
        m_controllerSegmentGeneration.insert_or_assign(controller, m_segmentGeneration);
    }

    segments.back()[*utick] = value;

    auto [rangeIt, inserted] = m_previewChangedRanges.try_emplace(controller, *utick, *utick);
    rangeIt->second.first = std::min({ rangeIt->second.first, *utick, segments.back().cbegin()->first });
    rangeIt->second.second = std::max({ rangeIt->second.second, *utick, segments.back().crbegin()->first });

    if (!m_previewTimer.isActive()) {
        m_previewTimer.start();
    }
}

std::optional<int> MidiCcRecorder::currentUtick() const
{
    const notation::IMasterNotationPtr masterNotation = globalContext()->currentMasterNotation();
    const MasterScore* score = masterNotation ? masterNotation->masterScore() : nullptr;
    if (!score || !m_lastReportedPosition) {
        return std::nullopt;
    }

    secs_t position = *m_lastReportedPosition;
    if (m_sinceLastReportedPosition.isValid()) {
        const qint64 elapsedMs = std::min(m_sinceLastReportedPosition.elapsed(), MAX_POSITION_REFINEMENT_MS);
        position += secs_t(double(elapsedMs) / 1000.0);
    }

    const int tick = masterNotation->playback()->secToTick(position);

    //! NOTE: written on the first pass through that tick, like a point drawn with the mouse (it's the one
    //! shown and edited in the score)
    const RepeatList& repeatList = score->expandedRepeatList();
    for (const RepeatSegment* segment : repeatList) {
        if (tick >= segment->tick && tick < segment->endTick()) {
            return tick + (segment->utick - segment->tick);
        }
    }

    return repeatList.empty() ? tick : tick + (repeatList.back()->utick - repeatList.back()->tick);
}

AutomationCurve MidiCcRecorder::curveWithTake(const AutomationCurve& existing, const std::vector<TakeSegment>& segments, bool thin,
                                              std::vector<std::pair<int, int> >* ranges) const
{
    AutomationCurve result = existing;

    for (const TakeSegment& segment : segments) {
        if (segment.empty()) {
            continue;
        }

        const int fromUtick = segment.cbegin()->first;
        const int toUtick = segment.crbegin()->first;
        if (ranges) {
            ranges->emplace_back(fromUtick, toUtick);
        }

        // "Touch": the curve is only replaced within the segment, and joins it with a step on each side, so
        // that it keeps its own shape outside of it
        const bool hasCurve = !result.empty();
        const double valueBefore = curveValueAt(result, fromUtick);
        const double valueAfter = curveValueAt(result, toUtick);

        result.erase(result.lower_bound(fromUtick), result.upper_bound(toUtick));

        const std::vector<TakePoint> points = thin ? thinned(segment) : takePoints(segment);

        for (size_t i = 0; i < points.size(); ++i) {
            AutomationPoint point = takePoint(points[i].value);

            if (hasCurve && i == 0) {
                point.value.inValue = AutomationPoint::ExplicitArrival { valueBefore, AutomationPoint::Ease::none() };
            }

            // A single-point segment keeps its value afterwards: there's no movement to end
            if (hasCurve && i == points.size() - 1 && points.size() > 1) {
                point.value.outValue = valueAfter;
            }

            result.insert({ points[i].utick, point });
        }
    }

    return result;
}

void MidiCcRecorder::publishPreviews()
{
    const notation::IMasterNotationPtr masterNotation = globalContext()->currentMasterNotation();
    const notation::INotationAutomationPtr automation = masterNotation ? masterNotation->automation() : nullptr;
    const notation::AutomationDataConstPtr data = automation ? automation->automationData() : nullptr;
    if (!data || !m_target) {
        m_previewChangedRanges.clear();
        return;
    }

    for (const auto& [controller, range] : m_previewChangedRanges) {
        const auto takeIt = m_take.find(controller);
        if (takeIt == m_take.cend()) {
            continue;
        }

        const AutomationCurveKey key = AutomationCurveKey::midiCc(m_target->trackId, controller);
        const AutomationCurve preview = curveWithTake(data->curve(key), takeIt->second, false /*thin*/, nullptr);
        automation->setRecordingPreview(key, preview, range.first, range.second);
    }

    m_previewChangedRanges.clear();
}

void MidiCcRecorder::clearPreviews()
{
    m_previewTimer.stop();
    m_previewChangedRanges.clear();

    const notation::IMasterNotationPtr masterNotation = globalContext()->currentMasterNotation();
    if (const notation::INotationAutomationPtr automation = masterNotation ? masterNotation->automation() : nullptr) {
        automation->clearRecordingPreviews();
    }
}

void MidiCcRecorder::clearTake()
{
    m_take.clear();
    m_controllerSegmentGeneration.clear();
}

void MidiCcRecorder::commitTake()
{
    const notation::IMasterNotationPtr masterNotation = globalContext()->currentMasterNotation();
    const notation::INotationAutomationPtr automation = masterNotation ? masterNotation->automation() : nullptr;
    const notation::AutomationDataConstPtr data = automation ? automation->automationData() : nullptr;
    if (m_take.empty() || !m_target || !data) {
        clearTake();
        clearPreviews();
        return;
    }

    const std::vector<uint8_t> customControllers = automation->customMidiCcs();

    std::vector<std::pair<AutomationCurveKey, AutomationPointEdits> > editsByCurve;
    std::vector<uint8_t> newCustomControllers;

    for (const auto& [controller, segments] : m_take) {
        const AutomationCurveKey key = AutomationCurveKey::midiCc(m_target->trackId, controller);
        const AutomationCurve& existing = data->curve(key);

        std::vector<std::pair<int, int> > ranges;
        const AutomationCurve result = curveWithTake(existing, segments, true /*thin*/, &ranges);
        if (ranges.empty()) {
            continue;
        }

        const auto isInRanges = [&ranges](int utick) {
            return std::any_of(ranges.cbegin(), ranges.cend(), [utick](const auto& range) {
                return utick >= range.first && utick <= range.second;
            });
        };

        // Only the ranges the take covers change: whatever the curve had there goes, the take's points come
        AutomationPointEdits edits;
        for (const auto& [utick, point] : existing) {
            if (isInRanges(utick) && result.find(utick) == result.cend()) {
                edits.push_back({ utick, AutomationPointEdit::ErasePoint {} });
            }
        }
        for (const auto& [utick, point] : result) {
            if (isInRanges(utick)) {
                edits.push_back({ utick, AutomationPointEdit::SetPoint { point } });
            }
        }

        editsByCurve.emplace_back(key, std::move(edits));

        if (!muse::contains(PREDEFINED_CONTROLLERS, controller) && !muse::contains(customControllers, controller)) {
            newCustomControllers.push_back(controller);
        }
    }

    clearTake();

    if (!editsByCurve.empty()) {
        automation->recordMidiCcTake(editsByCurve, newCustomControllers);
    }

    // Once the take is written, the curve itself shows it
    clearPreviews();
}
