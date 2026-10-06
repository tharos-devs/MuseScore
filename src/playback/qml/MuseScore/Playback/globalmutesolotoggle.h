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

#include <QList>

#include "audio/common/audiotypes.h"

namespace mu::playback {
class MixerChannelItem;

//! NOTE: the global Mute/Solo buttons' logic (Mixer, Track list), on the channels a caller passes in: the
//! first toggle turns off the active mutes/solos and remembers them, the next one restores them. A pending
//! capture always wins over the live state (engaged), which a channel muted by hand in between can turn back on:
//! branching on that would capture that channel instead of restoring the remembered ones, losing them.
//! Channels are remembered by track, since the caller's items may be rebuilt in between
class GlobalMuteSoloToggle
{
public:
    using Channels = QList<MixerChannelItem*>;

    //! NOTE: live, any muted (not only by another channel's solo) / soloed channel engages it
    static bool muteEngaged(const Channels& channels);
    static bool soloEngaged(const Channels& channels);

    void toggleMute(const Channels& channels);
    void toggleSolo(const Channels& channels);

    void clear();

private:
    QList<muse::audio::TrackId> m_mutedTrackIds;
    QList<muse::audio::TrackId> m_soloedTrackIds;
};
}
