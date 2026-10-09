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

#include "channelselection.h"

#include <algorithm>

#include "mixerchannelitem.h"

using namespace mu::playback;

void ChannelSelection::select(const Channels& channels, MixerChannelItem* item, bool toggle, bool range, int& anchorIndex,
                              const IsSelectable& isSelectable)
{
    if (!item) {
        return;
    }

    const int itemIndex = channels.indexOf(item);

    if (range && anchorIndex >= 0 && itemIndex >= 0) {
        const int from = std::min(anchorIndex, itemIndex);
        const int to = std::max(anchorIndex, itemIndex);

        for (int i = 0; i < channels.size(); ++i) {
            MixerChannelItem* channel = channels.at(i);
            if (isSelectable(channel)) {
                channel->setSelected(i >= from && i <= to);
            }
        }

        return;
    }

    if (toggle) {
        item->setSelected(!item->selected());
        anchorIndex = itemIndex;
        return;
    }

    for (MixerChannelItem* channel : channels) {
        if (channel != item && channel->selected()) {
            channel->setSelected(false);
        }
    }

    item->setSelected(true);
    anchorIndex = itemIndex;
}

void ChannelSelection::clear(const Channels& channels, int& anchorIndex)
{
    for (MixerChannelItem* item : channels) {
        if (item->selected()) {
            item->setSelected(false);
        }
    }

    anchorIndex = -1;
}

void ChannelSelection::setMuted(const Channels& channels, bool muted)
{
    for (MixerChannelItem* item : channels) {
        if (!item->selected()) {
            continue;
        }

        //! NOTE: mirrors MixerMuteAndSoloSection.qml's own per-button
        //! `enabled: !(muted && forceMute)` guard - a channel showing muted purely
        //! because ANOTHER channel's solo force-muted it isn't something the user is
        //! directly interacting with right now, so a multi-select fan-out shouldn't
        //! silently overwrite its own persisted manual mute state as a side effect
        //! of muting/unmuting a DIFFERENT selected channel.
        if (item->muted() && item->forceMute()) {
            continue;
        }

        item->setMuted(muted);
    }
}

void ChannelSelection::setSolo(const Channels& channels, bool solo)
{
    for (MixerChannelItem* item : channels) {
        if (!item->selected()) {
            continue;
        }

        //! NOTE: mirrors MixerMuteAndSoloSection.qml's own per-button `enabled`/
        //! `visible` guards - solo is only meaningful for a non-Aux channel or a
        //! Group-type Aux bus (see that file's own NOTE on why a plain send/return
        //! bus is excluded). The mute condition is deliberately the OPPOSITE of the
        //! Mute case above: `muted && !forceMute` (manually muted), not `muted &&
        //! forceMute` - a force-muted channel's own Solo button stays enabled by
        //! design (soloing it is exactly how you hand the solo over to it), so
        //! skipping it here would silently block the very channel the user just
        //! clicked Solo on from ever taking the solo while part of a selection.
        bool soloEligible = item->type() != MixerChannelItem::Type::Aux || item->isGroupBus();
        bool manuallyMuted = item->muted() && !item->forceMute();
        if (!soloEligible || manuallyMuted) {
            continue;
        }

        item->setSolo(solo);
    }
}
