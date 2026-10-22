/*
 * SPDX-License-Identifier: GPL-3.0-only
 * MuseScore-CLA-applies
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

#include "audiosyncedits.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <map>

#include "engraving/automation/tempovalues.h"
#include "engraving/dom/chord.h"
#include "engraving/dom/measure.h"
#include "engraving/dom/segment.h"
#include "engraving/dom/repeatlist.h"
#include "engraving/dom/score.h"
#include "engraving/dom/sig.h"
#include "engraving/dom/tempotimeline.h"
#include "engraving/types/constants.h"

#include "translation.h"

using namespace mu::engraving;
using namespace mu::notation;

namespace mu::playback::audiosync {
std::vector<SyncPoint> syncPoints(const INotationAutomation& automation)
{
    std::vector<SyncPoint> result;

    const AutomationDataConstPtr data = automation.automationData();
    if (!data) {
        return result;
    }

    const AutomationCurve& curve = data->isAudioSyncEnabled() ? data->curve(TEMPO_KEY) : data->inactiveTempoCurve();
    for (const auto& [utick, point] : curve) {
        if (!point.audioSync) {
            continue;
        }

        SyncPoint syncPoint;
        syncPoint.utick = utick;
        syncPoint.audioSecs = point.audioSync->audioSecs;
        syncPoint.origin = point.audioSync->origin;
        syncPoint.locked = point.audioSync->locked;
        syncPoint.confidence = point.audioSync->confidence;
        result.push_back(syncPoint);
    }

    return result;
}

std::optional<double> segmentBpm(const std::vector<SyncPoint>& points, size_t index)
{
    if (index + 1 >= points.size()) {
        return std::nullopt;
    }

    const double quarters = static_cast<double>(points[index + 1].utick - points[index].utick) / Constants::DIVISION;
    const double secs = points[index + 1].audioSecs - points[index].audioSecs;
    if (secs <= 0.0) {
        return std::nullopt;
    }

    return quarters / secs * 60.0;
}

std::optional<std::pair<double, double> > allowedAudioRange(const std::vector<SyncPoint>& points, int utick)
{
    double from = -std::numeric_limits<double>::infinity();
    double to = std::numeric_limits<double>::infinity();

    const SyncPoint* prev = nullptr;
    const SyncPoint* next = nullptr;
    for (const SyncPoint& point : points) {
        if (point.utick < utick) {
            prev = &point;
        } else if (point.utick > utick && !next) {
            next = &point;
        }
    }

    if (prev) {
        const double quarters = static_cast<double>(utick - prev->utick) / Constants::DIVISION;
        from = std::max(from, prev->audioSecs + quarters * 60.0 / MAX_SYNC_BPM);
        to = std::min(to, prev->audioSecs + quarters * 60.0 / MIN_SYNC_BPM);
    }

    if (next) {
        const double quarters = static_cast<double>(next->utick - utick) / Constants::DIVISION;
        from = std::max(from, next->audioSecs - quarters * 60.0 / MIN_SYNC_BPM);
        to = std::min(to, next->audioSecs - quarters * 60.0 / MAX_SYNC_BPM);
    }

    if (from > to) {
        return std::nullopt;
    }

    return std::make_pair(from, to);
}

std::optional<int> nearestBeatUtick(const Score* score, int utick)
{
    if (!score) {
        return std::nullopt;
    }

    const RepeatList& repeatList = score->expandedRepeatList();
    const auto segIt = repeatList.findRepeatSegmentFromUTick(utick);
    if (segIt == repeatList.cend()) {
        return std::nullopt;
    }

    const RepeatSegment* seg = *segIt;
    const int tickOffset = seg->utick - seg->tick;
    const Measure* measure = score->tick2measure(Fraction::fromTicks(utick - tickOffset));
    if (!measure) {
        return std::nullopt;
    }

    const int tickFrom = measure->tick().ticks();
    const int tickTo = measure->endTick().ticks();
    const TimeSigFrac timeSig = score->sigmap()->timesig(tickFrom).nominal();
    const double bps = score->tempoTimeline(true).tempo(tickFrom + tickOffset).val;
    const int step = std::max(1, timeSig.isBeatedCompound(bps) ? timeSig.beatTicks() : timeSig.dUnitTicks());

    // Same beats as the metronome: an anacrusis' beats are counted from its end
    std::vector<int> candidates { tickFrom, tickTo };
    int beatTick = tickFrom + (measure->isAnacrusis() ? measure->ticks().ticks() % step : 0);
    for (; beatTick < tickTo; beatTick += step) {
        candidates.push_back(beatTick);
    }

    const int rawTick = utick - tickOffset;
    const int nearest = *std::min_element(candidates.begin(), candidates.end(), [rawTick](int a, int b) {
        return std::abs(a - rawTick) < std::abs(b - rawTick);
    });

    return nearest + tickOffset;
}

static bool editPoint(INotationAutomation& automation, int utick,
                      const std::variant<AutomationPointEdit::SetPoint, AutomationPointEdit::MovePoint,
                                         AutomationPointEdit::ErasePoint>& change,
                      const muse::TranslatableString& actionName)
{
    std::vector<std::pair<AutomationCurveKey, AutomationPointEdits> > edits {
        { TEMPO_KEY, AutomationPointEdits { AutomationPointEdit { utick, change } } }
    };
    automation.editPoints(edits, actionName);
    return true;
}

bool addPoint(INotationAutomation& automation, const Score* score, int utick, double audioSecs)
{
    const AutomationDataConstPtr data = automation.automationData();
    if (!data || !data->isAudioSyncEnabled() || !score) {
        return false;
    }

    const std::vector<SyncPoint> points = syncPoints(automation);
    for (const SyncPoint& point : points) {
        if (point.utick == utick) {
            return false;
        }
    }

    const std::optional<std::pair<double, double> > range = allowedAudioRange(points, utick);
    if (!range || audioSecs < range->first || audioSecs > range->second) {
        return false;
    }

    AutomationPoint point;
    point.value.inValue = AutomationPoint::ArrivalFromPrevious {};
    point.value.outValue = normalizeTempo(score->tempoTimeline(true).tempo(utick));

    AudioSyncInfo sync;
    sync.audioSecs = audioSecs;
    sync.origin = AudioSyncInfo::Origin::Manual;
    point.audioSync = sync;

    return editPoint(automation, utick, AutomationPointEdit::SetPoint { point },
                     muse::TranslatableString("undoableAction", "Add audio sync point"));
}

bool movePoint(INotationAutomation& automation, int utick, double audioSecs)
{
    const AutomationDataConstPtr data = automation.automationData();
    if (!data || !data->isAudioSyncEnabled()) {
        return false;
    }

    const AutomationCurve& curve = data->curve(TEMPO_KEY);
    const auto it = curve.find(utick);
    if (it == curve.end() || !it->second.audioSync || it->second.audioSync->locked) {
        return false;
    }

    const std::optional<std::pair<double, double> > range = allowedAudioRange(syncPoints(automation), utick);
    if (!range) {
        return false;
    }

    AutomationPoint point = it->second;
    point.audioSync->audioSecs = std::clamp(audioSecs, range->first, range->second);
    point.audioSync->origin = AudioSyncInfo::Origin::Manual;
    if (point == it->second) {
        return false;
    }

    return editPoint(automation, utick, AutomationPointEdit::SetPoint { point },
                     muse::TranslatableString("undoableAction", "Move audio sync point"));
}

bool removePoint(INotationAutomation& automation, int utick)
{
    const AutomationDataConstPtr data = automation.automationData();
    if (!data || !data->isAudioSyncEnabled()) {
        return false;
    }

    //! NOTE The first one sets the score's start in the audio
    const std::vector<SyncPoint> points = syncPoints(automation);
    if (points.empty() || points.front().utick == utick) {
        return false;
    }

    const AutomationCurve& curve = data->curve(TEMPO_KEY);
    const auto it = curve.find(utick);
    if (it == curve.end() || !it->second.audioSync || it->second.audioSync->locked) {
        return false;
    }

    return editPoint(automation, utick, AutomationPointEdit::ErasePoint {},
                     muse::TranslatableString("undoableAction", "Delete audio sync point"));
}

bool removeAllPoints(INotationAutomation& automation)
{
    const AutomationDataConstPtr data = automation.automationData();
    if (!data || !data->isAudioSyncEnabled()) {
        return false;
    }

    const std::vector<SyncPoint> points = syncPoints(automation);
    AutomationPointEdits edits;
    for (size_t i = 1; i < points.size(); ++i) {
        if (!points[i].locked) {
            edits.push_back({ points[i].utick, AutomationPointEdit::ErasePoint {} });
        }
    }

    if (edits.empty()) {
        return false;
    }

    std::vector<std::pair<AutomationCurveKey, AutomationPointEdits> > editsByCurve { { TEMPO_KEY, edits } };
    automation.editPoints(editsByCurve, muse::TranslatableString("undoableAction", "Clear audio sync points"));
    return true;
}

// ---- detection ----

//! NOTE The measure starts and beats of the unrolled score from fromUtick on (the metronome's beats)
static std::vector<std::pair<int, bool> > scoreBeats(const Score* score, int fromUtick, int toUtick)
{
    std::vector<std::pair<int, bool> > result; // utick, measure start

    const TempoTimeline& timeline = score->tempoTimeline(true);
    for (const RepeatSegment* seg : score->expandedRepeatList()) {
        const int tickOffset = seg->utick - seg->tick;
        for (const Measure* measure : seg->measureList()) {
            const int tickFrom = measure->tick().ticks();
            const int tickTo = measure->endTick().ticks();
            if (tickTo + tickOffset <= fromUtick) {
                continue;
            }
            if (toUtick >= 0 && tickFrom + tickOffset >= toUtick) {
                return result;
            }

            const TimeSigFrac timeSig = score->sigmap()->timesig(tickFrom).nominal();
            const double bps = timeline.tempo(tickFrom + tickOffset).val;
            const int step = std::max(1, timeSig.isBeatedCompound(bps) ? timeSig.beatTicks() : timeSig.dUnitTicks());

            if (tickFrom + tickOffset >= fromUtick) {
                result.emplace_back(tickFrom + tickOffset, true);
            }
            int beatTick = tickFrom + (measure->isAnacrusis() ? measure->ticks().ticks() % step : 0);
            for (; beatTick < tickTo; beatTick += step) {
                if (beatTick > tickFrom && beatTick + tickOffset >= fromUtick) {
                    result.emplace_back(beatTick + tickOffset, false);
                }
            }
        }
    }

    // The score's end, so the last measure gets matched too
    if (const Measure* last = score->lastMeasure()) {
        const int endUtick = score->expandedRepeatList().empty() ? last->endTick().ticks()
                             : score->expandedRepeatList().back()->utick + score->expandedRepeatList().back()->len();
        if ((toUtick < 0 || endUtick <= toUtick) && (result.empty() || result.back().first < endUtick)) {
            result.emplace_back(endUtick, true);
        }
    }

    return result;
}

std::vector<MatchedPoint> matchMeasures(const Score* score, const std::vector<DetectedBeatTime>& beats, const DetectionOptions& options,
                                        MatchInfo* info)
{
    std::vector<MatchedPoint> result;
    if (!score || beats.size() < 2) {
        return result;
    }

    std::vector<std::pair<int, bool> > positions = scoreBeats(score, options.anchorUtick, options.toUtick);
    //! NOTE The anchor (e.g. the first note, a pickup's) isn't necessarily on a beat: it's the first position anyway
    if (positions.empty() || positions.front().first != options.anchorUtick) {
        positions.insert(positions.begin(), { options.anchorUtick, false });
    }
    if (positions.size() < 2) {
        return result;
    }

    // The audio's beats after the anchor, which is the first one
    std::vector<DetectedBeatTime> audioBeats;
    std::vector<double> intervals;
    for (size_t i = 1; i < beats.size(); ++i) {
        intervals.push_back(beats[i].secs - beats[i - 1].secs);
    }
    std::sort(intervals.begin(), intervals.end());
    const double medianInterval = intervals[intervals.size() / 2];

    // The anchor on the nearest detected beat, if one is close enough
    double anchorSecs = options.anchorSecs;
    const auto nearest = std::min_element(beats.begin(), beats.end(), [&options](const DetectedBeatTime& a, const DetectedBeatTime& b) {
        return std::abs(a.secs - options.anchorSecs) < std::abs(b.secs - options.anchorSecs);
    });
    if (nearest != beats.end() && std::abs(nearest->secs - options.anchorSecs) < medianInterval * 0.5) {
        anchorSecs = nearest->secs;
    }

    audioBeats.push_back({ anchorSecs, 1.f });
    for (const DetectedBeatTime& beat : beats) {
        if (beat.secs > anchorSecs + medianInterval * 0.5) {
            audioBeats.push_back(beat);
        }
    }

    // Octave errors: detected beats twice too fast or too slow. Judged by the expected tempo the user gave,
    // not by the score's tempo markings: they're only approximate here
    const double detectedBpm = 60.0 / medianInterval;
    DetectionOptions::Octave octave = options.octave;
    if (octave == DetectionOptions::Octave::Auto) {
        octave = detectedBpm > options.maxBpm ? DetectionOptions::Octave::Half
                 : detectedBpm < options.minBpm ? DetectionOptions::Octave::Double
                 : DetectionOptions::Octave::None;
    }

    if (info) {
        info->anchorSecs = anchorSecs;
        info->detectedBpm = detectedBpm;
        info->octave = octave;
        info->appliedBpm = octave == DetectionOptions::Octave::Half ? detectedBpm / 2.0
                           : octave == DetectionOptions::Octave::Double ? detectedBpm * 2.0 : detectedBpm;
    }

    if (octave == DetectionOptions::Octave::Half) {
        std::vector<DetectedBeatTime> halved;
        for (size_t i = 0; i < audioBeats.size(); i += 2) {
            halved.push_back(audioBeats[i]);
        }
        audioBeats = std::move(halved);
    } else if (octave == DetectionOptions::Octave::Double) {
        std::vector<DetectedBeatTime> doubled;
        for (size_t i = 0; i < audioBeats.size(); ++i) {
            doubled.push_back(audioBeats[i]);
            if (i + 1 < audioBeats.size()) {
                doubled.push_back({ (audioBeats[i].secs + audioBeats[i + 1].secs) / 2.0, 0.3f });
            }
        }
        audioBeats = std::move(doubled);
    }

    // Beat after beat: the measure starts
    const size_t count = std::min(positions.size(), audioBeats.size());
    for (size_t i = 0; i < count; ++i) {
        if (!positions[i].second) {
            continue;
        }
        result.push_back({ positions[i].first, audioBeats[i].secs, audioBeats[i].confidence });
    }

    return options.everyMeasure ? result : simplifyPoints(result, options.toleranceSecs);
}

std::vector<MatchedPoint> simplifyPoints(const std::vector<MatchedPoint>& points, double toleranceSecs)
{
    if (points.size() <= 2) {
        return points;
    }

    // Douglas-Peucker on (ticks, seconds): a straight line between two kept points is a constant tempo
    std::vector<bool> keep(points.size(), false);
    keep.front() = keep.back() = true;

    std::vector<std::pair<size_t, size_t> > stack { { 0, points.size() - 1 } };
    while (!stack.empty()) {
        const auto [from, to] = stack.back();
        stack.pop_back();
        if (to <= from + 1) {
            continue;
        }

        const MatchedPoint& a = points[from];
        const MatchedPoint& b = points[to];
        double maxError = 0.0;
        size_t maxIndex = from;
        for (size_t i = from + 1; i < to; ++i) {
            const double predicted = a.secs + (b.secs - a.secs) * static_cast<double>(points[i].utick - a.utick)
                                     / static_cast<double>(b.utick - a.utick);
            const double error = std::abs(points[i].secs - predicted);
            if (error > maxError) {
                maxError = error;
                maxIndex = i;
            }
        }

        if (maxError > toleranceSecs) {
            keep[maxIndex] = true;
            stack.push_back({ from, maxIndex });
            stack.push_back({ maxIndex, to });
        }
    }

    std::vector<MatchedPoint> result;
    for (size_t i = 0; i < points.size(); ++i) {
        if (keep[i]) {
            result.push_back(points[i]);
        }
    }
    return result;
}

int firstNoteUtick(const Score* score)
{
    if (!score) {
        return 0;
    }

    for (const Measure* measure = score->firstMeasure(); measure; measure = measure->nextMeasure()) {
        for (const Segment* segment = measure->first(SegmentType::ChordRest); segment; segment = segment->next(SegmentType::ChordRest)) {
            for (track_idx_t track = 0; track < score->ntracks(); ++track) {
                const EngravingItem* element = segment->element(track);
                if (element && element->isChord()) {
                    return score->expandedRepeatList().tick2utick(segment->tick().ticks());
                }
            }
        }
    }

    return 0;
}

double scoreStartSecsForFirstNote(const Score* score, const std::vector<MatchedPoint>& points, int firstNoteUtick, double firstNoteSecs)
{
    if (firstNoteUtick <= 0) {
        return firstNoteSecs;
    }

    // The tempo of the first matched segment (the music's own), else the score's current one
    double secsPerTick = 0.0;
    for (size_t i = 0; i + 1 < points.size(); ++i) {
        if (points[i + 1].utick > points[i].utick && points[i + 1].secs > points[i].secs) {
            secsPerTick = (points[i + 1].secs - points[i].secs) / static_cast<double>(points[i + 1].utick - points[i].utick);
            break;
        }
    }

    if (secsPerTick <= 0.0 && score) {
        const TempoTimeline& timeline = score->tempoTimeline(true);
        return firstNoteSecs - timeline.utick2utime(firstNoteUtick) * timeline.tempoMultiplier().val;
    }

    return firstNoteSecs - secsPerTick * static_cast<double>(firstNoteUtick);
}

AppliedDetection applyDetectedPoints(INotationAutomation& automation, const Score* score, const std::vector<MatchedPoint>& points,
                                     std::optional<double> scoreStartSecs)
{
    AppliedDetection result;

    const AutomationDataConstPtr data = automation.automationData();
    if (!data || !data->isAudioSyncEnabled() || !score || points.empty()) {
        return result;
    }

    const int fromUtick = points.front().utick;

    //! NOTE What stays: the points before the detection's start, and the locked ones. Those after its last point
    //! go too: left at their former times (e.g. the measures' at the written tempo), they'd keep the detected
    //! ones out (order and tempo range)
    const std::vector<SyncPoint> existing = syncPoints(automation);
    std::map<int, SyncPoint> kept;
    AutomationPointEdits edits;
    for (size_t i = 0; i < existing.size(); ++i) {
        SyncPoint point = existing[i];
        const bool inRange = point.utick > fromUtick;
        if (i == 0 && scoreStartSecs && !point.locked) {
            point.audioSecs = *scoreStartSecs;
        }
        if (!inRange || point.locked || i == 0) {
            kept[point.utick] = point;
        } else {
            if (point.origin == AudioSyncInfo::Origin::Manual) {
                ++result.replacedManual;
            }
            edits.push_back({ point.utick, AutomationPointEdit::ErasePoint {} });
        }
    }

    // The score's start
    if (scoreStartSecs && !existing.empty() && !existing.front().locked) {
        const auto it = data->curve(TEMPO_KEY).find(existing.front().utick);
        if (it != data->curve(TEMPO_KEY).end() && it->second.audioSync) {
            AutomationPoint startPoint = it->second;
            startPoint.audioSync->audioSecs = *scoreStartSecs;
            startPoint.audioSync->origin = AudioSyncInfo::Origin::Detected;
            if (!(startPoint == it->second)) {
                edits.push_back({ existing.front().utick, AutomationPointEdit::SetPoint { startPoint } });
            }
        }
    }

    // The detected ones, where they fit between what stays (order and tempo range)
    int added = 0;
    for (const MatchedPoint& point : points) {
        if (kept.count(point.utick)) {
            continue;
        }

        std::vector<SyncPoint> neighbors;
        for (const auto& [utick, syncPoint] : kept) {
            neighbors.push_back(syncPoint);
        }
        const std::optional<std::pair<double, double> > range = allowedAudioRange(neighbors, point.utick);
        if (!range || point.secs < range->first || point.secs > range->second) {
            continue;
        }

        SyncPoint syncPoint;
        syncPoint.utick = point.utick;
        syncPoint.audioSecs = point.secs;
        syncPoint.origin = AudioSyncInfo::Origin::Detected;
        syncPoint.confidence = point.confidence;
        kept[point.utick] = syncPoint;

        AutomationPoint automationPoint;
        automationPoint.value.inValue = AutomationPoint::ArrivalFromPrevious {};
        automationPoint.value.outValue = normalizeTempo(score->tempoTimeline(true).tempo(point.utick));
        AudioSyncInfo sync;
        sync.audioSecs = point.secs;
        sync.origin = AudioSyncInfo::Origin::Detected;
        sync.confidence = point.confidence;
        automationPoint.audioSync = sync;

        // Replaces an erase at the same tick, if any
        edits.erase(std::remove_if(edits.begin(), edits.end(), [&point](const AutomationPointEdit& edit) {
            return edit.tick == point.utick;
        }), edits.end());
        edits.push_back({ point.utick, AutomationPointEdit::SetPoint { automationPoint } });
        ++result.added;
    }

    if (edits.empty()) {
        return result;
    }

    std::vector<std::pair<AutomationCurveKey, AutomationPointEdits> > editsByCurve { { TEMPO_KEY, edits } };
    automation.editPoints(editsByCurve, muse::TranslatableString("undoableAction", "Detect audio sync points"));
    return result;
}
}
