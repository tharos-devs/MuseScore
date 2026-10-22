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

#include <atomic>
#include <functional>
#include <optional>
#include <vector>

namespace mu::playback {
struct MappedWav;
}

namespace mu::playback::audiosync {
//! NOTE Beat detection in the reference audio, for the Audio sync window's automatic sync points: onsets by
//! spectral flux, then beat tracking by dynamic programming over it (D. Ellis, "Beat Tracking by Dynamic
//! Programming", 2007), from the audio alone: the score's tempo and time signatures may not be known yet
struct BeatDetectionParams {
    //! NOTE In [0; 1]: higher finds weaker attacks
    double sensitivity = 0.5;
    //! NOTE In [0; 1]: how much the beats may stray from the tempo to follow the attacks (0: steady, as music played
    //! to a click; 1: flexible, as rubato)
    double flexibility = 0.5;
    //! NOTE The tempo of the beats as the user knows it (typed or tapped), roughly; 0: unknown
    double approxBpm = 0.0;
    //! NOTE The part of the audio to analyze (to < 0: until the end)
    double fromSecs = 0.0;
    double toSecs = -1.0;
};

//! NOTE How much the sound changes (spectral flux), every hopSecs from fromSecs
struct OnsetEnvelope {
    double fromSecs = 0.0;
    double hopSecs = 0.01;
    std::vector<double> values;
};

struct Onset {
    double secs = 0.0;
    double salience = 0.0; // > 0: how far above its surroundings, around 1 for a typical attack
};

struct DetectedBeat {
    double secs = 0.0;
    //! NOTE In [0; 1]: high when an attack is there, low for a beat the tracker filled in
    float confidence = 1.f;
    //! NOTE How strong the attack on it is (0: none), relative to the others: the measures' first beats are
    //! usually the strongest ones
    float strength = 0.f;
};

using CancelFlag = std::atomic<bool>;
using ProgressFn = std::function<void (double progress)>;

OnsetEnvelope onsetEnvelope(const MappedWav& wav, const BeatDetectionParams& params, const CancelFlag& canceled,
                            const ProgressFn& progress);

std::vector<Onset> detectOnsets(const OnsetEnvelope& envelope, const BeatDetectionParams& params);

//! NOTE The tempo of the beats (BPM): the period that repeats the most in the envelope, the ones near the approximate
//! tempo preferred, else near 120 BPM (the octave a listener most likely taps); 0 if none
double estimateTempo(const OnsetEnvelope& envelope, const BeatDetectionParams& params);

//! NOTE Beats every 60/bpm s or so, on the strongest attacks
std::vector<DetectedBeat> trackBeats(const OnsetEnvelope& envelope, const std::vector<Onset>& onsets, double bpm, double flexibility);

//! NOTE A cost from a flexibility in [0; 1]: steadyCost at 0, middleCost at 0.5 (the default), in between and beyond
//! on a log scale
double flexibilityCost(double flexibility, double steadyCost, double middleCost);

//! NOTE Where the music starts, after the silence the audio may start with: the first sustained sound above
//! the silence threshold (relative to the loudest part); nullopt if the audio is silent
std::optional<double> findMusicStart(const MappedWav& wav, const CancelFlag& canceled);

//! NOTE Both steps; progress in [0; 1]
std::vector<DetectedBeat> detectBeats(const MappedWav& wav, const BeatDetectionParams& params, const CancelFlag& canceled,
                                      const ProgressFn& progress);
}
