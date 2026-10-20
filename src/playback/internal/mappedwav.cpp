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

#include "mappedwav.h"

#include <algorithm>
#include <cstring>

#include <QFile>

#include "log.h"

using namespace mu::playback;

static uint16_t readLE16(const uchar* p)
{
    return static_cast<uint16_t>(p[0] | (p[1] << 8));
}

static uint32_t readLE32(const uchar* p)
{
    return static_cast<uint32_t>(p[0]) | (static_cast<uint32_t>(p[1]) << 8) | (static_cast<uint32_t>(p[2]) << 16)
           | (static_cast<uint32_t>(p[3]) << 24);
}

MappedWav::~MappedWav() = default;

std::shared_ptr<MappedWav> MappedWav::open(const QString& path)
{
    auto wav = std::make_shared<MappedWav>();
    wav->file = std::make_unique<QFile>(path);
    if (!wav->file->open(QIODevice::ReadOnly)) {
        LOGE() << "unable to open " << path;
        return nullptr;
    }

    const qint64 size = wav->file->size();
    const uchar* map = wav->file->map(0, size);
    if (!map || size < 12 || std::memcmp(map, "RIFF", 4) != 0 || std::memcmp(map + 8, "WAVE", 4) != 0) {
        LOGE() << "not a WAV file: " << path;
        return nullptr;
    }

    int format = 0;
    int bitsPerSample = 0;
    qint64 pos = 12;
    while (pos + 8 <= size) {
        const uchar* chunk = map + pos;
        const qint64 chunkSize = readLE32(chunk + 4);
        if (std::memcmp(chunk, "fmt ", 4) == 0 && chunkSize >= 16 && pos + 8 + 16 <= size) {
            format = readLE16(chunk + 8);
            wav->channels = readLE16(chunk + 10);
            wav->sampleRate = static_cast<int>(readLE32(chunk + 12));
            bitsPerSample = readLE16(chunk + 22);
        } else if (std::memcmp(chunk, "data", 4) == 0) {
            if (format != 1 || bitsPerSample != 16 || wav->channels <= 0 || wav->sampleRate <= 0) {
                LOGE() << "unsupported WAV format: " << path;
                return nullptr;
            }

            //! NOTE A truncated file: what's there
            const qint64 dataBytes = std::min(chunkSize, size - (pos + 8));
            wav->samples = reinterpret_cast<const int16_t*>(chunk + 8);
            wav->frames = dataBytes / (2 * wav->channels);
            return wav;
        }

        pos += 8 + chunkSize + (chunkSize & 1);
    }

    LOGE() << "no audio data in " << path;
    return nullptr;
}

float MappedWav::monoSample(int64_t frame) const
{
    int sum = 0;
    const int16_t* p = samples + frame * channels;
    for (int ch = 0; ch < channels; ++ch) {
        sum += p[ch];
    }
    return static_cast<float>(sum) / (32768.f * static_cast<float>(channels));
}
