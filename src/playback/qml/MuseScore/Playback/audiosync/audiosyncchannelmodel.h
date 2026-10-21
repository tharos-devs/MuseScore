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

#pragma once

#include <QObject>
#include <qqmlintegration.h>

#include "async/asyncable.h"
#include "audio/main/iplayback.h"
#include "context/iglobalcontext.h"
#include "modularity/ioc.h"

#include "iplaybackcontroller.h"
#include "mixerchannelitem.h"
#include "soundtrackchannel.h"

namespace mu::playback {
//! NOTE The Audio sync window's channel controls: the reference audio's channel, as in the Mixer (see SoundTrackChannel)
class AudioSyncChannelModel : public QObject, public muse::Contextable, public muse::async::Asyncable
{
    Q_OBJECT
    //! NOTE null without a reference audio track
    Q_PROPERTY(MixerChannelItem * channelItem READ channelItem NOTIFY channelItemChanged)
    QML_ELEMENT

    muse::ContextInject<context::IGlobalContext> globalContext = { this };
    muse::ContextInject<IPlaybackController> playbackController = { this };
    muse::ContextInject<muse::audio::IPlayback> playback = { this };

public:
    explicit AudioSyncChannelModel(QObject* parent = nullptr);

    Q_INVOKABLE void load();

    MixerChannelItem* channelItem() const;

signals:
    void channelItemChanged();

private:
    const SoundTrackChannel* channel() const;
    void rebuild();
    void onProjectChanged();

    SoundTrackChannels m_channels;
    MixerChannelItem* m_item = nullptr;
};
}
