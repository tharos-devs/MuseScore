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

#include "videoframeplayer.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstring>

#include <QVideoFrameFormat>
#include <QVideoSink>

#include "log.h"

using namespace mu::playback;
using namespace muse;
using namespace muse::media;

namespace {
//! NOTE Frames decoded in advance of the shown one, so playback never waits for the decoder
constexpr size_t READ_AHEAD_FRAMES = 8;

//! NOTE Frames kept behind the shown one, so that a target moving back a little is served from memory
//! instead of seeking (a seek restarts decoding from the previous keyframe: tens of ms)
constexpr size_t KEPT_BEHIND_FRAMES = 4;

//! NOTE Moving forward by less than this decodes the frames in between instead of seeking, which
//! is faster (a seek restarts from the previous keyframe, possibly seconds before)
constexpr double MAX_DECODE_FORWARD_SECS = 2.0;

constexpr double PTS_EPSILON_SECS = 0.001;

//! NOTE How often the picture's target is updated while playing (faster than any screen refresh)
constexpr int CLOCK_TICK_MS = 4;

//! NOTE Locking the local clock onto the engine's reports: a small error is corrected gradually (which
//! smooths out their irregular delivery), a large one (seek, loop wrap) immediately
constexpr double CLOCK_SNAP_THRESHOLD_MS = 60.0;
constexpr double CLOCK_CORRECTION_RATIO = 0.1;

//! NOTE The local clock never runs further than this ahead of the last report, so that the picture
//! doesn't run away while the score's position is held still (count-in, end of the score...)
constexpr double CLOCK_MAX_EXTRAPOLATION_MS = 25.0;

//! NOTE Picture timing compensation: reports arrive late (half an engine cycle on average), and a frame
//! pushed now is on screen about one refresh later, while the sound is heard after the output's latency (as
//! measured by the driver: its buffering and the device's own latency; else, one output buffer)
constexpr double REPORT_DELAY_MS = 8.0;
constexpr double DISPLAY_LATENCY_MS = 16.7;

QVideoFrame toQVideoFrame(const VideoFrame& frame)
{
    QVideoFrameFormat format(QSize(frame.width, frame.height), QVideoFrameFormat::Format_YUV420P);

    switch (frame.colorSpace) {
    case VideoFrame::ColorSpace::BT601: format.setColorSpace(QVideoFrameFormat::ColorSpace_BT601);
        break;
    case VideoFrame::ColorSpace::BT709: format.setColorSpace(QVideoFrameFormat::ColorSpace_BT709);
        break;
    case VideoFrame::ColorSpace::BT2020: format.setColorSpace(QVideoFrameFormat::ColorSpace_BT2020);
        break;
    case VideoFrame::ColorSpace::Unknown:
        //! NOTE Same heuristic as most players when the stream doesn't say: HD and above is BT.709
        format.setColorSpace(frame.height >= 720 ? QVideoFrameFormat::ColorSpace_BT709 : QVideoFrameFormat::ColorSpace_BT601);
        break;
    }

    format.setColorRange(frame.fullRange ? QVideoFrameFormat::ColorRange_Full : QVideoFrameFormat::ColorRange_Video);

    QVideoFrame result(format);
    if (!result.map(QVideoFrame::WriteOnly)) {
        return QVideoFrame();
    }

    const int planeWidths[3] = { frame.width, (frame.width + 1) / 2, (frame.width + 1) / 2 };
    const int planeHeights[3] = { frame.height, (frame.height + 1) / 2, (frame.height + 1) / 2 };

    for (int p = 0; p < 3 && p < result.planeCount(); ++p) {
        uchar* dst = result.bits(p);
        const int dstStride = result.bytesPerLine(p);
        const uint8_t* src = frame.data.data() + frame.offsets[p];
        const int srcStride = frame.strides[p];

        for (int y = 0; y < planeHeights[p]; ++y) {
            std::memcpy(dst + static_cast<size_t>(y) * dstStride, src + static_cast<size_t>(y) * srcStride,
                        static_cast<size_t>(planeWidths[p]));
        }
    }

    result.unmap();
    return result;
}
}

