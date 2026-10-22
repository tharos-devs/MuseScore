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
#include <numeric>

#include "fft.h"
#include "mappedwav.h"

#include "log.h"

namespace mu::playback::audiosync {
namespace {
constexpr int FFT_SIZE = 2048;
constexpr double HOP_SECS = 0.01;
constexpr double MIN_ONSET_INTERVAL_SECS = 0.05;
constexpr int PEAK_HALF_WIDTH = 3;       // local maximum over ±3 hops
constexpr int THRESHOLD_HALF_WIDTH = 8;  // adaptive threshold over ±80 ms
constexpr double BEAT_ONSET_TOLERANCE_SECS = 0.04;

constexpr double LOCAL_MEAN_SECS = 0.5;  // what an attack stands out from, for the beat tracking
constexpr double PREFERRED_BPM = 120.0;
//! NOTE How far from the preferred tempo the tempo can be (a log-normal weight's deviation, in octaves): unknown, and
//! known roughly
constexpr double PREFERRED_BPM_OCTAVES = 1.0;
constexpr double APPROX_BPM_OCTAVES = 0.3;
//! NOTE An approximate tempo can be this much off
constexpr double APPROX_BPM_RATIO = 1.5;
//! NOTE How much the beats keep to the tempo, steady and by default: low enough for the tempo to drift, as in
//! orchestral music
constexpr double STEADY_TIGHTNESS = 120.0;
constexpr double TIGHTNESS = 30.0;

//! NOTE The envelope's attacks only: what stands out from its surroundings, normalized
std::vector<double> beatSalience(const OnsetEnvelope& envelope)
{
    const std::vector<double>& values = envelope.values;
    const int64_t n = static_cast<int64_t>(values.size());
    const int64_t halfWidth = std::max<int64_t>(1, std::llround(LOCAL_MEAN_SECS / envelope.hopSecs));

    // Running sum for the local means
    std::vector<double> sums(static_cast<size_t>(n) + 1, 0.0);
    for (int64_t i = 0; i < n; ++i) {
        sums[i + 1] = sums[i] + values[i];
    }

    std::vector<double> result(static_cast<size_t>(n), 0.0);
    double squares = 0.0;
    for (int64_t i = 0; i < n; ++i) {
        const int64_t from = std::max<int64_t>(0, i - halfWidth);
        const int64_t to = std::min(n, i + halfWidth + 1);
        const double localMean = (sums[to] - sums[from]) / static_cast<double>(to - from);
        result[i] = std::max(0.0, values[i] - localMean);
        squares += result[i] * result[i];
    }

    const double deviation = n > 0 ? std::sqrt(squares / static_cast<double>(n)) : 0.0;
    if (deviation > 0.0) {
        for (double& value : result) {
            value /= deviation;
        }
    }
    return result;
}

//! NOTE The tempi estimateTempo() looks within
std::pair<double, double> tempoRange(double approxBpm)
{
    if (approxBpm > 0.0) {
        const double minBpm = approxBpm / APPROX_BPM_RATIO;
        const double maxBpm = approxBpm * APPROX_BPM_RATIO;
        return { minBpm, maxBpm };
    }
    return { 50.0, 200.0 };
}
}

OnsetEnvelope onsetEnvelope(const MappedWav& wav, const BeatDetectionParams& params, const CancelFlag& canceled,
                            const ProgressFn& progress)
{
    OnsetEnvelope envelope;
    envelope.hopSecs = HOP_SECS;

    const int64_t hop = std::max<int64_t>(1, std::llround(wav.sampleRate * HOP_SECS));
    const int64_t frameFrom = std::clamp<int64_t>(std::llround(params.fromSecs * wav.sampleRate), 0, wav.frames);
    const int64_t frameTo = params.toSecs < 0 ? wav.frames
                            : std::clamp<int64_t>(std::llround(params.toSecs * wav.sampleRate), frameFrom, wav.frames);
    envelope.fromSecs = static_cast<double>(frameFrom) / wav.sampleRate;
    if (frameTo - frameFrom < FFT_SIZE) {
        return envelope;
    }

    std::vector<float> window(FFT_SIZE);
    for (int i = 0; i < FFT_SIZE; ++i) {
        window[i] = 0.5f - 0.5f * std::cos(2.f * static_cast<float>(M_PI) * i / (FFT_SIZE - 1));
    }

    // Spectral flux: the sum of the increases of the log-compressed magnitudes, frame after frame (each frame
    // centered on its time)
    const int64_t hops = (frameTo - frameFrom) / hop;
    envelope.values.assign(static_cast<size_t>(hops), 0.0);
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
        envelope.values[static_cast<size_t>(h)] = sum;
    }

