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
#include <array>
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

std::optional<std::pair<double, double> > allowedMoveRange(const std::vector<SyncPoint>& points, int utick, bool moveFollowing)
{
    if (!moveFollowing) {
        return allowedAudioRange(points, utick);
    }

    // The ones after it move with it: only the segment before it changes
    std::vector<SyncPoint> before;
    for (const SyncPoint& point : points) {
        if (point.utick <= utick) {
            before.push_back(point);
        }
    }
    return allowedAudioRange(before, utick);
}

bool movePoint(INotationAutomation& automation, int utick, double audioSecs, bool moveFollowing)
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

    const std::optional<std::pair<double, double> > range = allowedMoveRange(syncPoints(automation), utick, moveFollowing);
    if (!range) {
        return false;
    }

    AutomationPoint point = it->second;
    point.audioSync->audioSecs = std::clamp(audioSecs, range->first, range->second);
    point.audioSync->origin = AudioSyncInfo::Origin::Manual;
    if (point == it->second) {
        return false;
    }

    AutomationPointEdits edits { { utick, AutomationPointEdit::SetPoint { point } } };
    if (moveFollowing) {
        const double deltaSecs = point.audioSync->audioSecs - it->second.audioSync->audioSecs;
        for (auto next = std::next(it); next != curve.end(); ++next) {
            if (!next->second.audioSync) {
                continue;
            }
            if (next->second.audioSync->locked) {
                break;
            }
            AutomationPoint moved = next->second;
            moved.audioSync->audioSecs += deltaSecs;
            edits.push_back({ next->first, AutomationPointEdit::SetPoint { moved } });
        }
    }

    std::vector<std::pair<AutomationCurveKey, AutomationPointEdits> > editsByCurve { { TEMPO_KEY, edits } };
    automation.editPoints(editsByCurve, moveFollowing ? muse::TranslatableString("undoableAction", "Move audio sync points")
                          : muse::TranslatableString("undoableAction", "Move audio sync point"));
    return true;
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
    float anchorStrength = 0.f;
    if (nearest != beats.end() && std::abs(nearest->secs - options.anchorSecs) < medianInterval * 0.5) {
        anchorStrength = nearest->strength;
        if (!options.keepAnchor) {
            anchorSecs = nearest->secs;
        }
    }

    audioBeats.push_back({ anchorSecs, 1.f, anchorStrength });
    for (const DetectedBeatTime& beat : beats) {
        if (beat.secs > anchorSecs + medianInterval * 0.5) {
            audioBeats.push_back(beat);
        }
    }

    if (info) {
        info->anchorSecs = anchorSecs;
        info->detectedBpm = 60.0 / medianInterval;
    }

    // The measures' first beats on the strongest ones: the audio's first beats until there are then before the
    // score's start (a pickup, a count-in)
    if (options.findDownbeats) {
        const auto secondMeasure = std::find_if(std::next(positions.begin()), positions.end(), [](const auto& position) {
            return position.second;
        });
        const size_t beatsPerMeasure = static_cast<size_t>(std::distance(positions.begin(), secondMeasure));
        //! NOTE Judged on the first measures: the time signature may change later
        static constexpr size_t MEASURES_JUDGED = 16;

        size_t bestShift = 0;
        double bestStrength = -1.0;
        for (size_t shift = 0; shift < beatsPerMeasure && secondMeasure != positions.end(); ++shift) {
            double strength = 0.0;
            size_t measures = 0;
            for (size_t i = 0; i < positions.size() && i + shift < audioBeats.size() && measures < MEASURES_JUDGED; ++i) {
                if (positions[i].second) {
                    strength += audioBeats[i + shift].strength;
                    ++measures;
                }
            }
            if (measures > 0 && strength / static_cast<double>(measures) > bestStrength) {
                bestStrength = strength / static_cast<double>(measures);
                bestShift = shift;
            }
        }

        if (bestShift > 0) {
            audioBeats.erase(audioBeats.begin(), audioBeats.begin() + static_cast<std::ptrdiff_t>(bestShift));
            if (info) {
                info->anchorSecs = audioBeats.front().secs;
            }
        }
    }

    // Measure after measure, on the audio's beats: a measure may get one beat more or less than the score says (a
    // beat the tracking missed or added: the tempo drifting from the tracked one), at a cost, made up for by
    // the strong attack its first beat gets. Counted beat after beat, one such beat would shift every measure after.
    // A tempo jumping from a measure to the next costs too: the strong attacks of music not in the score's time
    // signatures would otherwise be reached by squeezing or stretching measures
    //! NOTE Boundaries: the anchor, then the measure starts
    std::vector<size_t> boundaries { 0 };
    for (size_t i = 1; i < positions.size(); ++i) {
        if (positions[i].second) {
            boundaries.push_back(i);
        }
    }

    //! NOTE Only for a variable tempo (flexibility above the middle): the beats then miss or add some,
    //! that the strong attacks put right. With a constant tempo the beats are right, and a slightly stronger beat
    //! (a backbeat's snare drum), measure after measure, would shift all the measures by one beat for good
    const bool slipsAllowed = options.flexibility > 0.5;
    const double variable = std::clamp((options.flexibility - 0.5) * 2.0, 0.0, 1.0);
    const double slipCost = 0.8 * std::pow(0.11 / 0.8, variable); // in strengths (the strongest beat's is 1)
    const double tempoChangeCost = 3.2 * std::pow(1.0 / 3.2, variable); // per tempo ratio (natural log)
    //! NOTE The first measure's tempo is compared to the beats'
    const double beatsBps = 1.0 / medianInterval;
    static constexpr double UNREACHED = -std::numeric_limits<double>::infinity();
    const int beatCount = static_cast<int>(audioBeats.size());
    auto beatsOf = [&boundaries](size_t k) { return static_cast<int>(boundaries[k + 1] - boundaries[k]); };

    //! NOTE State: the k-th boundary on the audio's i-th beat, reached with a slip (-1, 0, +1: index slip + 1)
    struct State {
        double score = UNREACHED;
        int previousSlip = 0;
    };
    std::vector<std::vector<std::array<State, 3> > > states(boundaries.size(), std::vector<std::array<State, 3> >(beatCount));
    states[0][0][1].score = audioBeats[0].strength;

    for (size_t k = 0; k + 1 < boundaries.size(); ++k) {
        const int beats = beatsOf(k);
        for (int i = 0; i < beatCount; ++i) {
            for (int slip = -1; slip <= 1; ++slip) {
                const double score = states[k][i][slip + 1].score;
                if (score == UNREACHED) {
                    continue;
                }

                double previousBps = beatsBps;
                if (k > 0) {
                    const int previousIdx = i - beatsOf(k - 1) - slip;
                    previousBps = beatsOf(k - 1) / (audioBeats[i].secs - audioBeats[previousIdx].secs);
                }

                for (int nextSlip = slipsAllowed ? -1 : 0; nextSlip <= (slipsAllowed ? 1 : 0); ++nextSlip) {
                    const int next = i + beats + nextSlip;
                    if (next <= i || next >= beatCount || audioBeats[next].secs <= audioBeats[i].secs) {
                        continue;
                    }

                    const double bps = beats / (audioBeats[next].secs - audioBeats[i].secs);
                    const double tempoChange = std::abs(std::log(bps / previousBps));
                    const double nextScore = score + audioBeats[next].strength - slipCost * std::abs(nextSlip)
                                             - tempoChangeCost * tempoChange;
                    State& nextState = states[k + 1][next][nextSlip + 1];
                    if (nextScore > nextState.score) {
                        nextState.score = nextScore;
                        nextState.previousSlip = slip;
                    }
                }
            }
        }
    }

    //! NOTE Back from the best end: the score's end, or where the audio ends (the next measure, kept to the score's
    //! beats, past its last beat). Ending at the furthest boundary the audio can reach instead squeezes measures,
    //! one beat short each, to fit a score longer than the audio
    double bestEndScore = UNREACHED;
    size_t lastReached = 0;
    int beatIdx = 0;
    int endSlip = 0;
    for (size_t k = 0; k < boundaries.size(); ++k) {
        const int nextBeats = k + 1 < boundaries.size() ? beatsOf(k) : 0;
        for (int i = 0; i < beatCount; ++i) {
            const bool canEnd = k + 1 == boundaries.size() || i + nextBeats >= beatCount;
            for (int slip = -1; slip <= 1 && canEnd; ++slip) {
                if (states[k][i][slip + 1].score > bestEndScore) {
                    bestEndScore = states[k][i][slip + 1].score;
                    lastReached = k;
                    beatIdx = i;
                    endSlip = slip;
                }
            }
        }
    }

    std::vector<int> beatIdxs(lastReached + 1, 0);
    for (size_t k = lastReached + 1; k-- > 0;) {
        beatIdxs[k] = beatIdx;
        if (k > 0) {
            const int previousSlip = states[k][beatIdx][endSlip + 1].previousSlip;
            beatIdx -= beatsOf(k - 1) + endSlip;
            endSlip = previousSlip;
        }
    }

    for (size_t k = 0; k <= lastReached; ++k) {
        const std::pair<int, bool>& position = positions[boundaries[k]];
        if (position.second) {
            const DetectedBeatTime& beat = audioBeats[beatIdxs[k]];
            result.push_back({ position.first, beat.secs, beat.confidence });
        }
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
        return -1;
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

    return -1;
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