VideoFramePlayer::VideoFramePlayer(QObject* parent)
    : QObject(parent)
{
    m_clockTimer.start();

    m_clockTick = new QTimer(this);
    m_clockTick->setTimerType(Qt::PreciseTimer);
    m_clockTick->setInterval(CLOCK_TICK_MS);
    connect(m_clockTick, &QTimer::timeout, this, &VideoFramePlayer::onClockTick);

    updatePictureLead();
    if (audioConfiguration()) {
        audioConfiguration()->driverBufferSizeChanged().onNotify(this, [this]() {
            updatePictureLead();
        });
        audioConfiguration()->sampleRateChanged().onNotify(this, [this]() {
            updatePictureLead();
        });
    }
}

VideoFramePlayer::~VideoFramePlayer()
{
    stopWorker();
}

void VideoFramePlayer::ensureWorker()
{
    if (m_availabilityChecked) {
        return;
    }

    m_availabilityChecked = true;

    if (decoderFactory()) {
        const io::paths_t dirs = decoderFactory()->defaultFFmpegLibsDirs();
        m_decoder = decoderFactory()->createDecoder(dirs);
        if (m_decoder) {
            m_loopDecoder = decoderFactory()->createDecoder(dirs);
        }
    }

    m_available = m_decoder != nullptr;
    if (!m_available) {
        LOGW() << "no usable FFmpeg libraries found, the video picture falls back to Qt Multimedia's player";
        emit availableChanged();
        return;
    }

    m_worker = std::thread([this]() {
        workerLoop();
    });

    emit availableChanged();
}

void VideoFramePlayer::stopWorker()
{
    {
        std::lock_guard lock(m_mutex);
        m_quit = true;
    }
    m_wake.notify_all();

    if (m_worker.joinable()) {
        m_worker.join();
    }
}

