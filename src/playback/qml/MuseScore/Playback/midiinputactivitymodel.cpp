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

#include "midiinputactivitymodel.h"

using namespace mu::playback;

static constexpr int MIDI_INPUT_ACTIVITY_LIGHT_MS = 100;

MidiInputActivityModel::MidiInputActivityModel(QObject* parent)
    : QObject(parent)
{
    m_lightTimer.setSingleShot(true);
    m_lightTimer.setInterval(MIDI_INPUT_ACTIVITY_LIGHT_MS);
    QObject::connect(&m_lightTimer, &QTimer::timeout, this, [this]() {
        m_active = false;
        emit activeChanged();
    });
}

bool MidiInputActivityModel::active() const
{
    return m_active;
}

void MidiInputActivityModel::load()
{
    // Not the clock and active sensing some devices send all the time, which would keep it lit
    midiInPort()->eventReceived().onReceive(this, [this](const muse::midi::tick_t, const muse::midi::Event& event) {
        if (!event.isChannelVoice()) {
            return;
        }

        m_lightTimer.start();
        if (!m_active) {
            m_active = true;
            emit activeChanged();
        }
    }, muse::async::Asyncable::Mode::SetReplace);
}
