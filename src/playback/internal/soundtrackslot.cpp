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

#include "soundtrackslot.h"

#include "videoaudiodecoder.h"

#include "log.h"

using namespace mu::playback;
using namespace muse;
using namespace muse::audio;

SoundTrackSlot::SoundTrackSlot(Hooks hooks)
    : m_hooks(std::move(hooks))
{
}

SoundTrackSlot::~SoundTrackSlot() = default;

TrackId SoundTrackSlot::trackId() const
{
    return m_trackId;
}

const SoundTrackSlot::Source& SoundTrackSlot::source() const
{
    return m_source;
}

bool SoundTrackSlot::isReady() const
{
    if (m_source.mediaPath.empty() || m_failed) {
        return true;
    }

    //! NOTE The engine applies the RPCs in order: once they're all sent, an export sent after them sees them
    return m_trackId != INVALID_TRACK_ID
           && !m_isBeingAdded
           && !m_isDecodePending
           && m_decodedSourcePath == m_source.mediaPath
           && m_lastAppliedMuteState.has_value();
}

bool SoundTrackSlot::isDecoding() const
{
    return m_isDecodePending;
}

double SoundTrackSlot::decodeProgress() const
{
    return m_decodeProgress;
}

const muse::io::path_t& SoundTrackSlot::wavPath() const
{
    return m_wavPath;
}

bool SoundTrackSlot::decodeFailed() const
{
    return m_decodeFailed;
}

muse::async::Notification SoundTrackSlot::decodeStateChanged() const
{
    return m_decodeStateChanged;
}

bool SoundTrackSlot::isForceMuted() const
{
    return m_isForceMuted;
}

void SoundTrackSlot::update(const Source& source)
{
    m_source = source;

    if (m_source.mediaPath.empty()) {
        remove();
        return;
    }

    if (m_source.mediaPath != m_decodedSourcePath) {
        startDecoding(m_source.mediaPath);
    }

    if (m_trackId == INVALID_TRACK_ID) {
        add();
        return;
    }

    //! NOTE Only reaches the engine when the params actually changed
    applySourceParams();
}

void SoundTrackSlot::reset()
{
    if (m_decoder) {
        m_decoder->cancel();
    }

    const bool wasDecoding = m_isDecodePending;

    m_source = Source();
    m_trackId = INVALID_TRACK_ID;
    m_isBeingAdded = false;
    m_failed = false;
    m_isDecodePending = false;
    m_decodeFailed = false;
    m_decodeProgress = 0.0;
    m_decodedSourcePath = muse::io::path_t();
    m_wavPath = muse::io::path_t();
    m_lastAppliedSourceParams = AudioSourceParams();
    m_lastAppliedMuteState.reset();
    m_isForceMuted = false;

    if (wasDecoding) {
        m_decodeStateChanged.notify();
    }
}

void SoundTrackSlot::startDecoding(const muse::io::path_t& mediaPath)
{
    m_decodedSourcePath = mediaPath;
    m_wavPath = muse::io::path_t();

    //! NOTE Silent until decoded, rather than keep playing the previous file's audio
    applySourceParams();

    if (!m_decoder) {
        m_decoder = std::make_unique<VideoAudioDecoder>();
    }

    m_isDecodePending = true;
    m_decodeFailed = false;
    m_decodeProgress = 0.0;
    m_decodeStateChanged.notify();

    m_decoder->decode(mediaPath, m_hooks.sampleRate(), [this, mediaPath](const muse::io::path_t& wavPath) {
        if (mediaPath != m_decodedSourcePath) {
            return;
        }

        m_isDecodePending = false;
        m_decodeFailed = wavPath.empty();
        m_decodeProgress = 1.0;
        m_wavPath = wavPath;
        applySourceParams();
        m_decodeStateChanged.notify();
    }, [this, mediaPath](double progress) {
        if (mediaPath != m_decodedSourcePath) {
            return;
        }

        m_decodeProgress = progress;
        m_decodeStateChanged.notify();
    });
}

