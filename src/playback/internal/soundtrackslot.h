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

#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <atomic>
#include <utility>

#include <QObject>
#include <QString>

#include "async/asyncable.h"
#include "async/notification.h"
#include "global/io/path.h"
#include "audio/common/audiotypes.h"
#include "audio/main/iplayback.h"
#include "project/iprojectaudiosettings.h"

namespace mu::playback {
class VideoAudioDecoder;

//! NOTE One engine sound track playing a media file's decoded audio on the score's clock (see
//! IPlayback::addSoundTrack()): the attached video's audio, the Audio sync window's reference audio.
//! Owns the decoding, the track's lifetime and what's sent to the engine; the owner gives it the
//! wanted state (update()) and its persisted output params (Hooks::outputParams).
class SoundTrackSlot : public muse::async::Asyncable
{
public:
    struct Source {
        muse::io::path_t mediaPath; // empty: no track
        //! NOTE File position = score position + offset
        int offsetMs = 0;
        bool includeInExport = false;
        //! NOTE Played transposed, and at this speed (the score's playback speed: it follows it), each one
        //! without changing the other (see transformWav()): a transformed copy is made first
        int pitchSemitones = 0;
        double speed = 1.0;
    };

    struct Hooks {
        std::function<muse::audio::IPlayback* ()> playback;
        std::function<unsigned int()> sampleRate;
        std::function<std::string()> title;
        std::function<project::AudioOutputParams()> outputParams;
        //! NOTE Identifies the notation the track belongs to: a track added for another one is removed at once
        std::function<uint64_t()> playbackKey;
        //! NOTE Called once the track exists (after its catch-up), or right after it was removed
        std::function<void(muse::audio::TrackId)> onAdded;
        std::function<void(muse::audio::TrackId)> onRemoved;
    };

    explicit SoundTrackSlot(Hooks hooks);
    ~SoundTrackSlot() override;

    //! NOTE Adds/removes the track, decodes a new file, sends changed source params
    void update(const Source& source);
    //! NOTE The notation's tracks were all removed by the engine: forget everything, without engine calls
    void reset();

    muse::audio::TrackId trackId() const;
    const Source& source() const;

    //! NOTE Whether the track is completely set up for the current source (added, decoded, its mute state
    //! applied): an export must wait for it. True without a source, or if the engine refused the track
    bool isReady() const;

    bool isDecoding() const;
    //! NOTE In [0; 1], meaningful while decoding
    double decodeProgress() const;
    //! NOTE The decoded WAV file, empty while decoding or if decoding failed (not transposed)
    const muse::io::path_t& wavPath() const;
    //! NOTE A transposed copy is being made (the previous file plays meanwhile); progress in [0; 1]
    bool isPitchShifting() const;
    double pitchShiftProgress() const;
    //! NOTE The last decoding finished without any audio (missing file, unsupported format, no audio stream)
    bool decodeFailed() const;
    //! NOTE Decoding started/progressed/finished
    muse::async::Notification decodeStateChanged() const;

    //! NOTE Sends the mute state if it changed; returns whether it did
    bool applyMuteState(bool muted, bool forceMute);
    bool isForceMuted() const;

private:
    void add();
    void remove();
    void startDecoding(const muse::io::path_t& mediaPath);
    muse::audio::AudioSourceParams sourceParams() const;
    void applySourceParams();
    //! NOTE The file to play: the decoded one, or its transposed copy (made if needed)
    void updatePlayedWav();
    void cancelPitchShift();

    Hooks m_hooks;
    Source m_source;

    muse::audio::TrackId m_trackId = muse::audio::INVALID_TRACK_ID;
    bool m_isBeingAdded = false;
    bool m_failed = false; // the engine refused it: nothing to wait for

    std::unique_ptr<VideoAudioDecoder> m_decoder;
    bool m_isDecodePending = false;
    bool m_decodeFailed = false;
    double m_decodeProgress = 0.0;
    muse::io::path_t m_decodedSourcePath; //! the file m_wavPath is (being) decoded from
    muse::io::path_t m_wavPath;
    muse::io::path_t m_playedWavPath;
    double m_playedSpeed = 1.0; // of the played file: its offset is in its own time

    std::unique_ptr<QObject> m_pitchShiftContext; // results are delivered in its thread (the main one)
    std::shared_ptr<std::atomic<bool> > m_pitchShiftCanceled;
    QString m_pitchShiftTarget;
    double m_pitchShiftProgress = 0.0;
    muse::async::Notification m_decodeStateChanged;

    muse::audio::AudioSourceParams m_lastAppliedSourceParams;
    std::optional<std::pair<bool, bool> > m_lastAppliedMuteState; //! (muted, forceMute)
    bool m_isForceMuted = false;
};
}