void VideoFramePlayer::workerLoop()
{
    std::deque<VideoFramePtr> buffer;
    bool isOpen = false;
    double streamDurationSecs = 0.0;
    double fruitlessSeekTarget = -1.0;

    // The first frames at the loop start, from m_loopDecoder
    std::deque<VideoFramePtr> loopBuffer;
    bool isLoopDecoderOpen = false;
    double loopBufferStart = -1.0;
    bool endOfStream = false;
    double shownPts = -1.0;
    quint64 generation = 0;

    auto decodeNext = [&]() -> bool {
        VideoFramePtr frame = m_decoder->decodeNextFrame();
        if (!frame) {
            endOfStream = true;
            return false;
        }
        buffer.push_back(std::move(frame));
        return true;
    };

    while (true) {
        bool openNow = false;
        io::path_t path;

        {
            std::unique_lock lock(m_mutex);
            //! NOTE Every change notifies: nothing to do (and no wake-up) until then
            m_wake.wait(lock, [this]() {
                return m_quit || m_openRequested || m_targetChanged;
            });

            if (m_quit) {
                break;
            }

            if (m_openRequested) {
                m_openRequested = false;
                openNow = true;
                path = m_requestedPath;
                generation = m_generation.load();
            }

            m_targetChanged = false;
        }

        if (openNow) {
            buffer.clear();
            endOfStream = false;
            shownPts = -1.0;

            m_decoder->close();
            isOpen = !path.empty() && m_decoder->open(path);

            loopBuffer.clear();
            loopBufferStart = -1.0;
            if (m_loopDecoder) {
                m_loopDecoder->close();
                isLoopDecoderOpen = isOpen && m_loopDecoder->open(path);
            }

            const VideoStreamInfo info = isOpen ? m_decoder->streamInfo() : VideoStreamInfo();
            streamDurationSecs = info.durationSecs;
            fruitlessSeekTarget = -1.0;
            QMetaObject::invokeMethod(this, [this, info, generation]() {
                onStreamInfo(info, generation);
            }, Qt::QueuedConnection);
        }

        if (!isOpen) {
            continue;
        }

        //! NOTE Past the end, the last frame is shown (the score can be longer than the video)
        double target = m_targetSecs.load();
        if (streamDurationSecs > 0.0) {
            target = std::min(target, streamDurationSecs);
        }

        // Does the buffer already contain, or lead soon to, the frame shown at the target?
        const bool behindBuffer = buffer.empty() || target + PTS_EPSILON_SECS < buffer.front()->ptsSecs;
        const bool farAhead = !buffer.empty() && target > buffer.back()->ptsSecs + MAX_DECODE_FORWARD_SECS;

        //! NOTE A seek that gave no frame at all isn't retried for the same target (it would just fail again)
        const bool sameFruitlessSeek = buffer.empty() && target == fruitlessSeekTarget;

        if ((behindBuffer || farAhead) && !sameFruitlessSeek) {
            const bool inLoopBuffer = !loopBuffer.empty() && target + PTS_EPSILON_SECS >= loopBuffer.front()->ptsSecs
                                      && target <= loopBuffer.back()->ptsSecs + PTS_EPSILON_SECS;

            if (inLoopBuffer) {
                //! NOTE Loop wrap: continue from the decoder prepared there, and prepare the other one next
                std::swap(m_decoder, m_loopDecoder);
                buffer.swap(loopBuffer);
                loopBuffer.clear();
                loopBufferStart = -1.0;
            } else {
                buffer.clear();
                m_decoder->seek(target);
            }

            endOfStream = false;
            fruitlessSeekTarget = -1.0;
        }

        // Decode up to the first frame after the target
        while (!endOfStream && (buffer.empty() || buffer.back()->ptsSecs <= target + PTS_EPSILON_SECS)) {
            decodeNext();
        }

        if (buffer.empty()) {
            fruitlessSeekTarget = target;
            continue;
        }

        // The frame shown at the target: the last one whose pts <= target
        size_t shownIndex = 0;
        while (shownIndex + 1 < buffer.size() && buffer[shownIndex + 1]->ptsSecs <= target + PTS_EPSILON_SECS) {
            ++shownIndex;
        }

        // Drop the older frames, except a few behind the shown one
        while (shownIndex > KEPT_BEHIND_FRAMES) {
            buffer.pop_front();
            --shownIndex;
        }

        if (!buffer.empty() && buffer[shownIndex]->ptsSecs != shownPts) {
            shownPts = buffer[shownIndex]->ptsSecs;
            QVideoFrame frame = toQVideoFrame(*buffer[shownIndex]);
            QMetaObject::invokeMethod(this, [this, frame, pts = shownPts, generation]() {
                onFrame(frame, pts, generation);
            }, Qt::QueuedConnection);
        }

        auto isInterrupted = [this]() {
            std::lock_guard lock(m_mutex);
            return m_targetChanged || m_openRequested || m_quit;
        };

        // Read ahead, unless a new target is already waiting
        while (!endOfStream && buffer.size() < shownIndex + 1 + READ_AHEAD_FRAMES) {
            if (isInterrupted()) {
                break;
            }
            decodeNext();
        }

        //! NOTE Prepare the loop start only once the read-ahead is complete: that margin (several frames)
        //! covers the time this takes, so the frames being shown are never late because of it
        const double loopStart = m_loopStartSecs.load();
        const bool readAheadComplete = endOfStream || buffer.size() >= shownIndex + 1 + READ_AHEAD_FRAMES;
        if (isLoopDecoderOpen && loopStart >= 0.0 && loopStart != loopBufferStart && readAheadComplete && !isInterrupted()) {
            loopBuffer.clear();
            loopBufferStart = loopStart;

            if (m_loopDecoder->seek(loopStart)) {
                while (loopBuffer.size() < READ_AHEAD_FRAMES) {
                    VideoFramePtr frame = m_loopDecoder->decodeNextFrame();
                    if (!frame) {
                        break;
                    }
                    loopBuffer.push_back(std::move(frame));
                }
            }
        }
    }

    m_decoder->close();
    if (m_loopDecoder) {
        m_loopDecoder->close();
    }
}

