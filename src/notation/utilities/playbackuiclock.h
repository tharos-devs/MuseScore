/*
 * SPDX-License-Identifier: GPL-3.0-only
 * MuseScore-Studio-CLA-applies
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

#include <QObject>
#include <QSet>
#include <QTimer>

#include <functional>

namespace mu::notation {
//! NOTE: the one clock of what moves on screen during playback: the score's and the Timeline's playback cursors,
//! the Mixer's and the Track list's meters. With a timer each, their updates came at unrelated times and each one
//! made Qt Quick render its own frame: up to one at each screen refresh. Updated at the same tick, they share one
class PlaybackUiClock : public QObject
{
    Q_OBJECT

public:
    static PlaybackUiClock* instance();

    //! NOTE: the clock ticks while it has users (e.g. a playback cursor while playing, a meter with a level to show)
    void addUser(const QObject* user);
    void removeUser(const QObject* user);

signals:
    void tick();

private:
    explicit PlaybackUiClock(QObject* parent);

    void onUserDestroyed(QObject* user);

    QTimer m_timer;
    QSet<const QObject*> m_users;
};

//! NOTE: a call made at the playback UI clock's next tick, once however many times it was requested since the last
//! one (e.g. a meter showing the last level received)
class PlaybackUiTickCall : public QObject
{
public:
    PlaybackUiTickCall(QObject* parent, std::function<void()> func);

    void request();
    void cancel();

private:
    std::function<void()> m_func;
    bool m_requested = false;
};
}
