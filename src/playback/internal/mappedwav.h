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

#include <cstdint>
#include <memory>

#include <QString>

class QFile;

namespace mu::playback {
//! NOTE A 16-bit PCM WAV file memory-mapped, read in place: the OS pages in what's read (e.g. the Audio sync
//! window's decoded reference audio, see VideoAudioDecoder)
struct MappedWav {
    std::unique_ptr<QFile> file;
    const int16_t* samples = nullptr; // interleaved, inside the mapping
    int64_t frames = 0;
    int channels = 0;
    int sampleRate = 0;

    ~MappedWav();

    //! NOTE nullptr if it can't be read (not a 16-bit PCM WAV file)
    static std::shared_ptr<MappedWav> open(const QString& path);

    float sample(int channel, int64_t frame) const
    {
        return samples[frame * channels + channel] / 32768.f;
    }

    //! NOTE All channels mixed
    float monoSample(int64_t frame) const;
};
}
