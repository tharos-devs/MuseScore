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

#include "referenceaudiopeaks.h"

#include "playback/internal/mappedwav.h"

#include <algorithm>
#include <cstring>
#include <limits>
#include <thread>

#include <QCoreApplication>
#include <QMetaObject>
#include <QPointer>

#include "log.h"

using namespace mu::playback;

namespace {
constexpr int64_t BASE_BIN_FRAMES = 64;
constexpr int64_t LEVEL_FACTOR = 4;
constexpr int MAX_LEVELS = 8;

struct Bin {
    int16_t min = 0;
    int16_t max = 0;
};
}

struct ReferenceAudioPeaks::Data {
    std::shared_ptr<MappedWav> wav;
    const int16_t* samples = nullptr; // interleaved, inside the mapping
    int64_t frames = 0;
    int channels = 0;
    int sampleRate = 0;
    bool identicalChannels = true;

    //! NOTE levels[l]: (BASE_BIN_FRAMES * LEVEL_FACTOR^l) frames per bin, bins interleaved by channel
    std::vector<std::vector<Bin> > levels;

    int64_t binFrames(size_t level) const
    {
        int64_t result = BASE_BIN_FRAMES;
        for (size_t i = 0; i < level; ++i) {
            result *= LEVEL_FACTOR;
        }
        return result;
    }
};

struct ReferenceAudioPeaks::Job {
    std::atomic<bool> canceled { false };
};

ReferenceAudioPeaks::ReferenceAudioPeaks(QObject* parent)
    : QObject(parent)
{
}

ReferenceAudioPeaks::~ReferenceAudioPeaks()
{
    if (m_job) {
        m_job->canceled = true;
    }
}

const QString& ReferenceAudioPeaks::wavPath() const
{
    return m_wavPath;
}

bool ReferenceAudioPeaks::isReady() const
{
    return m_data != nullptr;
}

bool ReferenceAudioPeaks::failed() const
{
    return m_failed;
}

int ReferenceAudioPeaks::sampleRate() const
{
    return m_data ? m_data->sampleRate : 0;
}

int ReferenceAudioPeaks::channels() const
{
    return m_data ? m_data->channels : 0;
}

bool ReferenceAudioPeaks::isStereo() const
{
    return m_data && m_data->channels > 1 && !m_data->identicalChannels;
}

int64_t ReferenceAudioPeaks::frames() const
{
    return m_data ? m_data->frames : 0;
}

double ReferenceAudioPeaks::durationSecs() const
{
    return m_data && m_data->sampleRate > 0 ? static_cast<double>(m_data->frames) / m_data->sampleRate : 0.0;
}

void ReferenceAudioPeaks::load(const QString& wavPath)
{
    if (wavPath == m_wavPath) {
        return;
    }

    if (m_job) {
        m_job->canceled = true;
        m_job = nullptr;
    }

    const bool wasReady = m_data != nullptr;
    m_wavPath = wavPath;
    m_data = nullptr;
    m_failed = false;

    if (wavPath.isEmpty()) {
        if (wasReady) {
            emit readyChanged();
        }
        return;
    }

    auto job = std::make_shared<Job>();
    m_job = job;

    QPointer<ReferenceAudioPeaks> self(this);

    std::thread([self, job, wavPath]() {
        std::shared_ptr<Data> data = open(wavPath);
        if (data && !job->canceled) {
            computePeaks(*data, job->canceled);
        }

        if (job->canceled) {
            return;
        }

        QMetaObject::invokeMethod(qApp, [self, job, data]() {
            if (!self || self->m_job != job) {
                return;
            }

            self->m_job = nullptr;
            self->m_data = data;
            self->m_failed = data == nullptr;
            emit self->readyChanged();
        }, Qt::QueuedConnection);
    }).detach();

    if (wasReady) {
        emit readyChanged();
    }
}

std::shared_ptr<ReferenceAudioPeaks::Data> ReferenceAudioPeaks::open(const QString& wavPath)
{
    std::shared_ptr<MappedWav> wav = MappedWav::open(wavPath);
    if (!wav) {
        return nullptr;
    }

    auto data = std::make_shared<Data>();
    data->samples = wav->samples;
    data->frames = wav->frames;
    data->channels = wav->channels;
    data->sampleRate = wav->sampleRate;
    data->wav = std::move(wav);
    return data;
}

