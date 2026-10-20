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
#include <vector>

namespace mu::playback {
struct MappedWav;
}

namespace mu::playback::audiosync {
//! NOTE Beat detection in the reference audio, for the Audio sync window's automatic sync points: onsets by
//! spectral flux, then beat tracking by BeatRoot (Simon Dixon, GPL, already used by MIDI import)
struct BeatDetectionParams {
    //! NOTE In [0; 1]: higher finds weaker attacks
    double sensitivity = 0.5;
    double minBpm = 50.0;
    double maxBpm = 200.0;
    //! NOTE The part of the audio to analyze (to < 0: until the end)
    double fromSecs = 0.0;
    double toSecs = -1.0;
};

struct Onset {
    double secs = 0.0;
    double salience = 0.0; // normalized, around 1 for a typical attack
};

struct DetectedBeat {
    double secs = 0.0;
    //! NOTE In [0; 1]: high when an attack is there, low for a beat the tracker filled in
    float confidence = 1.f;
};

using CancelFlag = std::atomic<bool>;
using ProgressFn = std::function<void (double progress)>;

std::vector<Onset> detectOnsets(const MappedWav& wav, const BeatDetectionParams& params, const CancelFlag& canceled,
                                const ProgressFn& progress);

std::vector<DetectedBeat> trackBeats(const std::vector<Onset>& onsets, const BeatDetectionParams& params);

//! NOTE Both steps; progress in [0; 1]
std::vector<DetectedBeat> detectBeats(const MappedWav& wav, const BeatDetectionParams& params, const CancelFlag& canceled,
                                      const ProgressFn& progress);
}
