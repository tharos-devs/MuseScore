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
#include <cstdint>
#include <memory>
#include <vector>

#include <QObject>
#include <QString>

class QFile;

namespace mu::playback {
//! NOTE The waveform of the Audio sync window's reference audio, read from its decoded WAV file (16-bit PCM).
//! The file is memory-mapped, never read into memory as a whole: the OS pages in what's drawn. Min/max peaks
//! are computed once, in a worker thread, at several resolutions (frames per bin: 64, 256, 1024...), so any
//! zoom level draws from a handful of bins per pixel; below 64 frames per pixel the samples themselves are read.
class ReferenceAudioPeaks : public QObject
{
    Q_OBJECT

public:
    struct MinMax {
        float min = 0.f;
        float max = 0.f;
        bool valid = false;
    };

    explicit ReferenceAudioPeaks(QObject* parent = nullptr);
    ~ReferenceAudioPeaks() override;

    //! NOTE Asynchronous: isReady() once the peaks are computed (readyChanged); an empty path clears
    void load(const QString& wavPath);

    const QString& wavPath() const;
    bool isReady() const;
    bool failed() const;

    int sampleRate() const;
    int channels() const;
    //! NOTE Whether the file really has several channels: a mono source is decoded with identical channels
    bool isStereo() const;
    int64_t frames() const;
    double durationSecs() const;

    //! NOTE Min/max of a channel (-1: all channels) over [frameFrom; frameTo), samples in [-1; 1]
    MinMax range(int channel, int64_t frameFrom, int64_t frameTo) const;
    //! NOTE A single sample, for the highest zoom levels
    float sample(int channel, int64_t frame) const;

signals:
    void readyChanged();

private:
    struct Data;
    struct Job;

    static std::shared_ptr<Data> open(const QString& wavPath);
    static void computePeaks(Data& data, const std::atomic<bool>& canceled);

    QString m_wavPath;
    std::shared_ptr<Data> m_data; // set once ready
    std::shared_ptr<Job> m_job;
    bool m_failed = false;
};
}
