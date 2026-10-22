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

#include "beatdetection.h"

#include <algorithm>
#include <cmath>
#include <complex>
#include <limits>
#include <numeric>

#include "fft.h"
#include "mappedwav.h"

#ifdef MUE_PLAYBACK_HAS_BEATROOT
#include "importexport/midi/thirdparty/beatroot/Agent.h"
#include "importexport/midi/thirdparty/beatroot/BeatTracker.h"
#include "importexport/midi/thirdparty/beatroot/Induction.h"
#endif

#include "log.h"

namespace mu::playback::audiosync {
namespace {
constexpr int FFT_SIZE = 2048;
constexpr double HOP_SECS = 0.01;
constexpr double MIN_ONSET_INTERVAL_SECS = 0.05;
constexpr int PEAK_HALF_WIDTH = 3;       // local maximum over ±3 hops
constexpr int THRESHOLD_HALF_WIDTH = 8;  // adaptive threshold over ±80 ms
constexpr double BEAT_ONSET_TOLERANCE_SECS = 0.04;
}

std::vector<Onset> detectOnsets(const MappedWav& wav, const BeatDetectionParams& params, const CancelFlag& canceled,
                                const ProgressFn& progress)
{
    std::vector<Onset> onsets;

    const int64_t hop = std::max<int64_t>(1, std::llround(wav.sampleRate * HOP_SECS));
    const int64_t frameFrom = std::clamp<int64_t>(std::llround(params.fromSecs * wav.sampleRate), 0, wav.frames);
    const int64_t frameTo = params.toSecs < 0 ? wav.frames
                            : std::clamp<int64_t>(std::llround(params.toSecs * wav.sampleRate), frameFrom, wav.frames);
    if (frameTo - frameFrom < FFT_SIZE) {
        return onsets;
    }

    std::vector<float> window(FFT_SIZE);
    for (int i = 0; i < FFT_SIZE; ++i) {
        window[i] = 0.5f - 0.5f * std::cos(2.f * static_cast<float>(M_PI) * i / (FFT_SIZE - 1));
    }

    // Spectral flux: the sum of the increases of the log-compressed magnitudes, frame after frame (each frame
    // centered on its time)
    const int64_t hops = (frameTo - frameFrom) / hop;
    std::vector<double> flux(static_cast<size_t>(hops), 0.0);
    std::vector<float> previous(FFT_SIZE / 2, 0.f);
    std::vector<std::complex<float> > buffer(FFT_SIZE);

    for (int64_t h = 0; h < hops; ++h) {
        if ((h & 0xff) == 0) {
            if (canceled) {
                return {};
            }
            if (progress) {
                progress(0.9 * static_cast<double>(h) / static_cast<double>(hops));
            }
        }

        const int64_t center = frameFrom + h * hop;
        for (int i = 0; i < FFT_SIZE; ++i) {
            const int64_t frame = center - FFT_SIZE / 2 + i;
            const float s = frame >= 0 && frame < wav.frames ? wav.monoSample(frame) : 0.f;
            buffer[i] = std::complex<float>(s * window[i], 0.f);
        }

        fft(buffer);

        double sum = 0.0;
        for (int k = 1; k < FFT_SIZE / 2; ++k) {
            const float magnitude = std::log1p(100.f * std::abs(buffer[k]));
            sum += std::max(0.f, magnitude - previous[k]);
            previous[k] = magnitude;
        }
        flux[static_cast<size_t>(h)] = sum;
    }

    if (flux.empty()) {
        return onsets;
    }

    // Normalized, then peaks above an adaptive threshold (local mean + a margin set by the sensitivity)
    const double mean = std::accumulate(flux.begin(), flux.end(), 0.0) / static_cast<double>(flux.size());
    double variance = 0.0;
    for (double f : flux) {
        variance += (f - mean) * (f - mean);
    }
    const double deviation = std::sqrt(variance / static_cast<double>(flux.size()));
    if (deviation <= 0.0) {
        return onsets;
    }

    for (double& f : flux) {
        f = (f - mean) / deviation;
    }

    const double delta = 0.1 + (1.0 - std::clamp(params.sensitivity, 0.0, 1.0)) * 1.4;
    const int64_t n = static_cast<int64_t>(flux.size());
    double lastOnsetSecs = -1e9;

    for (int64_t i = 0; i < n; ++i) {
        bool isPeak = true;
        for (int64_t j = std::max<int64_t>(0, i - PEAK_HALF_WIDTH); j <= std::min(n - 1, i + PEAK_HALF_WIDTH) && isPeak; ++j) {
            isPeak = j == i || flux[j] < flux[i] || (flux[j] == flux[i] && j > i);
        }
        if (!isPeak) {
            continue;
        }

        double localMean = 0.0;
        int count = 0;
        for (int64_t j = std::max<int64_t>(0, i - THRESHOLD_HALF_WIDTH); j <= std::min(n - 1, i + THRESHOLD_HALF_WIDTH); ++j) {
            localMean += flux[j];
            ++count;
        }
        localMean /= count;

        if (flux[i] < localMean + delta) {
            continue;
        }

        const double secs = static_cast<double>(frameFrom + i * hop) / wav.sampleRate;
        if (secs - lastOnsetSecs < MIN_ONSET_INTERVAL_SECS) {
            continue;
        }

        //! NOTE How far it stands out from its surroundings: always > 0, BeatRoot takes a negative score for an
        //! agent to delete (the normalized flux itself is negative in quiet passages, and every agent died there)
        onsets.push_back({ secs, flux[i] - localMean });
        lastOnsetSecs = secs;
    }

    return onsets;
}

std::vector<DetectedBeat> trackBeats(const std::vector<Onset>& onsets, const BeatDetectionParams& params)
{
    std::vector<DetectedBeat> beats;

#ifdef MUE_PLAYBACK_HAS_BEATROOT
    if (onsets.size() < 4) {
        return beats;
    }

    //! NOTE Times from the first onset: BeatRoot tries other phases over its first 5 s only
    const double origin = onsets.front().secs;
    EventList events;
    double maxSalience = 0.0;
    for (const Onset& onset : onsets) {
        events.push_back(Event(onset.secs - origin, onset.salience));
        maxSalience = std::max(maxSalience, onset.salience);
    }

    //! NOTE The beats go on through the quiet passages (a held chord, a pause): by default, every agent without an
    //! attack for 10 s is dropped, and none is created after the first 5 s, so none was left after a long one
    AgentParameters agentParams;
    agentParams.expiryTime = std::numeric_limits<double>::max();

    //! NOTE The tempo range BeatRoot looks for (its defaults: 60-200 BPM); static: the beat trackers don't run concurrently
    const double savedMinIBI = Induction::minIBI;
    const double savedMaxIBI = Induction::maxIBI;
    Induction::minIBI = 60.0 / std::max(params.maxBpm, params.minBpm + 1.0);
    Induction::maxIBI = 60.0 / std::max(1.0, params.minBpm);

    std::vector<double> beatTimes = BeatTracker::beatTrack(agentParams, events);
    for (double& secs : beatTimes) {
        secs += origin;
    }

    Induction::minIBI = savedMinIBI;
    Induction::maxIBI = savedMaxIBI;

    // Confidence: the attack (if any) the beat falls on
    size_t onsetIdx = 0;
    for (double secs : beatTimes) {
        while (onsetIdx + 1 < onsets.size() && onsets[onsetIdx + 1].secs <= secs) {
            ++onsetIdx;
        }

        double salience = 0.0;
        for (size_t i = onsetIdx; i <= std::min(onsetIdx + 1, onsets.size() - 1); ++i) {
            if (std::abs(onsets[i].secs - secs) <= BEAT_ONSET_TOLERANCE_SECS) {
                salience = std::max(salience, onsets[i].salience);
            }
        }

        DetectedBeat beat;
        beat.secs = secs;
        beat.confidence = salience > 0.0 ? static_cast<float>(std::clamp(0.5 + 0.5 * salience / std::max(1.0, maxSalience * 0.5), 0.5, 1.0))
                          : 0.3f;
        beats.push_back(beat);
    }
#else
    UNUSED(onsets);
    UNUSED(params);
    LOGW() << "beat tracking isn't available in this build";
#endif

    return beats;
}

std::optional<double> findMusicStart(const MappedWav& wav, const CancelFlag& canceled)
{
    constexpr double WINDOW_SECS = 0.01;
    constexpr double SILENCE_DB = -40.0; // below the loudest window
    constexpr int SUSTAINED_WINDOWS = 5; // 50 ms: not a click or a breath

    const int64_t window = std::max<int64_t>(1, std::llround(wav.sampleRate * WINDOW_SECS));
    const int64_t windows = wav.frames / window;
    if (windows <= 0) {
        return std::nullopt;
    }

    std::vector<float> rms(static_cast<size_t>(windows));
    float peak = 0.f;
    for (int64_t w = 0; w < windows; ++w) {
        if ((w & 0xfff) == 0 && canceled) {
            return std::nullopt;
        }

        double sum = 0.0;
        for (int64_t f = w * window; f < (w + 1) * window; ++f) {
            const float s = wav.monoSample(f);
            sum += s * s;
        }
        rms[static_cast<size_t>(w)] = static_cast<float>(std::sqrt(sum / static_cast<double>(window)));
        peak = std::max(peak, rms[static_cast<size_t>(w)]);
    }

    if (peak <= 0.f) {
        return std::nullopt;
    }

    const float threshold = peak * static_cast<float>(std::pow(10.0, SILENCE_DB / 20.0));
    int run = 0;
    for (int64_t w = 0; w < windows; ++w) {
        run = rms[static_cast<size_t>(w)] >= threshold ? run + 1 : 0;
        if (run >= SUSTAINED_WINDOWS) {
            return static_cast<double>((w - SUSTAINED_WINDOWS + 1) * window) / wav.sampleRate;
        }
    }

    return std::nullopt;
}

std::vector<DetectedBeat> detectBeats(const MappedWav& wav, const BeatDetectionParams& params, const CancelFlag& canceled,
                                      const ProgressFn& progress)
{
    const std::vector<Onset> onsets = detectOnsets(wav, params, canceled, progress);
    if (canceled) {
        return {};
    }

    std::vector<DetectedBeat> beats = trackBeats(onsets, params);
    if (progress) {
        progress(1.0);
    }

    LOGI() << "beat detection: " << onsets.size() << " onsets, " << beats.size() << " beats";
    return beats;
}
}
