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

#pragma once

#include <optional>
#include <utility>
#include <vector>

#include "engraving/automation/automationtypes.h"
#include "notation/inotationautomation.h"

namespace mu::engraving {
class Score;
}

namespace mu::playback::audiosync {
//! NOTE A segment between two audio sync points can't be played faster or slower than this
inline constexpr double MIN_SYNC_BPM = 10.0;
inline constexpr double MAX_SYNC_BPM = 400.0;

struct SyncPoint {
    int utick = 0;
    double audioSecs = 0.0;
    engraving::AudioSyncInfo::Origin origin = engraving::AudioSyncInfo::Origin::Manual;
    bool locked = false;
    float confidence = 1.f;
};

//! NOTE The audio sync points (in use or not, see INotationAutomation::isAudioSyncEnabled()), by utick
std::vector<SyncPoint> syncPoints(const notation::INotationAutomation& automation);

//! NOTE The tempo (BPM, quarter notes) of the segment from points[index] to the next one; nullopt for the last one
std::optional<double> segmentBpm(const std::vector<SyncPoint>& points, size_t index);

//! NOTE The audio times a sync point at utick can have, the neighbors' segments staying within MIN/MAX_SYNC_BPM
//! (the point at utick itself, if any, ignored); nullopt if none
std::optional<std::pair<double, double> > allowedAudioRange(const std::vector<SyncPoint>& points, int utick);

//! NOTE The nearest measure start or beat (the metronome's beats) to a utick of the unrolled score
std::optional<int> nearestBeatUtick(const engraving::Score* score, int utick);

//! NOTE Undoable edits; false if refused (out of the allowed range, initial point...)
bool addPoint(notation::INotationAutomation& automation, const engraving::Score* score, int utick, double audioSecs);
bool movePoint(notation::INotationAutomation& automation, int utick, double audioSecs);
bool removePoint(notation::INotationAutomation& automation, int utick);
}
