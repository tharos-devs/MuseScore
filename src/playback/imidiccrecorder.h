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

#include "modularity/imoduleinterface.h"
#include "async/notification.h"

namespace mu::playback {
//! NOTE: records the MIDI CCs received from the MIDI input device into the MIDI CC curves of the
//! selected staff's VST instrument, during playback (each CC into its own curve, "touch" mode)
class IMidiCcRecorder : MODULE_CONTEXT_INTERFACE
{
    INTERFACE_ID(IMidiCcRecorder)

public:
    virtual ~IMidiCcRecorder() = default;

    virtual bool isArmed() const = 0;
    virtual void toggleArmed() = 0;
    virtual muse::async::Notification armedChanged() const = 0;
};
}
