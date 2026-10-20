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

#include "async/notification.h"
#include "global/io/path.h"

namespace mu::project {
//! NOTE The reference audio file of the "Audio sync" window: a recording the score's tempo is synced to
//! (the sync points themselves are tempo curve points, stored with the score's automation)
struct ReferenceAudioSettings
{
    //! NOTE Absolute path. Saved with a path relative to the score too, used when the absolute one
    //! doesn't exist anymore (score and audio moved together)
    muse::io::path_t path;
    bool muted = false;
    bool solo = false;
    //! NOTE Whether the reference audio is mixed into audio/video exports (off by default)
    bool includeInExport = false;
    //! NOTE Display: left/right channels drawn separately instead of one mixed waveform (a mono file is
    //! always drawn as one)
    bool showStereo = true;
    //! NOTE Its track delay (the Mixer's): played this much later (negative: earlier), to compensate a latency
    //! heard between it and the score; the sync points and the display aren't affected
    int delayMs = 0;
    //! NOTE The Audio sync window's A-B loop, in the audio's time (< 0: not set); while enabled, it's played
    //! instead of the score's loop
    double loopFromSecs = -1.0;
    double loopToSecs = -1.0;
    bool loopEnabled = false;

    bool hasLoop() const
    {
        return loopFromSecs >= 0.0 && loopToSecs > loopFromSecs;
    }

    bool isValid() const
    {
        return !path.empty();
    }

    bool operator==(const ReferenceAudioSettings& other) const
    {
        return path == other.path
               && muted == other.muted
               && solo == other.solo
               && includeInExport == other.includeInExport
               && showStereo == other.showStereo
               && delayMs == other.delayMs
               && loopFromSecs == other.loopFromSecs
               && loopToSecs == other.loopToSecs
               && loopEnabled == other.loopEnabled;
    }

    bool operator!=(const ReferenceAudioSettings& other) const
    {
        return !(*this == other);
    }
};

//! NOTE How the Audio sync window was showing it (zoom, position): saved with the project, but not a change of it
struct AudioSyncViewState
{
    double pixelsPerSecond = 0.0; // 0: not set (fits the whole audio)
    double scrollSecs = 0.0;
    double amplitudeZoom = 1.0;

    bool isValid() const
    {
        return pixelsPerSecond > 0.0;
    }
};

class IProjectAudioSyncSettings
{
public:
    virtual ~IProjectAudioSyncSettings() = default;

    virtual const ReferenceAudioSettings& referenceAudio() const = 0;
    virtual void setReferenceAudio(const ReferenceAudioSettings& settings) = 0;
    virtual void clearReferenceAudio() = 0;

    virtual muse::async::Notification settingsChanged() const = 0;

    //! NOTE Doesn't notify settingsChanged (nor mark the project as modified)
    virtual const AudioSyncViewState& viewState() const = 0;
    virtual void setViewState(const AudioSyncViewState& state) = 0;
};

using IProjectAudioSyncSettingsPtr = std::shared_ptr<IProjectAudioSyncSettings>;

//! NOTE Read-modify-write of the current reference audio settings; a no-op without a reference audio
void updateReferenceAudio(const IProjectAudioSyncSettingsPtr& settings, const std::function<void(ReferenceAudioSettings&)>& mutate);
}
