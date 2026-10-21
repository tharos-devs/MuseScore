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

#include "soundtrackchannel.h"

#include "channeleditundo.h"

#include "context/iglobalcontext.h"
#include "translation.h"

#include "project/inotationproject.h"
#include "project/iprojectaudiosyncsettings.h"
#include "project/iprojectvideosettings.h"

#include "iplaybackcontroller.h"

using namespace mu::playback;
using namespace mu::project;
using namespace muse;
using namespace muse::audio;

namespace {
//! NOTE: resolved when used, shared by the channels' functions
struct SoundTrackServices : public muse::Contextable
{
    muse::ContextInject<IPlaybackController> controller = { this };
    muse::ContextInject<mu::context::IGlobalContext> context = { this };

    explicit SoundTrackServices(const muse::modularity::ContextPtr& ctx)
        : muse::Contextable(ctx) {}

    INotationProjectPtr project() const
    {
        return context()->currentProject();
    }

    IProjectVideoSettingsPtr videoSettings() const
    {
        const INotationProjectPtr project = this->project();
        return project ? project->videoSettings() : nullptr;
    }

    IProjectAudioSyncSettingsPtr audioSyncSettings() const
    {
        const INotationProjectPtr project = this->project();
        return project ? project->audioSyncSettings() : nullptr;
    }
};

using ServicesPtr = std::shared_ptr<SoundTrackServices>;

SoundTrackChannel makeVideoChannel(const ServicesPtr& services)
{
    SoundTrackChannel channel;
    channel.type = MixerChannelItem::Type::Video;
    channel.category = ChannelCategory::Video;
    channel.title = []() { return muse::qtrc("playback", "Video"); };

    channel.trackId = [services]() { return services->controller()->videoTrackId(); };
    channel.outputParams = [services]() { return services->controller()->videoOutputParams(); };
    channel.setOutputParams = [services](const AudioOutputParams& params) { services->controller()->setVideoOutputParams(params); };
    channel.isForceMuted = [services]() { return services->controller()->isVideoForceMuted(); };
    channel.muteStateChanged = [services]() { return services->controller()->videoMuteStateChanged(); };

    channel.hasSource = [services]() {
        const IProjectVideoSettingsPtr settings = services->videoSettings();
        return settings && settings->attachment().isValid();
    };

    //! NOTE: stored in the attachment, shared with the Video panel's "M" button and the Timeline;
    //! PlaybackController applies it to the engine (see updateSoloMuteStates())
    channel.userSoloMuteState = [services]() -> SoundTrackChannel::SoloMuteState {
        const IProjectVideoSettingsPtr settings = services->videoSettings();
        if (!settings) {
            return {};
        }
        return { settings->attachment().muted, settings->attachment().solo };
    };

    channel.storeUserSoloMuteState = [services](const SoundTrackChannel::SoloMuteState& state) {
        updateVideoAttachment(services->videoSettings(), [&state](VideoAttachmentSettings& attachment) {
            attachment.muted = state.mute;
            attachment.solo = state.solo;
        });
    };

    channel.settingsChanged = [services]() -> std::optional<async::Notification> {
        const IProjectVideoSettingsPtr settings = services->videoSettings();
        if (!settings) {
            return std::nullopt;
        }
        return settings->settingsChanged();
    };

    return channel;
}

SoundTrackChannel makeReferenceAudioChannel(const ServicesPtr& services)
{
    SoundTrackChannel channel;
    channel.type = MixerChannelItem::Type::ReferenceAudio;
    channel.category = ChannelCategory::ReferenceAudio;
    channel.title = []() { return muse::qtrc("playback", "Audio"); };

    channel.trackId = [services]() { return services->controller()->referenceAudioTrackId(); };
    channel.outputParams = [services]() { return services->controller()->referenceAudioOutputParams(); };
    channel.setOutputParams = [services](const AudioOutputParams& params) {
        services->controller()->setReferenceAudioOutputParams(params);
    };
    channel.isForceMuted = [services]() { return services->controller()->isReferenceAudioForceMuted(); };
    channel.muteStateChanged = [services]() { return services->controller()->referenceAudioMuteStateChanged(); };

    channel.hasSource = [services]() {
        const IProjectAudioSyncSettingsPtr settings = services->audioSyncSettings();
        return settings && settings->referenceAudio().isValid();
    };

    //! NOTE: stored with the reference audio, shared with the Audio sync window;
    //! PlaybackController applies it to the engine (see updateSoloMuteStates())
    channel.userSoloMuteState = [services]() -> SoundTrackChannel::SoloMuteState {
        const IProjectAudioSyncSettingsPtr settings = services->audioSyncSettings();
        if (!settings) {
            return {};
        }
        return { settings->referenceAudio().muted, settings->referenceAudio().solo };
    };

    channel.storeUserSoloMuteState = [services](const SoundTrackChannel::SoloMuteState& state) {
        updateReferenceAudio(services->audioSyncSettings(), [&state](ReferenceAudioSettings& referenceAudio) {
            referenceAudio.muted = state.mute;
            referenceAudio.solo = state.solo;
        });
    };

    //! NOTE: stored with the reference audio too (undoable through the Mixer's channel edits)
    channel.delayMs = [services]() {
        const IProjectAudioSyncSettingsPtr settings = services->audioSyncSettings();
        return settings ? settings->referenceAudio().delayMs : 0;
    };

    channel.setDelayMs = [services](int delayMs) {
        updateReferenceAudio(services->audioSyncSettings(), [delayMs](ReferenceAudioSettings& referenceAudio) {
            referenceAudio.delayMs = delayMs;
        });
    };

    channel.settingsChanged = [services]() -> std::optional<async::Notification> {
        const IProjectAudioSyncSettingsPtr settings = services->audioSyncSettings();
        if (!settings) {
            return std::nullopt;
        }
        return settings->settingsChanged();
    };

    return channel;
}
}

