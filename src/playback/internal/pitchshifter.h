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

#include <algorithm>
#include <atomic>
#include <functional>

#include <QString>

namespace mu::playback::audiosync {
//! NOTE The speeds transformWav() supports (clamped to them): the played speed outside of them isn't the score's one
constexpr double MIN_TRANSFORM_SPEED = 0.25;
constexpr double MAX_TRANSFORM_SPEED = 2.0;

inline double clampTransformSpeed(double speed)
{
    return std::clamp(speed, MIN_TRANSFORM_SPEED, MAX_TRANSFORM_SPEED);
}

//! NOTE Writes a 16-bit PCM WAV file (e.g. the reference audio, decoded) transposed by semitones and played at speed
//! (its length divided by it), each one without changing the other: a phase vocoder (with identity phase locking)
//! stretches it, then it's resampled. Progress in [0; 1]; false if canceled or the file can't be read/written
bool transformWav(const QString& inPath, const QString& outPath, int semitones, double speed, const std::atomic<bool>& canceled,
                  const std::function<void(double)>& progress);
}
