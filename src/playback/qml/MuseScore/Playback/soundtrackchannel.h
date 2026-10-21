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
#include <optional>
#include <vector>

#include <QColor>
#include <QString>

#include "async/channel.h"
#include "async/notification.h"
#include "modularity/ioc.h"

#include "audio/common/audiotypes.h"
#include "notation/inotationsolomutestate.h"
#include "audio/main/iplayback.h"
#include "project/iprojectaudiosettings.h"
#include "project/iprojectundostack.h"
#include "playback/playbacktypes.h"

#include "mixerchannelitem.h"

namespace mu::playback {
//! NOTE: a sound track: an engine track not made from the score (the attached video's audio, the Audio sync
//! window's reference audio), existing as long as its source is set. PlaybackController keeps its output params,
//! its user solo/mute are stored with its source's project settings. The Mixer and the Track list each show it as
//! a dedicated channel, the same way for all of them, through this
struct SoundTrackChannel
{
    using SoloMuteState = notation::INotationSoloMuteState::SoloMuteState;

    MixerChannelItem::Type type = MixerChannelItem::Type::Unknown;
    ChannelCategory category = ChannelCategory::Video;

    //! NOTE: translated
    std::function<QString()> title;

    std::function<muse::audio::TrackId()> trackId;
    std::function<project::AudioOutputParams()> outputParams;
    std::function<void(const project::AudioOutputParams&)> setOutputParams;

    //! NOTE: whether its source (video, audio file) is set
    std::function<bool()> hasSource;

    //! NOTE: the user's own solo/mute (without the force-mute)
    std::function<SoloMuteState()> userSoloMuteState;
    std::function<void(const SoloMuteState&)> storeUserSoloMuteState;

    //! NOTE: muted only because some other track is soloed
    std::function<bool()> isForceMuted;
    //! NOTE: the engine's live mute changed (muted, forceMute)
    std::function<muse::async::Channel<bool, bool>()> muteStateChanged;

    //! NOTE: its track delay (see MixerChannelItem::delayMs), if it has one (not set otherwise)
    std::function<int()> delayMs;
    std::function<void(int)> setDelayMs;

    //! NOTE: its source or its user solo/mute changed (the current project's settings, nullopt without a project)
    std::function<std::optional<muse::async::Notification>()> settingsChanged;

    //! NOTE: same rules as an instrument track: soloing unmutes
    void setUserSoloMuteState(const SoloMuteState& state) const;

    //! NOTE: the effective mute: the user's, or forced by another track's solo
    bool isEffectivelyMuted() const;

    //! NOTE: the user's solo/mute and the live effective mute/force-mute (and its track delay, stored with them)
    void loadMuteState(MixerChannelItem* item) const;

    //! NOTE: not undoable, like its other output params
    void setColor(const QColor& color) const;

    //! NOTE: what a channel of it changes goes to the engine and is stored (volume, pan, gain, EQ, solo/mute, color),
    //! undoably for the gestures; the same in every panel showing it. context: the connections' lifetime
    void connectItem(MixerChannelItem* item, QObject* context, const std::function<muse::audio::IPlayback* ()>& playback,
                     const std::function<project::IProjectUndoStackPtr()>& undoStack) const;
};

using SoundTrackChannels = std::vector<SoundTrackChannel>;

//! NOTE: in display order: the Video's, then the Reference audio's
SoundTrackChannels makeSoundTrackChannels(const muse::modularity::ContextPtr& iocContext);

const SoundTrackChannel* findSoundTrackChannel(const SoundTrackChannels& channels, MixerChannelItem::Type type);
}
