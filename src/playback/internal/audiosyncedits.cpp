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

#include "engraving/automation/tempovalues.h"
#include "engraving/dom/measure.h"
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
}
