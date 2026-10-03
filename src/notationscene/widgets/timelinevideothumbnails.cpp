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

#include "timelinevideothumbnails.h"

#include <QThread>

#include "log.h"

using namespace mu::notation;
using namespace muse;
using namespace muse::media;

//! NOTE: e.g. ~350 pictures 160 px high, many more when lower
static constexpr size_t MAX_CACHE_BYTES = 64 * 1024 * 1024;

TimelineVideoThumbnails::TimelineVideoThumbnails(QObject* parent)
    : QObject(parent)
{
}

TimelineVideoThumbnails::~TimelineVideoThumbnails()
{
    {
        std::lock_guard lock(m_mutex);
        m_quit = true;
    }
    m_wake.notify_all();

    if (m_thread) {
        m_thread->wait();
        delete m_thread;
    }
}

void TimelineVideoThumbnails::setVideo(const io::path_t& path)
{
    {
        std::lock_guard lock(m_mutex);
        if (path == m_path) {
            return;
        }

        m_path = path;
        m_openRequested = true;
        m_isOpening = !path.empty();
        m_pending.clear();
        ++m_generation;
    }

    m_cache.clear();
    m_cacheBytes = 0;
    m_lastRequest.clear();
    m_aspectRatio = 0.0;
    m_durationSecs = 0.0;

    if (!path.empty()) {
        ensureWorker();
    }

    m_wake.notify_all();
    emit thumbnailsChanged();
}

bool TimelineVideoThumbnails::isOpening() const
{
    return m_isOpening;
}

double TimelineVideoThumbnails::aspectRatio() const
{
    return m_aspectRatio;
}

double TimelineVideoThumbnails::durationSecs() const
{
    return m_durationSecs;
}

QImage TimelineVideoThumbnails::thumbnail(const Slot& slot) const
{
    // The last frame starting before (or at) the end of the slot: a slot can be empty, e.g. before the
    // start of the video, all at its time 0...
    auto it = m_cache.upper_bound(slot.centerSecs + slot.halfSecs);
    if (it == m_cache.begin()) {
        return QImage();
    }
    --it;

    // ... either starts within the slot, or is known to be still shown at its center
    if (it->first >= slot.centerSecs - slot.halfSecs || it->second.coveredUntilSecs >= slot.centerSecs) {
        return it->second.image;
    }

    return QImage();
}

void TimelineVideoThumbnails::request(const std::vector<Slot>& wanted, int pictureHeight)
{
    if (wanted == m_lastRequest && pictureHeight == m_lastRequestHeight) {
        return;
    }

    m_lastRequest = wanted;
    m_lastRequestHeight = pictureHeight;

    {
        std::lock_guard lock(m_mutex);
        m_pending = wanted;
        m_pictureHeight = pictureHeight;
    }

    m_wake.notify_all();
}

void TimelineVideoThumbnails::ensureWorker()
{
    if (m_thread) {
        return;
    }

    m_thread = QThread::create([this]() {
        workerLoop();
    });
    m_thread->start(QThread::LowPriority);
}

void TimelineVideoThumbnails::workerLoop()
{
    IVideoDecoderPtr decoder;
    if (decoderFactory()) {
        decoder = decoderFactory()->createDecoder(decoderFactory()->defaultFFmpegLibsDirs());
    }

    if (!decoder) {
        LOGW() << "no usable FFmpeg libraries found, no video thumbnails in the Timeline";
    }

    bool isOpen = false;
    quint64 generation = 0;

    while (true) {
        bool openNow = false;
        io::path_t path;
        Slot slot;
        bool hasSlot = false;
        int pictureHeight = 0;

        {
            std::unique_lock lock(m_mutex);
            m_wake.wait(lock, [this]() {
                return m_quit || m_openRequested || !m_pending.empty();
            });

            if (m_quit) {
                break;
            }

            if (m_openRequested) {
                m_openRequested = false;
                openNow = true;
                path = m_path;
                generation = m_generation;
            } else {
                slot = m_pending.front();
                m_pending.erase(m_pending.begin());
                hasSlot = true;
                pictureHeight = m_pictureHeight;
            }
        }

        if (openNow) {
            //! NOTE: reported even without a decoder, so that the row doesn't wait for pictures forever
            if (decoder) {
                decoder->close();
            }
            isOpen = decoder && !path.empty() && decoder->open(path);

            const VideoStreamInfo info = isOpen ? decoder->streamInfo() : VideoStreamInfo();
            const double aspect = info.isValid() ? static_cast<double>(info.width) / info.height : 0.0;
            const double duration = info.durationSecs;
            QMetaObject::invokeMethod(this, [this, generation, aspect, duration]() {
                if (generation == m_generation) {
                    m_isOpening = false;
                    m_aspectRatio = aspect;
                    m_durationSecs = duration;
                    emit thumbnailsChanged();
                }
            }, Qt::QueuedConnection);
            continue;
        }

        if (!isOpen || !hasSlot || pictureHeight <= 0) {
            continue;
        }

        const int maxWidth = static_cast<int>(pictureHeight * 4); // up to 4:1, the height is what limits
        VideoThumbnailPtr thumb = decoder->decodeThumbnail(slot.centerSecs, slot.halfSecs, maxWidth, pictureHeight);
        if (!thumb) {
            continue;
        }

        QImage image = QImage(thumb->rgba.data(), thumb->width, thumb->height, thumb->width * 4, QImage::Format_RGBA8888).copy();
        const double pts = thumb->ptsSecs;
        const double target = slot.centerSecs;
        QMetaObject::invokeMethod(this, [this, generation, pts, target, image]() {
            onThumbnail(generation, pts, target, image);
        }, Qt::QueuedConnection);
    }
}

void TimelineVideoThumbnails::onThumbnail(quint64 generation, double ptsSecs, double targetSecs, const QImage& image)
{
    {
        std::lock_guard lock(m_mutex);
        if (generation != m_generation) {
            return;
        }
    }

    //! NOTE: a target before the first frame shows that frame: filed from the target
    const double key = std::min(ptsSecs, targetSecs);

    Entry& entry = m_cache[key];
    m_cacheBytes -= entry.image.sizeInBytes();
    entry.image = image;
    m_cacheBytes += entry.image.sizeInBytes();
    entry.coveredUntilSecs = std::max(entry.coveredUntilSecs, targetSecs);

    if (m_cacheBytes > MAX_CACHE_BYTES) {
        evict(targetSecs);
    }

    emit thumbnailsChanged();
}

//! NOTE: drops the pictures the farthest from the ones just shown
void TimelineVideoThumbnails::evict(double aroundSecs)
{
    while (m_cacheBytes > MAX_CACHE_BYTES && m_cache.size() > 1) {
        const double first = m_cache.begin()->first;
        const double last = std::prev(m_cache.end())->first;
        auto victim = aroundSecs - first > last - aroundSecs ? m_cache.begin() : std::prev(m_cache.end());
        m_cacheBytes -= victim->second.image.sizeInBytes();
        m_cache.erase(victim);
    }
}
