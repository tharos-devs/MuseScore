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

#include <functional>

#include <QList>

namespace mu::playback {
class MixerChannelItem;

//! NOTE: the channel selection of the Mixer and the Track list, kept by the channel items themselves (selected):
//! a click selects a channel alone, Cmd/Ctrl+click adds or removes it, Shift+click selects the channels from the
//! anchor (the last one clicked, an index in the caller's list, which the caller keeps up to date). Mute and Solo
//! clicked on a selected channel apply to the whole selection
class ChannelSelection
{
public:
    using Channels = QList<MixerChannelItem*>;
    using IsSelectable = std::function<bool (const MixerChannelItem*)>;

    static void select(const Channels& channels, MixerChannelItem* item, bool toggle, bool range, int& anchorIndex,
                       const IsSelectable& isSelectable);
    static void clear(const Channels& channels, int& anchorIndex);

    static void setMuted(const Channels& channels, bool muted);
    static void setSolo(const Channels& channels, bool solo);
};
}