void VideoFramePlayer::onStreamInfo(const VideoStreamInfo& info, quint64 generation)
{
    if (generation != m_generation.load()) {
        return;
    }

    m_info = info;
    m_loaded = info.isValid();
    emit streamInfoChanged();
}

void VideoFramePlayer::onFrame(const QVideoFrame& frame, double ptsSecs, quint64 generation)
{
    if (generation != m_generation.load()) {
        return;
    }

    if (m_sink) {
        m_sink->setVideoFrame(frame);
    }

    m_shownFramePtsMs = ptsSecs * 1000.0;
    emit shownFrameChanged();
}

QUrl VideoFramePlayer::source() const
{
    return m_source;
}

void VideoFramePlayer::setSource(const QUrl& source)
{
    if (m_source == source) {
        return;
    }

    m_source = source;
    emit sourceChanged();

    ensureWorker();

    m_info = VideoStreamInfo();
    m_loaded = false;
    m_shownFramePtsMs = -1.0;
    emit streamInfoChanged();
    emit shownFrameChanged();

    if (m_sink) {
        m_sink->setVideoFrame(QVideoFrame());
    }

    if (!m_available) {
        return;
    }

    {
        std::lock_guard lock(m_mutex);
        m_requestedPath = io::path_t(source.isLocalFile() ? source.toLocalFile() : QString());
        m_openRequested = true;
        ++m_generation;
    }
    m_wake.notify_all();
}

QVideoSink* VideoFramePlayer::videoSink() const
{
    return m_sink;
}

void VideoFramePlayer::setVideoSink(QVideoSink* sink)
{
    if (m_sink == sink) {
        return;
    }

    m_sink = sink;
    emit videoSinkChanged();
}

double VideoFramePlayer::positionMs() const
{
    return m_positionMs;
}

void VideoFramePlayer::setPositionMs(double positionMs)
{
    positionMs = std::max(0.0, positionMs);
    if (qFuzzyCompare(m_positionMs + 1.0, positionMs + 1.0)) {
        return;
    }

    m_positionMs = positionMs;
    emit positionMsChanged();

    if (!m_playing) {
        if (m_previewPositionMs >= 0.0 && std::abs(positionMs - m_previewExpectedPositionMs) > 1.0) {
            m_previewPositionMs = -1.0; // the score was moved elsewhere
        }
        applyStoppedTarget();
        return;
    }

    const double now = static_cast<double>(m_clockTimer.nsecsElapsed()) / 1e6;
    const double error = positionMs - clockMs();

    if (std::abs(error) > CLOCK_SNAP_THRESHOLD_MS) {
        m_anchorMs = positionMs;
    } else {
        m_anchorMs = clockMs() + error * CLOCK_CORRECTION_RATIO;
    }
    m_anchorTimeMs = now;

    onClockTick();
}

double VideoFramePlayer::loopStartMs() const
{
    return m_loopStartMs;
}

void VideoFramePlayer::setLoopStartMs(double loopStartMs)
{
    if (loopStartMs < 0.0) {
        loopStartMs = -1.0;
    }

    if (qFuzzyCompare(m_loopStartMs + 2.0, loopStartMs + 2.0)) {
        return;
    }

    m_loopStartMs = loopStartMs;
    m_loopStartSecs.store(loopStartMs < 0.0 ? -1.0 : loopStartMs / 1000.0);
    emit loopStartMsChanged();

    {
        std::lock_guard lock(m_mutex);
        m_targetChanged = true; // wake the worker
    }
    m_wake.notify_all();
}