    return envelope;
}

std::vector<Onset> detectOnsets(const OnsetEnvelope& envelope, const BeatDetectionParams& params)
{
    std::vector<Onset> onsets;
    if (envelope.values.empty()) {
        return onsets;
    }

    // Normalized, then peaks above an adaptive threshold (local mean + a margin set by the sensitivity)
    std::vector<double> flux = envelope.values;
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

        const double secs = envelope.fromSecs + static_cast<double>(i) * envelope.hopSecs;
        if (secs - lastOnsetSecs < MIN_ONSET_INTERVAL_SECS) {
            continue;
        }

        onsets.push_back({ secs, flux[i] - localMean });
        lastOnsetSecs = secs;
    }

    return onsets;
}

double estimateTempo(const OnsetEnvelope& envelope, const BeatDetectionParams& params)
{
    const std::vector<double> salience = beatSalience(envelope);
    const int64_t n = static_cast<int64_t>(salience.size());
    const auto [minBpm, maxBpm] = tempoRange(params.approxBpm);
    const double preferredBpm = params.approxBpm > 0.0 ? params.approxBpm : PREFERRED_BPM;
    const double preferredOctaves = params.approxBpm > 0.0 ? APPROX_BPM_OCTAVES : PREFERRED_BPM_OCTAVES;
    const int64_t minLag = std::max<int64_t>(2, std::llround(60.0 / maxBpm / envelope.hopSecs));
    const int64_t maxLag = std::llround(60.0 / minBpm / envelope.hopSecs);
    if (n <= 2 * maxLag + 1) {
        return 0.0;
    }

    // Autocorrelation, up to twice the longest beat (a beat's double counts in its favor)
    std::vector<double> autocorrelation(static_cast<size_t>(2 * maxLag + 2), 0.0);
    for (int64_t lag = 1; lag <= 2 * maxLag + 1; ++lag) {
        double sum = 0.0;
        for (int64_t i = lag; i < n; ++i) {
            sum += salience[i] * salience[i - lag];
        }
        autocorrelation[lag] = sum / static_cast<double>(n - lag);
    }

    // The best peak, with its half and double: a beat's metrical level is supported by the others
    double best = 0.0;
    int64_t bestLag = 0;
    for (int64_t lag = minLag; lag <= maxLag; ++lag) {
        if (autocorrelation[lag] < autocorrelation[lag - 1] || autocorrelation[lag] < autocorrelation[lag + 1]) {
            continue;
        }

        const double bpm = 60.0 / (static_cast<double>(lag) * envelope.hopSecs);
        const double octaves = std::log2(bpm / preferredBpm) / preferredOctaves;
        const double weight = std::exp(-0.5 * octaves * octaves);
        const double score = (autocorrelation[lag] + 0.5 * autocorrelation[2 * lag] + 0.5 * autocorrelation[lag / 2]) * weight;
        if (score > best) {
            best = score;
            bestLag = lag;
        }
    }

    return bestLag > 0 ? 60.0 / (static_cast<double>(bestLag) * envelope.hopSecs) : 0.0;
}

double flexibilityCost(double flexibility, double steadyCost, double middleCost)
{
    return steadyCost * std::pow(middleCost / steadyCost, 2.0 * std::clamp(flexibility, 0.0, 1.0));
}