void ReferenceAudioPeaks::computePeaks(Data& data, const std::atomic<bool>& canceled)
{
    const int channels = data.channels;

    // Level 0, from the samples
    {
        const int64_t bins = (data.frames + BASE_BIN_FRAMES - 1) / BASE_BIN_FRAMES;
        std::vector<Bin> level(static_cast<size_t>(bins * channels));

        for (int64_t b = 0; b < bins; ++b) {
            if ((b & 0xffff) == 0 && canceled) {
                return;
            }

            const int64_t frameFrom = b * BASE_BIN_FRAMES;
            const int64_t frameTo = std::min(frameFrom + BASE_BIN_FRAMES, data.frames);

            if (data.identicalChannels && channels > 1) {
                for (int64_t f = frameFrom; f < frameTo && data.identicalChannels; ++f) {
                    const int16_t* frame = data.samples + f * channels;
                    for (int ch = 1; ch < channels; ++ch) {
                        if (frame[ch] != frame[0]) {
                            data.identicalChannels = false;
                            break;
                        }
                    }
                }
            }
            for (int ch = 0; ch < channels; ++ch) {
                int16_t mn = std::numeric_limits<int16_t>::max();
                int16_t mx = std::numeric_limits<int16_t>::min();
                for (int64_t f = frameFrom; f < frameTo; ++f) {
                    const int16_t s = data.samples[f * channels + ch];
                    mn = std::min(mn, s);
                    mx = std::max(mx, s);
                }
                level[static_cast<size_t>(b * channels + ch)] = { mn, mx };
            }
        }

        data.levels.push_back(std::move(level));
    }

    // Coarser levels, from the previous one
    while (data.levels.size() < MAX_LEVELS) {
        const std::vector<Bin>& prev = data.levels.back();
        const int64_t prevBins = static_cast<int64_t>(prev.size()) / channels;
        if (prevBins <= 1) {
            break;
        }

        const int64_t bins = (prevBins + LEVEL_FACTOR - 1) / LEVEL_FACTOR;
        std::vector<Bin> level(static_cast<size_t>(bins * channels));
        for (int64_t b = 0; b < bins; ++b) {
            for (int ch = 0; ch < channels; ++ch) {
                Bin result { std::numeric_limits<int16_t>::max(), std::numeric_limits<int16_t>::min() };
                for (int64_t p = b * LEVEL_FACTOR; p < std::min((b + 1) * LEVEL_FACTOR, prevBins); ++p) {
                    const Bin& bin = prev[static_cast<size_t>(p * channels + ch)];
                    result.min = std::min(result.min, bin.min);
                    result.max = std::max(result.max, bin.max);
                }
                level[static_cast<size_t>(b * channels + ch)] = result;
            }
        }

        if (canceled) {
            return;
        }

        data.levels.push_back(std::move(level));
    }
}

ReferenceAudioPeaks::MinMax ReferenceAudioPeaks::range(int channel, int64_t frameFrom, int64_t frameTo) const
{
    MinMax result;
    if (!m_data) {
        return result;
    }

    const Data& data = *m_data;
    frameFrom = std::max<int64_t>(frameFrom, 0);
    frameTo = std::min(frameTo, data.frames);
    if (frameTo <= frameFrom) {
        return result;
    }

    const int chFrom = channel < 0 ? 0 : std::min(channel, data.channels - 1);
    const int chTo = channel < 0 ? data.channels - 1 : chFrom;

    int16_t mn = std::numeric_limits<int16_t>::max();
    int16_t mx = std::numeric_limits<int16_t>::min();

    // The coarsest level still having at least one bin in the range (bins straddling its edges included)
    const int64_t span = frameTo - frameFrom;
    size_t level = 0;
    while (level + 1 < data.levels.size() && data.binFrames(level + 1) <= span) {
        ++level;
    }

    if (span < BASE_BIN_FRAMES || data.levels.empty()) {
        for (int64_t f = frameFrom; f < frameTo; ++f) {
            for (int ch = chFrom; ch <= chTo; ++ch) {
                const int16_t s = data.samples[f * data.channels + ch];
                mn = std::min(mn, s);
                mx = std::max(mx, s);
            }
        }
    } else {
        const int64_t binFrames = data.binFrames(level);
        const std::vector<Bin>& bins = data.levels[level];
        const int64_t binCount = static_cast<int64_t>(bins.size()) / data.channels;
        const int64_t binFrom = frameFrom / binFrames;
        const int64_t binTo = std::min((frameTo + binFrames - 1) / binFrames, binCount);
        for (int64_t b = binFrom; b < binTo; ++b) {
            for (int ch = chFrom; ch <= chTo; ++ch) {
                const Bin& bin = bins[static_cast<size_t>(b * data.channels + ch)];
                mn = std::min(mn, bin.min);
                mx = std::max(mx, bin.max);
            }
        }
    }

    result.min = mn / 32768.f;
    result.max = mx / 32768.f;
    result.valid = true;
    return result;
}

float ReferenceAudioPeaks::sample(int channel, int64_t frame) const
{
    if (!m_data || frame < 0 || frame >= m_data->frames) {
        return 0.f;
    }

    const Data& data = *m_data;
    if (channel >= 0) {
        return data.samples[frame * data.channels + std::min(channel, data.channels - 1)] / 32768.f;
    }

    float sum = 0.f;
    for (int ch = 0; ch < data.channels; ++ch) {
        sum += data.samples[frame * data.channels + ch];
    }
    return sum / (32768.f * data.channels);
}
