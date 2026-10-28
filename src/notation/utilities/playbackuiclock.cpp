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

#include "playbackuiclock.h"

#include <QCoreApplication>

using namespace mu::notation;

//! NOTE: ~43 FPS, the score's playback cursor's former rate (1024 buffer + 44100)
static constexpr int TICK_INTERVAL_MS = 23;

PlaybackUiClock* PlaybackUiClock::instance()
{
    static PlaybackUiClock* clock = new PlaybackUiClock(qApp);
    return clock;
}

PlaybackUiClock::PlaybackUiClock(QObject* parent)
    : QObject(parent)
{
    m_timer.setInterval(TICK_INTERVAL_MS);
    connect(&m_timer, &QTimer::timeout, this, &PlaybackUiClock::tick);
}

void PlaybackUiClock::addUser(const QObject* user)
{
    if (m_users.contains(user)) {
        return;
    }

    m_users.insert(user);
    connect(user, &QObject::destroyed, this, &PlaybackUiClock::onUserDestroyed);

    //! NOTE: not restarted when already ticking: the users share its ticks
    if (!m_timer.isActive()) {
        m_timer.start();
    }
}

void PlaybackUiClock::removeUser(const QObject* user)
{
    if (!m_users.remove(user)) {
        return;
    }

    disconnect(user, &QObject::destroyed, this, &PlaybackUiClock::onUserDestroyed);

    if (m_users.isEmpty()) {
        m_timer.stop();
    }
}

void PlaybackUiClock::onUserDestroyed(QObject* user)
{
    m_users.remove(user);

    if (m_users.isEmpty()) {
        m_timer.stop();
    }
}
