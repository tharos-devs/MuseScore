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

#include <functional>
#include <memory>

#include <QFile>
#include <QObject>

#include "global/io/path.h"

class QAudioDecoder;
class QAudioBuffer;

namespace mu::playback {
//! NOTE Decodes the audio track of a video file into a 16-bit stereo PCM WAV file in the app's cache
//! directory, to be played by the audio engine as a sound track (see IPlayback::addSoundTrack()).
//! Uses Qt Multimedia's QAudioDecoder (backed by the FFmpeg bundled with Qt), so it doesn't depend
//! on the user-configured FFmpeg used by video export. Results are cached by file path, size,
//! modification time and sample rate, so reopening a project doesn't decode again.
class VideoAudioDecoder : public QObject
{
    Q_OBJECT

public:
    //! NOTE Empty path: the video has no audio track (or it couldn't be decoded)
    using Callback = std::function<void (const muse::io::path_t& wavPath)>;

    explicit VideoAudioDecoder(QObject* parent = nullptr);
    ~VideoAudioDecoder() override;

    //! NOTE Cancels any decoding in progress; the callback is always called asynchronously
    void decode(const muse::io::path_t& videoPath, unsigned int sampleRate, const Callback& onFinished);
    void cancel();

    bool isDecoding() const;

private:
    void onBufferReady();
    void onFinished();
    void onError();

    bool writeBuffer(const QAudioBuffer& buffer);
    void finish(bool ok);
    void pruneCache(const QString& keepPath) const;

    QString cacheDir() const;

    QAudioDecoder* m_decoder = nullptr;
    QFile m_file;
    QString m_targetPath;
    Callback m_callback;
    quint32 m_fileSampleRate = 0;
    quint64 m_dataBytes = 0;
    quint64 m_generation = 0;
};
}