bool VideoFramePlayer::playing() const
{
    return m_playing;
}

void VideoFramePlayer::setPlaying(bool playing)
{
    if (m_playing == playing) {
        return;
    }

    m_playing = playing;
    emit playingChanged();

    if (playing) {
        m_previewPositionMs = -1.0;
        m_anchorMs = m_positionMs;
        m_anchorTimeMs = static_cast<double>(m_clockTimer.nsecsElapsed()) / 1e6;
        m_clockTick->start();
        onClockTick();
    } else {
        m_clockTick->stop();
        applyStoppedTarget();
    }
}

double VideoFramePlayer::clockMs() const
{
    const double now = static_cast<double>(m_clockTimer.nsecsElapsed()) / 1e6;
    const double clock = m_anchorMs + (now - m_anchorTimeMs);
    return std::min(clock, m_positionMs + CLOCK_MAX_EXTRAPOLATION_MS);
}

void VideoFramePlayer::updatePictureLead()
{
    double audioLatencyMs = 0.0;
    if (audioDriverController() && audioDriverController()->outputLatencySecs() > 0.0) {
        audioLatencyMs = 1000.0 * audioDriverController()->outputLatencySecs();
    } else if (audioConfiguration() && audioConfiguration()->sampleRate() > 0) {
        audioLatencyMs = 1000.0 * audioConfiguration()->driverBufferSize() / audioConfiguration()->sampleRate();
    }

    m_pictureLeadMs = REPORT_DELAY_MS + DISPLAY_LATENCY_MS - audioLatencyMs;
}

void VideoFramePlayer::applyStoppedTarget()
{
    //! NOTE Stopped: exactly the frame at the position, no timing compensation
    setTargetSecs((m_previewPositionMs >= 0.0 ? m_previewPositionMs : m_positionMs) / 1000.0);
}

void VideoFramePlayer::previewVideoPosition(double videoPositionMs, double expectedPositionMs)
{
    if (m_playing) {
        return;
    }

    m_previewPositionMs = std::max(0.0, videoPositionMs);
    m_previewExpectedPositionMs = std::max(0.0, expectedPositionMs);
    applyStoppedTarget();
}

void VideoFramePlayer::onClockTick()
{
    if (!m_playing) {
        return;
    }

    //! NOTE The measured latency is only known, and can only change, while the audio runs
    updatePictureLead();

    double targetMs = std::max(0.0, clockMs() + m_pictureLeadMs);

    //! NOTE Playing: the picture never goes back, except for a real jump (seek, loop wrap) - the clock's
    //! small corrections are absorbed by holding the current frame a little longer instead
    const double lastTargetMs = m_targetSecs.load() * 1000.0;
    if (targetMs < lastTargetMs && lastTargetMs - targetMs < CLOCK_SNAP_THRESHOLD_MS) {
        targetMs = lastTargetMs;
    }

    setTargetSecs(targetMs / 1000.0);
}

void VideoFramePlayer::setTargetSecs(double secs)
{
    m_targetSecs.store(secs);
    {
        std::lock_guard lock(m_mutex);
        m_targetChanged = true;
    }
    m_wake.notify_all();
}

bool VideoFramePlayer::available() const
{
    return m_available;
}

bool VideoFramePlayer::loaded() const
{
    return m_loaded;
}

double VideoFramePlayer::durationMs() const
{
    return m_info.durationSecs * 1000.0;
}

double VideoFramePlayer::frameRate() const
{
    return m_info.frameRate;
}

int VideoFramePlayer::frameWidth() const
{
    return m_info.width;
}

int VideoFramePlayer::frameHeight() const
{
    return m_info.height;
}

QString VideoFramePlayer::codecName() const
{
    return QString::fromStdString(m_info.codecName);
}

qint64 VideoFramePlayer::bitRate() const
{
    return m_info.bitRate;
}

double VideoFramePlayer::shownFramePtsMs() const
{
    return m_shownFramePtsMs;
}
