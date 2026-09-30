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

#include <chrono>
#include <cstring>

#include <QCoreApplication>
#include <QLibraryInfo>
#include <QVideoFrameFormat>
#include <QVideoSink>

#include "log.h"

using namespace mu::playback;
using namespace muse;
using namespace muse::media;

namespace {
//! NOTE Frames decoded in advance of the shown one, so playback never waits for the decoder
constexpr size_t READ_AHEAD_FRAMES = 8;

//! NOTE Moving forward by less than this decodes the frames in between instead of seeking, which
//! is faster (a seek restarts from the previous keyframe, possibly seconds before)
constexpr double MAX_DECODE_FORWARD_SECS = 2.0;

constexpr double PTS_EPSILON_SECS = 0.001;

QVideoFrame toQVideoFrame(const VideoFrame& frame)
{
    QVideoFrameFormat format(QSize(frame.width, frame.height), QVideoFrameFormat::Format_YUV420P);

    //! NOTE Same heuristic as most players when the stream doesn't say: HD and above is BT.709
    format.setColorSpace(frame.height >= 720 ? QVideoFrameFormat::ColorSpace_BT709 : QVideoFrameFormat::ColorSpace_BT601);

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
}

VideoFramePlayer::~VideoFramePlayer()
{
    stopWorker();
}

io::paths_t VideoFramePlayer::ffmpegLibsDirs() const
{
    io::paths_t dirs;

    const QString appDir = QCoreApplication::applicationDirPath();

    //! NOTE Where the deployment tools put the FFmpeg libraries of Qt Multimedia's FFmpeg backend:
    //! next to the executable on Windows (windeployqt), in the bundle's Frameworks on macOS (macdeployqt)
#if defined(Q_OS_MAC)
    dirs.push_back(io::path_t(appDir + "/../Frameworks"));
#endif
    dirs.push_back(io::path_t(appDir));

    //! NOTE Development builds use Qt's own installation directly
    dirs.push_back(io::path_t(QLibraryInfo::path(QLibraryInfo::LibrariesPath)));
    dirs.push_back(io::path_t(QLibraryInfo::path(QLibraryInfo::BinariesPath)));

    //! NOTE Last resort: the FFmpeg the user configured for video export
    if (videoEncoderResolver()) {
        dirs.push_back(videoEncoderResolver()->loadedFFmpegDir());
    }

    return dirs;
}

void VideoFramePlayer::ensureWorker()
{
    if (m_availabilityChecked) {
        return;
    }

    m_availabilityChecked = true;

    if (decoderFactory()) {
        m_decoder = decoderFactory()->createDecoder(ffmpegLibsDirs());
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
            m_wake.wait_for(lock, std::chrono::milliseconds(10), [this]() {
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

            const VideoStreamInfo info = isOpen ? m_decoder->streamInfo() : VideoStreamInfo();
            QMetaObject::invokeMethod(this, [this, info, generation]() {
                onStreamInfo(info, generation);
            }, Qt::QueuedConnection);
        }

        if (!isOpen) {
            continue;
        }

        const double target = m_targetSecs.load();

        // Does the buffer already contain, or lead soon to, the frame shown at the target?
        const bool behindBuffer = buffer.empty() || target + PTS_EPSILON_SECS < buffer.front()->ptsSecs;
        const bool farAhead = !buffer.empty() && target > buffer.back()->ptsSecs + MAX_DECODE_FORWARD_SECS;
        if (behindBuffer || farAhead) {
            buffer.clear();
            endOfStream = false;
            m_decoder->seek(target);
        }

        // Decode up to the first frame after the target
        while (!endOfStream && (buffer.empty() || buffer.back()->ptsSecs <= target + PTS_EPSILON_SECS)) {
            decodeNext();
        }

        // Drop the frames before the one shown at the target
        while (buffer.size() >= 2 && buffer[1]->ptsSecs <= target + PTS_EPSILON_SECS) {
            buffer.pop_front();
        }

        if (!buffer.empty() && buffer.front()->ptsSecs != shownPts) {
            shownPts = buffer.front()->ptsSecs;
            QVideoFrame frame = toQVideoFrame(*buffer.front());
            QMetaObject::invokeMethod(this, [this, frame, pts = shownPts, generation]() {
                onFrame(frame, pts, generation);
            }, Qt::QueuedConnection);
        }

        // Read ahead, unless a new target is already waiting
        while (!endOfStream && buffer.size() < READ_AHEAD_FRAMES) {
            {
                std::lock_guard lock(m_mutex);
                if (m_targetChanged || m_openRequested || m_quit) {
                    break;
                }
            }
            decodeNext();
        }
    }

    m_decoder->close();
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

    m_targetSecs.store(positionMs / 1000.0);
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