std::vector<DetectedBeat> trackBeats(const OnsetEnvelope& envelope, const std::vector<Onset>& onsets, double bpm, double flexibility)
{
    std::vector<DetectedBeat> beats;

    const std::vector<double> salience = beatSalience(envelope);
    const int64_t n = static_cast<int64_t>(salience.size());
    const int64_t period = bpm > 0.0 ? std::llround(60.0 / bpm / envelope.hopSecs) : 0;
    if (period < 2 || n < 2 * period) {
        return beats;
    }

    // Each hop's best score as a beat: its attack plus the best previous beat's, minus how far their interval is
    // from the tempo's (in octaves, squared)
    const double tightness = flexibilityCost(flexibility, STEADY_TIGHTNESS, TIGHTNESS);
    std::vector<double> penalties(static_cast<size_t>(2 * period + 1), 0.0);
    for (int64_t interval = period / 2; interval <= 2 * period; ++interval) {
        const double ratio = std::log(static_cast<double>(interval) / static_cast<double>(period));
        penalties[interval] = tightness * ratio * ratio;
    }

    std::vector<double> scores(static_cast<size_t>(n), 0.0);
    std::vector<int64_t> previous(static_cast<size_t>(n), -1);
    for (int64_t t = 0; t < n; ++t) {
        double best = 0.0;
        int64_t bestPrevious = -1;
        for (int64_t interval = period / 2; interval <= 2 * period && interval <= t; ++interval) {
            const double score = scores[t - interval] - penalties[interval];
            if (score > best) {
                best = score;
                bestPrevious = t - interval;
            }
        }
        scores[t] = salience[t] + best;
        previous[t] = bestPrevious;
    }

    // Back from the best last beat
    int64_t t = n - 1;
    for (int64_t i = std::max<int64_t>(0, n - period); i < n; ++i) {
        if (scores[i] > scores[t]) {
            t = i;
        }
    }

    std::vector<int64_t> hops;
    for (; t >= 0; t = previous[t]) {
        hops.push_back(t);
    }
    std::reverse(hops.begin(), hops.end());

    // Confidence: the attack (if any) the beat falls on
    double maxSalience = 0.0;
    for (const Onset& onset : onsets) {
        maxSalience = std::max(maxSalience, onset.salience);
    }
    double maxStrength = 0.0;
    for (int64_t hop : hops) {
        maxStrength = std::max(maxStrength, salience[hop]);
    }

    size_t onsetIdx = 0;
    for (int64_t hop : hops) {
        const double secs = envelope.fromSecs + static_cast<double>(hop) * envelope.hopSecs;
        while (onsetIdx + 1 < onsets.size() && onsets[onsetIdx + 1].secs <= secs) {
            ++onsetIdx;
        }

        double onsetSalience = 0.0;
        for (size_t i = onsetIdx; i < std::min(onsetIdx + 2, onsets.size()); ++i) {
            if (std::abs(onsets[i].secs - secs) <= BEAT_ONSET_TOLERANCE_SECS) {
                onsetSalience = std::max(onsetSalience, onsets[i].salience);
            }
        }

        DetectedBeat beat;
        beat.secs = secs;
        beat.confidence = onsetSalience > 0.0
                          ? static_cast<float>(std::clamp(0.5 + 0.5 * onsetSalience / std::max(1.0, maxSalience * 0.5), 0.5, 1.0))
                          : 0.3f;
        beat.strength = maxStrength > 0.0 ? static_cast<float>(salience[hop] / maxStrength) : 0.f;
        beats.push_back(beat);
    }

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
    const OnsetEnvelope envelope = onsetEnvelope(wav, params, canceled, progress);
    if (canceled) {
        return {};
    }

    const std::vector<Onset> onsets = detectOnsets(envelope, params);
    const double bpm = estimateTempo(envelope, params);
    std::vector<DetectedBeat> beats = trackBeats(envelope, onsets, bpm, params.flexibility);
    if (progress) {
        progress(1.0);
    }

    LOGI() << "beat detection: " << onsets.size() << " onsets, tempo " << bpm << " BPM, " << beats.size() << " beats";
    return beats;
}
}
