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

#include "globalmutesolotoggle.h"

#include "mixerchannelitem.h"

using namespace mu::playback;
using namespace muse::audio;

//! NOTE: an item can read muted() == true purely because some OTHER item's solo force-muted it (see
//! MixerChannelItem::loadMuteForceMuteState()) - that's not a mute the user asked for
static bool isExplicitlyMuted(const MixerChannelItem* item)
{
    return item->muted() && !item->forceMute();
}

static MixerChannelItem* findChannel(const GlobalMuteSoloToggle::Channels& channels, TrackId trackId)
{
    for (MixerChannelItem* item : channels) {
        if (item->trackId() == trackId) {
            return item;
        }
    }

    return nullptr;
}

bool GlobalMuteSoloToggle::muteEngaged(const Channels& channels)
{
    for (const MixerChannelItem* item : channels) {
        if (isExplicitlyMuted(item)) {
            return true;
        }
    }

    return false;
}

bool GlobalMuteSoloToggle::soloEngaged(const Channels& channels)
{
    for (const MixerChannelItem* item : channels) {
        if (item->solo()) {
            return true;
        }
    }

    return false;
}

void GlobalMuteSoloToggle::toggleMute(const Channels& channels)
{
    if (!m_mutedTrackIds.isEmpty()) {
        for (TrackId trackId : std::as_const(m_mutedTrackIds)) {
            if (MixerChannelItem* item = findChannel(channels, trackId)) {
                item->setMuted(true);
            }
        }
        m_mutedTrackIds.clear();
        return;
    }

    for (MixerChannelItem* item : channels) {
        if (isExplicitlyMuted(item)) {
            m_mutedTrackIds.push_back(item->trackId());
            item->setMuted(false);
        }
    }
}

void GlobalMuteSoloToggle::toggleSolo(const Channels& channels)
{
    if (!m_soloedTrackIds.isEmpty()) {
        for (TrackId trackId : std::as_const(m_soloedTrackIds)) {
            if (MixerChannelItem* item = findChannel(channels, trackId)) {
                item->setSolo(true);
            }
        }
        m_soloedTrackIds.clear();
        return;
    }

    for (MixerChannelItem* item : channels) {
        if (item->solo()) {
            m_soloedTrackIds.push_back(item->trackId());
            item->setSolo(false);
        }
    }
}

void GlobalMuteSoloToggle::clear()
{
    m_mutedTrackIds.clear();
    m_soloedTrackIds.clear();
}