AudioSourceParams SoundTrackSlot::sourceParams() const
{
    AudioSourceParams params;
    params.configuration[SOUND_TRACK_FILE_PATH_KEY] = m_wavPath.toStdString();
    params.configuration[SOUND_TRACK_OFFSET_MS_KEY] = std::to_string(m_source.offsetMs);
    params.configuration[SOUND_TRACK_INCLUDE_IN_EXPORT_KEY] = m_source.includeInExport ? "1" : "0";

    return params;
}

void SoundTrackSlot::applySourceParams()
{
    if (m_trackId == INVALID_TRACK_ID) {
        return;
    }

    AudioSourceParams params = sourceParams();
    if (params == m_lastAppliedSourceParams) {
        return;
    }

    m_hooks.playback()->setSourceParams(m_trackId, params);
    m_lastAppliedSourceParams = params;
}

void SoundTrackSlot::add()
{
    if (m_isBeingAdded || m_trackId != INVALID_TRACK_ID) {
        return;
    }

    m_isBeingAdded = true;
    m_failed = false;

    const project::AudioOutputParams outParams = m_hooks.outputParams();

    TrackParams params;
    params.source = sourceParams();
    params.fxChain = outParams.fxChain;
    params.auxSends = outParams.auxSends;
    params.control = outParams.control();

    //! NOTE Start muted: the owner applies the real mute/solo state as soon as the track exists (onAdded),
    //! which avoids a blip of audio a soloed instrument should have silenced
    params.control.muted = true;

    const uint64_t playbackKey = m_hooks.playbackKey();

    m_hooks.playback()->addSoundTrack(m_hooks.title(), params)
    .onResolve(this, [this, params, playbackKey](const TrackId trackId, const TrackParams&) {
        //! NOTE The notation may have been closed (or another opened) meanwhile. The track may then have been
        //! added after that notation's removeAllTracks(): remove it explicitly, or it (and its whole audio
        //! file in memory) would stay in the engine
        if (m_hooks.playbackKey() != playbackKey) {
            m_hooks.playback()->removeTrack(trackId);
            return;
        }

        m_isBeingAdded = false;
        m_trackId = trackId;
        m_lastAppliedSourceParams = params.source;
        m_lastAppliedMuteState.reset();

        if (m_source.mediaPath.empty()) {
            // The source was removed while the track was being added
            remove();
            return;
        }

        // Catch up with what changed in the meantime (e.g. decoding finished)
        applySourceParams();

        m_hooks.onAdded(trackId);
    })
    .onReject(this, [this](int code, const std::string& msg) {
        m_isBeingAdded = false;
        m_failed = true;
        LOGE() << "unable to add the sound track \"" << m_hooks.title() << "\", error code: " << code << ", " << msg;
    });
}

void SoundTrackSlot::remove()
{
    if (m_decoder) {
        m_decoder->cancel();
    }

    const bool wasDecoding = m_isDecodePending;
    m_isDecodePending = false;
    m_decodeFailed = false;
    m_decodedSourcePath = muse::io::path_t();
    m_wavPath = muse::io::path_t();

    if (wasDecoding) {
        m_decodeStateChanged.notify();
    }

    if (m_trackId == INVALID_TRACK_ID) {
        return;
    }

    TrackId trackId = m_trackId;
    m_trackId = INVALID_TRACK_ID;
    m_lastAppliedSourceParams = AudioSourceParams();
    m_lastAppliedMuteState.reset();
    m_isForceMuted = false;

    //! NOTE Same as removeAuxBus(): clearing the fx chain first is what actually destroys its plugins
    m_hooks.playback()->setFxChainParams(trackId, AudioFxChain());
    m_hooks.playback()->removeTrack(trackId);

    m_hooks.onRemoved(trackId);
}

bool SoundTrackSlot::applyMuteState(bool muted, bool forceMute)
{
    if (m_trackId == INVALID_TRACK_ID) {
        return false;
    }

    m_isForceMuted = forceMute;

    const std::pair<bool, bool> state { muted, forceMute };
    if (m_lastAppliedMuteState == state) {
        return false;
    }

    project::AudioOutputParams params = m_hooks.outputParams();
    params.muted = muted;
    m_hooks.playback()->setControlParams(m_trackId, params.control());
    m_lastAppliedMuteState = state;

    return true;
}
