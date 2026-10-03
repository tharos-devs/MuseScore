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

#include <condition_variable>
#include <map>
#include <mutex>
#include <vector>

#include <QImage>
#include <QObject>

#include "modularity/ioc.h"
#include "media/ivideodecoderfactory.h"

class QThread;

namespace mu::notation {
//! NOTE: the pictures of the Timeline's Video row. Decoded only on request, in the background (a low
//! priority thread with its own decoder), and kept in memory. A request replaces the previous one, so
//! scrolling or zooming never queues up work for pictures no longer shown.
class TimelineVideoThumbnails : public QObject
{
    Q_OBJECT

    muse::GlobalInject<muse::media::IVideoDecoderFactory> decoderFactory;

public:
    //! NOTE: wants the frame shown at centerSecs; any frame shown within centerSecs +/- halfSecs will do
    struct Slot {
        double centerSecs = 0.0;
        double halfSecs = 0.0;

        bool operator==(const Slot& other) const
        {
            return centerSecs == other.centerSecs && halfSecs == other.halfSecs;
        }
    };

    explicit TimelineVideoThumbnails(QObject* parent = nullptr);
    ~TimelineVideoThumbnails() override;

    //! NOTE: an empty path closes the video
    void setVideo(const muse::io::path_t& path);

    //! NOTE: between setVideo() and the video being open (or failing to)
    bool isOpening() const;

    //! NOTE: both 0 until the video is open (and if it can't be)
    double aspectRatio() const;
    double durationSecs() const;

    //! NOTE: a null image if not decoded yet; may be smaller than wanted (decoded for a lower row)
    QImage thumbnail(const Slot& slot) const;

    //! NOTE: decodes the given slots, in order, pictureHeight pixels high
    void request(const std::vector<Slot>& wanted, int pictureHeight);

signals:
    void thumbnailsChanged();

private:
    struct Entry {
        QImage image;
        double coveredUntilSecs = 0.0; // the frame is the one shown from its pts up to (at least) this time
    };

    void workerLoop();
    void ensureWorker();
    void onThumbnail(quint64 generation, double ptsSecs, double targetSecs, const QImage& image);
    void evict(double aroundSecs);

    // Main thread
    std::map<double, Entry> m_cache; // by pts
    std::vector<Slot> m_lastRequest;
    int m_lastRequestHeight = 0;
    size_t m_cacheBytes = 0;
    double m_aspectRatio = 0.0;
    double m_durationSecs = 0.0;
    bool m_isOpening = false;
    QThread* m_thread = nullptr;

    // Shared with the worker
    std::mutex m_mutex;
    std::condition_variable m_wake;
    bool m_quit = false;
    bool m_openRequested = false;
    muse::io::path_t m_path;
    int m_pictureHeight = 0; // of the pending request
    quint64 m_generation = 0;
    std::vector<Slot> m_pending;
};
}
