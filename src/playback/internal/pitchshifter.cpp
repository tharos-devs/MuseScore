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

#include "pitchshifter.h"

#include <algorithm>
#include <cmath>
#include <complex>
#include <cstring>
#include <vector>

#include <QFile>

#include "fft.h"
#include "mappedwav.h"

#include "log.h"

namespace mu::playback::audiosync {
namespace {
constexpr int FRAME_SIZE = 4096;
constexpr int HOP = FRAME_SIZE / 4;
constexpr int LOW_PASS_TAPS = 64;
constexpr int LOW_PASS_PHASES = 256;
//! NOTE About a Blackman-windowed sinc's transition band at LOW_PASS_TAPS taps, in cycles per sample
constexpr double TRANSITION_WIDTH = 5.5 / LOW_PASS_TAPS;
constexpr int BINS = FRAME_SIZE / 2 + 1;
constexpr double TWO_PI = 2.0 * M_PI;

double wrapPhase(double phase)
{
    return phase - TWO_PI * std::floor((phase + M_PI) / TWO_PI);
}

void putLE16(char* p, uint16_t v)
{
    p[0] = static_cast<char>(v & 0xff);
    p[1] = static_cast<char>((v >> 8) & 0xff);
}

void putLE32(char* p, uint32_t v)
{
    for (int i = 0; i < 4; ++i) {
        p[i] = static_cast<char>((v >> (8 * i)) & 0xff);
    }
}

QByteArray wavHeader(int sampleRate, int channels, uint32_t dataBytes)
{
    QByteArray header(44, 0);
    char* p = header.data();
    std::memcpy(p, "RIFF", 4);
    putLE32(p + 4, 36 + dataBytes);
    std::memcpy(p + 8, "WAVE", 4);
    std::memcpy(p + 12, "fmt ", 4);
    putLE32(p + 16, 16);
    putLE16(p + 20, 1); // PCM
    putLE16(p + 22, static_cast<uint16_t>(channels));
    putLE32(p + 24, static_cast<uint32_t>(sampleRate));
    putLE32(p + 28, static_cast<uint32_t>(sampleRate * channels * 2));
    putLE16(p + 32, static_cast<uint16_t>(channels * 2));
    putLE16(p + 34, 16);
    std::memcpy(p + 36, "data", 4);
    putLE32(p + 40, dataBytes);
    return header;
}

//! NOTE One channel stretched by ratio (its pitch kept), into stretched (about frames * ratio samples, after
//! FRAME_SIZE samples of lead-in). The synthesis hop stays at most HOP (a Hann² overlap-add is only flat when
//! FRAME_SIZE / hop is large enough): it's HOP when stretching, and the analysis hop (synthesis hop / ratio, frames
//! read at its rounded positions) varies instead; when shortening, the analysis hop stays about HOP
bool stretchChannel(const MappedWav& wav, int channel, double ratio, std::vector<float>& stretched, const std::atomic<bool>& canceled,
                    const std::function<void(double)>& progress)
{
    const int64_t frames = wav.frames;
    const int synthesisHop = std::max(1, static_cast<int>(std::lround(HOP * std::min(1.0, ratio))));
    const double analysisHop = synthesisHop / ratio;

    // The input padded with FRAME_SIZE zeros on both sides
    const int64_t frameCount = static_cast<int64_t>(std::floor(static_cast<double>(frames + FRAME_SIZE) / analysisHop)) + 1;
    stretched.assign(static_cast<size_t>(frameCount * synthesisHop + FRAME_SIZE), 0.f);

    std::vector<float> window(FRAME_SIZE);
    double windowSquares = 0.0;
    for (int i = 0; i < FRAME_SIZE; ++i) {
        window[i] = static_cast<float>(0.5 - 0.5 * std::cos(TWO_PI * i / FRAME_SIZE));
        windowSquares += window[i] * window[i];
    }
    //! NOTE Overlap-add gain of the analysis and synthesis windows at the synthesis hop
    const float normalization = static_cast<float>(synthesisHop / windowSquares);

    std::vector<std::complex<float> > buffer(FRAME_SIZE);
    std::vector<double> magnitude(BINS), phase(BINS), previousPhase(BINS, 0.0), synthesisPhase(BINS, 0.0), advanced(BINS);
    std::vector<int> peakOf(BINS);
    int64_t previousStart = 0;

    for (int64_t f = 0; f < frameCount; ++f) {
        if ((f & 0x3f) == 0) {
            if (canceled) {
                return false;
            }
            if (progress) {
                progress(static_cast<double>(f) / static_cast<double>(frameCount));
            }
        }

        const int64_t start = std::llround(static_cast<double>(f) * analysisHop) - FRAME_SIZE;
        for (int i = 0; i < FRAME_SIZE; ++i) {
            const int64_t frame = start + i;
            const float s = frame >= 0 && frame < frames ? wav.sample(channel, frame) : 0.f;
            buffer[i] = std::complex<float>(s * window[i], 0.f);
        }

        fft(buffer);

        for (int k = 0; k < BINS; ++k) {
            magnitude[k] = std::abs(buffer[k]);
            phase[k] = std::arg(buffer[k]);
        }

        if (f == 0) {
            synthesisPhase = phase;
        } else {
            // Each bin's true frequency, from its phase advance over the actual (rounded) analysis hop, carried over
            // the synthesis hop
            const double hop = static_cast<double>(std::max<int64_t>(1, start - previousStart));
            for (int k = 0; k < BINS; ++k) {
                const double omega = TWO_PI * k / FRAME_SIZE;
                const double deviation = wrapPhase(phase[k] - previousPhase[k] - omega * hop);
                advanced[k] = synthesisPhase[k] + (omega + deviation / hop) * synthesisHop;
            }

            //! NOTE Identity phase locking: the bins around a spectral peak keep their phase relation to it (much
            //! less "phasiness" than each bin on its own)
            std::vector<int> peaks;
            for (int k = 1; k + 1 < BINS; ++k) {
                if (magnitude[k] > magnitude[k - 1] && magnitude[k] >= magnitude[k + 1]) {
                    peaks.push_back(k);
                }
            }

            if (peaks.empty()) {
                synthesisPhase = advanced;
            } else {
                size_t p = 0;
                for (int k = 0; k < BINS; ++k) {
                    while (p + 1 < peaks.size() && std::abs(peaks[p + 1] - k) < std::abs(peaks[p] - k)) {
                        ++p;
                    }
                    peakOf[k] = peaks[p];
                }
                for (int k = 0; k < BINS; ++k) {
                    const int peak = peakOf[k];
                    synthesisPhase[k] = k == peak ? advanced[k] : advanced[peak] + (phase[k] - phase[peak]);
                }
            }
        }
        previousPhase = phase;
        previousStart = start;

        for (int k = 0; k < BINS; ++k) {
            buffer[k] = std::polar(static_cast<float>(magnitude[k]), static_cast<float>(synthesisPhase[k]));
        }
        for (int k = 1; k < FRAME_SIZE / 2; ++k) {
            buffer[FRAME_SIZE - k] = std::conj(buffer[k]);
        }

        fft(buffer, true);

        const int64_t out = f * synthesisHop;
        for (int i = 0; i < FRAME_SIZE; ++i) {
            stretched[static_cast<size_t>(out + i)] += buffer[i].real() * window[i] * normalization;
        }
    }

    return true;
}

//! NOTE Band-limited interpolation for reading a signal step (> 1) times faster: a Blackman-windowed sinc low-passed
//! below the read signal's Nyquist frequency (so that it doesn't alias), tabulated at LOW_PASS_PHASES + 1 fractional
//! positions of LOW_PASS_TAPS taps each (row p, tap t: the weight of sample floor(pos) - LOW_PASS_TAPS / 2 + 1 + t
//! for pos's fraction p / LOW_PASS_PHASES)
std::vector<float> lowPassTable(double step)
{
    const double cutoff = std::max(0.05, 0.5 / step - TRANSITION_WIDTH / 2.0); // in cycles per sample
    const double halfWidth = LOW_PASS_TAPS / 2.0;

    std::vector<float> table(static_cast<size_t>((LOW_PASS_PHASES + 1) * LOW_PASS_TAPS));
    for (int p = 0; p <= LOW_PASS_PHASES; ++p) {
        const double fraction = static_cast<double>(p) / LOW_PASS_PHASES;
        float* row = table.data() + static_cast<size_t>(p * LOW_PASS_TAPS);
        double sum = 0.0;
        for (int t = 0; t < LOW_PASS_TAPS; ++t) {
            const double x = fraction + LOW_PASS_TAPS / 2 - 1 - t; // pos - that sample
            const double sinc = std::abs(x) < 1e-9 ? 2.0 * cutoff : std::sin(TWO_PI * cutoff * x) / (M_PI * x);
            const double window = std::abs(x) >= halfWidth
                                  ? 0.0 : 0.42 + 0.5 * std::cos(M_PI * x / halfWidth) + 0.08 * std::cos(TWO_PI * x / halfWidth);
            row[t] = static_cast<float>(sinc * window);
            sum += row[t];
        }
        // Unity gain at DC
        for (int t = 0; t < LOW_PASS_TAPS; ++t) {
            row[t] = static_cast<float>(row[t] / sum);
        }
    }

    return table;
}

float lowPassAt(const std::vector<float>& signal, const std::vector<float>& table, double pos)
{
    const int64_t i = static_cast<int64_t>(std::floor(pos));
    const int p = static_cast<int>(std::lround((pos - static_cast<double>(i)) * LOW_PASS_PHASES));
    const float* row = table.data() + static_cast<size_t>(p * LOW_PASS_TAPS);
    const int64_t first = i - LOW_PASS_TAPS / 2 + 1;
    const int64_t length = static_cast<int64_t>(signal.size());
    const int from = static_cast<int>(std::clamp<int64_t>(-first, 0, LOW_PASS_TAPS));
    const int to = static_cast<int>(std::clamp<int64_t>(length - first, 0, LOW_PASS_TAPS));

    float acc = 0.f;
    for (int t = from; t < to; ++t) {
        acc += row[t] * signal[static_cast<size_t>(first + t)];
    }
    return acc;
}

float cubicAt(const std::vector<float>& signal, double pos)
{
    const int64_t i = static_cast<int64_t>(std::floor(pos));
    const float t = static_cast<float>(pos - static_cast<double>(i));
    auto at = [&signal](int64_t n) {
        return n >= 0 && n < static_cast<int64_t>(signal.size()) ? signal[static_cast<size_t>(n)] : 0.f;
    };
    const float y0 = at(i - 1), y1 = at(i), y2 = at(i + 1), y3 = at(i + 2);
    const float a = -0.5f * y0 + 1.5f * y1 - 1.5f * y2 + 0.5f * y3;
    const float b = y0 - 2.5f * y1 + 2.f * y2 - 0.5f * y3;
    const float c = -0.5f * y0 + 0.5f * y2;
    return ((a * t + b) * t + c) * t + y1;
}
}

bool transformWav(const QString& inPath, const QString& outPath, int semitones, double speed, const std::atomic<bool>& canceled,
                  const std::function<void(double)>& progress)
{
    const std::shared_ptr<MappedWav> wav = MappedWav::open(inPath);
    if (!wav || wav->frames <= 0) {
        return false;
    }

    //! NOTE Stretched by pitch / speed (its pitch kept), then read pitch times faster: transposed by pitch, its length
    //! divided by speed
    speed = clampTransformSpeed(speed);
    const double pitch = std::pow(2.0, semitones / 12.0);
    const double ratio = pitch / speed;
    const int channels = wav->channels;
    const int64_t outFrames = std::max<int64_t>(1, std::llround(static_cast<double>(wav->frames) / speed));
    std::vector<int16_t> out(static_cast<size_t>(outFrames * channels));
    std::vector<float> stretched;

    //! NOTE Read faster than its rate: low-passed while resampled, or it would alias
    const bool antiAliased = pitch > 1.0;
    const std::vector<float> lowPass = antiAliased ? lowPassTable(pitch) : std::vector<float>();

    for (int ch = 0; ch < channels; ++ch) {
        const auto channelProgress = [&progress, ch, channels](double p) {
            if (progress) {
                progress((ch + p) / channels);
            }
        };
        if (!stretchChannel(*wav, ch, ratio, stretched, canceled, channelProgress)) {
            return false;
        }

        //! NOTE Input sample t is at (t + FRAME_SIZE / 2) * ratio + FRAME_SIZE / 2 in the stretched signal: the frame
        //! centered on it (input t = f * analysis hop - FRAME_SIZE / 2) is written centered at f * synthesis hop
        //! + FRAME_SIZE / 2, and synthesis hop / analysis hop = ratio (the analysis positions' rounding is under
        //! half a sample). Output sample i is input time i * speed: stretched position offset + i * pitch
        const double offset = FRAME_SIZE / 2.0 * ratio + FRAME_SIZE / 2.0;
        for (int64_t i = 0; i < outFrames; ++i) {
            if ((i & 0xffff) == 0 && canceled) {
                return false;
            }
            const double pos = offset + static_cast<double>(i) * pitch;
            const float s = antiAliased ? lowPassAt(stretched, lowPass, pos) : cubicAt(stretched, pos);
            out[static_cast<size_t>(i * channels + ch)] = static_cast<int16_t>(std::lround(std::clamp(s, -1.f, 1.f) * 32767.f));
        }
    }

    if (canceled) {
        return false;
    }

    const QString partPath = outPath + ".part";
    QFile file(partPath);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        LOGE() << "unable to create " << partPath;
        return false;
    }

    const uint32_t dataBytes = static_cast<uint32_t>(out.size() * sizeof(int16_t));
    file.write(wavHeader(wav->sampleRate, channels, dataBytes));
    const qint64 written = file.write(reinterpret_cast<const char*>(out.data()), dataBytes);
    file.close();
    if (written != static_cast<qint64>(dataBytes)) {
        QFile::remove(partPath);
        return false;
    }

    QFile::remove(outPath);
    if (!QFile::rename(partPath, outPath)) {
        QFile::remove(partPath);
        return false;
    }

    if (progress) {
        progress(1.0);
    }
    return true;
}
}
