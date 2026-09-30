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
#include <condition_variable>
#include <deque>
#include <mutex>
#include <thread>

#include <QObject>
#include <QPointer>
#include <QElapsedTimer>
#include <QTimer>
#include <QUrl>
#include <QVideoFrame>
#include <qqmlintegration.h>

#include "modularity/ioc.h"
#include "async/asyncable.h"
#include "media/ivideodecoderfactory.h"
#include "media/ivideoencoderresolver.h"
#include "audio/main/iaudioconfiguration.h"

#include <QVideoSink>

namespace mu::playback {
//! NOTE Frame-accurate video picture: decodes the video with FFmpeg on a worker thread and pushes into
//! a VideoOutput's sink exactly the frame shown at the requested position (the last frame whose
//! timestamp is <= it). Unlike a free-running media player, what's shown only depends on the position,
//! so the same position always shows the same frame.
//! Uses the FFmpeg libraries bundled with Qt Multimedia (so no user setup is needed), falling back to
//! the FFmpeg configured for video export. `available` is false if none could be loaded.
class VideoFramePlayer : public QObject, public muse::async::Asyncable
{
    Q_OBJECT

    Q_PROPERTY(QUrl source READ source WRITE setSource NOTIFY sourceChanged FINAL)
    Q_PROPERTY(QVideoSink * videoSink READ videoSink WRITE setVideoSink NOTIFY videoSinkChanged FINAL)
    //! NOTE The position to show, in the video's timeline. While `playing`, it's the score position as
    //! reported by the audio engine: between two reports (and to smooth their delivery jitter), the
    //! picture follows a local clock locked onto them, so it moves on every screen refresh
    Q_PROPERTY(double positionMs READ positionMs WRITE setPositionMs NOTIFY positionMsChanged FINAL)
    Q_PROPERTY(bool playing READ playing WRITE setPlaying NOTIFY playingChanged FINAL)

    //! NOTE Start of the playback loop in the video's timeline, < 0 if none: its first frames are decoded
    //! in advance by a second decoder, so that the picture doesn't freeze while seeking back there on
    //! each loop wrap
    Q_PROPERTY(double loopStartMs READ loopStartMs WRITE setLoopStartMs NOTIFY loopStartMsChanged FINAL)

    Q_PROPERTY(bool available READ available NOTIFY availableChanged FINAL)
    Q_PROPERTY(bool loaded READ loaded NOTIFY streamInfoChanged FINAL)
    Q_PROPERTY(double durationMs READ durationMs NOTIFY streamInfoChanged FINAL)
    Q_PROPERTY(double frameRate READ frameRate NOTIFY streamInfoChanged FINAL)
    Q_PROPERTY(int frameWidth READ frameWidth NOTIFY streamInfoChanged FINAL)
    Q_PROPERTY(int frameHeight READ frameHeight NOTIFY streamInfoChanged FINAL)
    Q_PROPERTY(QString codecName READ codecName NOTIFY streamInfoChanged FINAL)
    Q_PROPERTY(qint64 bitRate READ bitRate NOTIFY streamInfoChanged FINAL)

    //! NOTE The timestamp of the frame currently shown, -1 if none
    Q_PROPERTY(double shownFramePtsMs READ shownFramePtsMs NOTIFY shownFrameChanged FINAL)

    QML_ELEMENT

    muse::GlobalInject<muse::media::IVideoDecoderFactory> decoderFactory;
    muse::GlobalInject<muse::media::IVideoEncoderResolver> videoEncoderResolver;
    muse::GlobalInject<muse::audio::IAudioConfiguration> audioConfiguration;

public:
    explicit VideoFramePlayer(QObject* parent = nullptr);
    ~VideoFramePlayer() override;

    QUrl source() const;
    void setSource(const QUrl& source);

    QVideoSink* videoSink() const;
    void setVideoSink(QVideoSink* sink);

    double positionMs() const;
    void setPositionMs(double positionMs);

    bool playing() const;
    void setPlaying(bool playing);

    double loopStartMs() const;
    void setLoopStartMs(double loopStartMs);

    bool available() const;
    bool loaded() const;
    double durationMs() const;
    double frameRate() const;
    int frameWidth() const;
    int frameHeight() const;
    QString codecName() const;
    qint64 bitRate() const;
    double shownFramePtsMs() const;

    //! NOTE Stopped: shows this video position instead of `positionMs`, until `positionMs` becomes
    //! different from expectedPositionMs (the score position the corresponding seek will lead to) or
    //! playback starts. For video positions the score can't reach (before its start, after its end).
    Q_INVOKABLE void previewVideoPosition(double videoPositionMs, double expectedPositionMs);

signals:
    void sourceChanged();
    void videoSinkChanged();
    void positionMsChanged();
    void playingChanged();
    void loopStartMsChanged();
    void availableChanged();
    void streamInfoChanged();
    void shownFrameChanged();

private:
    void ensureWorker();
    void stopWorker();
    void workerLoop();

    muse::io::paths_t ffmpegLibsDirs() const;

    void onStreamInfo(const muse::media::VideoStreamInfo& info, quint64 generation);

    void setTargetSecs(double secs);
    double clockMs() const; // the local clock, while playing
    void onClockTick();
    void updatePictureLead();
    void applyStoppedTarget();
    void onFrame(const QVideoFrame& frame, double ptsSecs, quint64 generation);

    QUrl m_source;
    QPointer<QVideoSink> m_sink;
    double m_positionMs = 0.0;

    bool m_playing = false;
    QElapsedTimer m_clockTimer;
    QTimer* m_clockTick = nullptr;
    double m_anchorMs = 0.0; // local clock = m_anchorMs + time elapsed since m_anchorTimeMs
    double m_anchorTimeMs = 0.0;
    double m_pictureLeadMs = 0.0;

    double m_previewPositionMs = -1.0;
    double m_previewExpectedPositionMs = -1.0;

    bool m_available = false;
    bool m_availabilityChecked = false;
    muse::media::VideoStreamInfo m_info;
    bool m_loaded = false;
    double m_shownFramePtsMs = -1.0;

    // Worker thread
    std::thread m_worker;
    std::mutex m_mutex;
    std::condition_variable m_wake;
    bool m_quit = false;
    muse::io::path_t m_requestedPath;
    bool m_openRequested = false;
    std::atomic<quint64> m_generation { 0 };
    std::atomic<double> m_targetSecs { 0.0 };
    bool m_targetChanged = false;
    std::atomic<double> m_loopStartSecs { -1.0 };
    double m_loopStartMs = -1.0;

    // Owned by the worker thread once started
    muse::media::IVideoDecoderPtr m_decoder;
    muse::media::IVideoDecoderPtr m_loopDecoder; // prepared at the loop start, swapped with m_decoder on a loop wrap
};
}
