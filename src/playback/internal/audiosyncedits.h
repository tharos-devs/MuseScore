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

//! NOTE Detected beats turned into sync points (see beatdetection.h)
struct DetectedBeatTime {
    double secs = 0.0;
    float confidence = 1.f;
};

struct DetectionOptions {
    enum class Octave {
        Auto,   // the audio's beats taken twice as fast/slow as detected if out of [minBpm; maxBpm]
        None,
        Double, // two score beats per detected beat
        Half,   // one score beat every two detected beats
    };

    //! NOTE The score position (normally a measure's start) at a known audio time, where the matching starts
    int anchorUtick = 0;
    double anchorSecs = 0.0;
    //! NOTE Where it ends (< 0: the score's end)
    int toUtick = -1;
    Octave octave = Octave::Auto;
    //! NOTE The expected tempo of the score's beats (Octave::Auto)
    double minBpm = 50.0;
    double maxBpm = 200.0;
    //! NOTE A point on every measure; otherwise only where the tempo changes (see simplifyPoints())
    bool everyMeasure = false;
    double toleranceSecs = 0.02;
};

//! NOTE Score positions with their audio times, by utick
struct MatchedPoint {
    int utick = 0;
    double secs = 0.0;
    float confidence = 1.f;
};

//! NOTE What the matching found, to tell the user
struct MatchInfo {
    double detectedBpm = 0.0; // the beats as detected
    DetectionOptions::Octave octave = DetectionOptions::Octave::None; // as applied (never Auto)
    double appliedBpm = 0.0; // the beats the score's beats were matched to
    //! NOTE The anchor's audio time, moved onto the nearest detected beat (within half a beat): placed by hand or
    //! found from the music's start, it's rarely exactly on it, and the beats after it would be off by one beat
    double anchorSecs = 0.0;
};

//! NOTE The measure starts matched to detected beats, from the anchor on
std::vector<MatchedPoint> matchMeasures(const engraving::Score* score, const std::vector<DetectedBeatTime>& beats,
                                        const DetectionOptions& options, MatchInfo* info = nullptr);

//! NOTE Only the points the others can't be told apart from, within a tolerance, with a constant tempo between them
//! (the first and last ones kept)
std::vector<MatchedPoint> simplifyPoints(const std::vector<MatchedPoint>& points, double toleranceSecs);

//! NOTE The first note of the unrolled score (its first chord, rests before it skipped); 0 if none
int firstNoteUtick(const engraving::Score* score);

//! NOTE Where the score's start falls when its first note is at firstNoteSecs, its first beats being as long as the
//! matched points say (or as long as at the current tempo, without any)
double scoreStartSecsForFirstNote(const engraving::Score* score, const std::vector<MatchedPoint>& points, int firstNoteUtick,
                                  double firstNoteSecs);

struct AppliedDetection {
    int added = 0;
    int replacedManual = 0;
};

//! NOTE Writes detected points (undoable, one step): the existing points in their range are replaced, the locked ones
//! never; points that would break the order or the tempo range are dropped. scoreStartSecs: the first sync point
//! (the score's start) moved there too
AppliedDetection applyDetectedPoints(notation::INotationAutomation& automation, const engraving::Score* score,
                                     const std::vector<MatchedPoint>& points, std::optional<double> scoreStartSecs);

//! NOTE Undoable edits; false if refused (out of the allowed range, initial point...)
bool addPoint(notation::INotationAutomation& automation, const engraving::Score* score, int utick, double audioSecs);
bool movePoint(notation::INotationAutomation& automation, int utick, double audioSecs);
bool removePoint(notation::INotationAutomation& automation, int utick);
//! NOTE All of them but the first one (the score's start) and the locked ones, in one undoable step; false if none
bool removeAllPoints(notation::INotationAutomation& automation);
}
