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
#include <QTimer>
#include <qqmlintegration.h>

#include "async/asyncable.h"
#include "modularity/ioc.h"
#include "midi/imidiinport.h"

namespace mu::playback {
//! NOTE: the MIDI input activity light (in the status bar): briefly active on every message received from
//! the MIDI input device - any note (on and off), controller, pitch bend, aftertouch or program change
class MidiInputActivityModel : public QObject, public muse::async::Asyncable
{
    Q_OBJECT

    Q_PROPERTY(bool active READ active NOTIFY activeChanged)

    muse::GlobalInject<muse::midi::IMidiInPort> midiInPort;

    QML_ELEMENT

public:
    explicit MidiInputActivityModel(QObject* parent = nullptr);

    bool active() const;

    Q_INVOKABLE void load();

signals:
    void activeChanged();

private:
    bool m_active = false;
    QTimer m_lightTimer;
};
}
