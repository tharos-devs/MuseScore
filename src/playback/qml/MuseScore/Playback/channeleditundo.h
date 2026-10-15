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
#include <utility>
#include <vector>

#include <QList>
#include <QObject>
#include <QPointer>

#include "types/translatablestring.h"

#include "audio/common/audiotypes.h"
#include "project/iprojectundostack.h"

namespace mu::playback {
class MixerChannelItem;

//! NOTE: the undo steps of what is changed on a channel, in any panel showing it (Mixer, Track list). An undo/redo
//! finds a channel bound to the track when it runs (see MixerChannelItem::findBoundChannel()), never the panel's:
//! that panel may be closed by then, the change reaching the others through the engine
class ChannelEditUndo
{
public:
    using UndoStackGetter = std::function<project::IProjectUndoStackPtr()>;
    //! NOTE: the panel's channels: an effect or aux send picked on a selected one goes to the other selected ones
    using ChannelsGetter = std::function<QList<MixerChannelItem*>()>;
    //! NOTE: the channels that change their effects, and their chains before
    using ChannelFxChains = std::vector<std::pair<QPointer<MixerChannelItem>, muse::audio::AudioFxChain> >;

    //! NOTE: the volume, pan, gain, EQ and aux send level gestures (see MixerChannelItem::beginVolumeChange() et al.),
    //! for every channel type. context: the connections' lifetime
    static void connectContinuousChanges(MixerChannelItem* item, QObject* context, const UndoStackGetter& undoStack);

    //! NOTE: an effect (or "No effect") picked in a slot, and a send slot pointed at another bus (or "No aux send"):
    //! on a selected instrument channel, applied to the other selected ones too, as one undo step
    static void connectPickedChanges(MixerChannelItem* item, QObject* context, const UndoStackGetter& undoStack,
                                     const ChannelsGetter& channels);

    //! NOTE: an undo step restoring the FX chains of the channels changed since before (their own chains: another
    //! panel's channels of the same tracks get the change later, through the engine)
    static void pushFxChainsUndo(const project::IProjectUndoStackPtr& undoStack, const muse::TranslatableString& actionName,
                                 const ChannelFxChains& before);
};
}
