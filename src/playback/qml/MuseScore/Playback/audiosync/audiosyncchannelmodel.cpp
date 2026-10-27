/*
 * SPDX-License-Identifier: GPL-3.0-only
 * MuseScore-CLA-applies
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

#include "audiosyncchannelmodel.h"

#include "project/inotationproject.h"

using namespace mu::playback;
using namespace muse::audio;

AudioSyncChannelModel::AudioSyncChannelModel(QObject* parent)
    : QObject(parent), muse::Contextable(muse::iocCtxForQmlObject(this))
{
}

void AudioSyncChannelModel::load()
{
    m_channels = makeSoundTrackChannels(iocContext());

    //! NOTE The channel follows the reference audio's track
    playbackController()->trackAdded().onReceive(this, [this](const TrackId trackId) {
        if (trackId == playbackController()->referenceAudioTrackId()) {
            rebuild();
        }
    });

    playbackController()->trackRemoved().onReceive(this, [this](const TrackId trackId) {
        if (m_item && m_item->trackId() == trackId) {
            rebuild();
        }
    });

    playbackController()->referenceAudioMuteStateChanged().onReceive(this, [this](bool muted, bool forceMute) {
        if (m_item) {
            m_item->loadMuteForceMuteState(muted, forceMute);
        }
    });

    projectSession()->projectChanged().onNotify(this, [this]() {
        onProjectChanged();
    });

    onProjectChanged();
}

const SoundTrackChannel* AudioSyncChannelModel::channel() const
{
    return findSoundTrackChannel(m_channels, MixerChannelItem::Type::ReferenceAudio);
}

void AudioSyncChannelModel::onProjectChanged()
{
    if (const SoundTrackChannel* soundTrack = channel()) {
        if (std::optional<muse::async::Notification> changed = soundTrack->settingsChanged()) {
            //! NOTE Its solo/mute (and track delay) are stored with the reference audio, shared with the other panels
            changed->onNotify(this, [this]() {
                if (m_item && channel()) {
                    channel()->loadMuteState(m_item);
                }
            }, Asyncable::Mode::SetReplace);
        }
    }

    rebuild();
}

void AudioSyncChannelModel::rebuild()
{
    MixerChannelItem* oldItem = m_item;
    m_item = nullptr;

    const SoundTrackChannel* soundTrack = channel();
    const TrackId trackId = playbackController()->referenceAudioTrackId();
    if (soundTrack && trackId != INVALID_TRACK_ID) {
        m_item = new MixerChannelItem(this, soundTrack->type, true /*outputOnly*/, trackId);
        m_item->setTitle(soundTrack->title());
        soundTrack->loadMuteState(m_item);
        m_item->loadOutputParams(soundTrack->outputParams());
        m_item->subscribeOnTrackAudioSignalChanges();

        soundTrack->connectItem(m_item, this, [this]() { return playback().get(); }, [this]() {
            project::INotationProjectPtr project = projectSession()->project();
            return project ? project->undoStack() : nullptr;
        });
    }

    emit channelItemChanged();

    //! NOTE Only once the view has released it
    if (oldItem) {
        oldItem->disconnect(this);
        oldItem->deleteLater();
    }
}

MixerChannelItem* AudioSyncChannelModel::channelItem() const
{
    return m_item;
}