void SoundTrackChannel::setUserSoloMuteState(const SoloMuteState& state) const
{
    storeUserSoloMuteState({ state.solo ? false : state.mute, state.solo });
}

bool SoundTrackChannel::isEffectivelyMuted() const
{
    const SoloMuteState state = userSoloMuteState();
    return (state.mute && !state.solo) || isForceMuted();
}

void SoundTrackChannel::loadMuteState(MixerChannelItem* item) const
{
    //! NOTE: exactly like PlaybackController::updateVideoMuteState()/updateReferenceAudioMuteState()
    const SoloMuteState state = userSoloMuteState();
    item->loadSoloMuteState(state);

    const bool forceMute = isForceMuted();
    item->loadMuteForceMuteState((state.mute && !state.solo) || forceMute, forceMute);

    if (delayMs) {
        item->bindExternalDelay(delayMs, setDelayMs);
    }
}

void SoundTrackChannel::setColor(const QColor& color) const
{
    AudioOutputParams params = outputParams();
    params.color = color;
    setOutputParams(params);
}

void SoundTrackChannel::connectItem(MixerChannelItem* item, QObject* context, const std::function<IPlayback*()>& playback,
                                    const std::function<IProjectUndoStackPtr()>& undoStack) const
{
    const SoundTrackChannel channel = *this;
    const TrackId trackId = item->trackId();

    QObject::connect(item, &MixerChannelItem::controlParamsChanged, context,
                     [channel, trackId, playback](const AudioOutputParams& params) {
        //! NOTE The engine's mute is always the live effective one (force-mute included), whatever the item shows
        ControlParams control = params.control();
        control.muted = channel.isEffectivelyMuted();
        playback()->setControlParams(trackId, control);

        AudioOutputParams outParams = channel.outputParams();
        outParams.volume = params.volume;
        outParams.balance = params.balance;
        outParams.gain = params.gain;
        outParams.eq = params.eq;
        channel.setOutputParams(outParams);
    });

    QObject::connect(item, &MixerChannelItem::soloMuteStateChanged, context,
                     [channel](const notation::INotationSoloMuteState::SoloMuteState& state) {
        //! NOTE Stored with its source's settings, shared with its own UI (e.g. the Video panel's "M" button);
        //! PlaybackController applies it to the engine (see updateSoloMuteStates())
        channel.setUserSoloMuteState(state);
    });

    QObject::connect(item, &MixerChannelItem::colorChanged, context, [channel, item]() {
        channel.setColor(item->color());
    });

    ChannelEditUndo::connectContinuousChanges(item, context, undoStack);
}

SoundTrackChannels mu::playback::makeSoundTrackChannels(const muse::modularity::ContextPtr& iocContext)
{
    const ServicesPtr services = std::make_shared<SoundTrackServices>(iocContext);
    return { makeVideoChannel(services), makeReferenceAudioChannel(services) };
}

const SoundTrackChannel* mu::playback::findSoundTrackChannel(const SoundTrackChannels& channels, MixerChannelItem::Type type)
{
    for (const SoundTrackChannel& channel : channels) {
        if (channel.type == type) {
            return &channel;
        }
    }

    return nullptr;
}
