/*
 * SPDX-License-Identifier: GPL-3.0-only
 * MuseScore-Studio-CLA-applies
 *
 * MuseScore Studio
 * Music Composition & Notation
 *
 * Copyright (C) 2021 MuseScore Limited and others
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

#include <optional>

#include <QTime>

#include "global/containers.h"

#include "audio/common/audiotypes.h"
#include "engraving/types/types.h"

namespace mu::playback {
//! NOTE: total number of aux/group buses bootstrapped for a brand-new project (index 0 is
//! always the Reverb/Aux bus, the rest default to Group buses - see PlaybackController::setupTracks())
static constexpr muse::audio::aux_channel_idx_t DEFAULT_AUX_CHANNEL_NUM = 2;
//! NOTE: hard cap on the number of aux buses a project can have
static constexpr muse::audio::aux_channel_idx_t MAX_AUX_CHANNEL_NUM = 20;
static constexpr muse::audio::aux_channel_idx_t REVERB_CHANNEL_IDX = 0;

//! NOTE: an unused slot of a track's (or bus's) aux sends. A real send bypassed and pulled to 0%
//! is AuxSendParams{0, false}: -1 (outside the valid [0;1] gain range) tells "never assigned" apart
static constexpr muse::audio::AuxSendParams BLANK_AUX_SEND { -1.f, false };

inline bool isBlankAuxSend(const muse::audio::AuxSendParams& params)
{
    return params == BLANK_AUX_SEND;
}

enum class PlaybackCursorType {
    SMOOTH,
    STEPPED
};

enum class MixerSectionType {
    Unknown,
    Labels,
    Sound,
    Gain,
    Eq,
    AudioFX,
    AuxSends,
    Balance,
    Volume,
    Fader,
    MuteAndSolo,
    Title
};

static const std::map<MixerSectionType, std::string> MIXER_SECTION_TYPE_STR = {
    { MixerSectionType::Unknown, "unknown" },
    { MixerSectionType::Labels, "labels" },
    { MixerSectionType::Sound, "sound" },
    { MixerSectionType::Gain, "gain" },
    { MixerSectionType::Eq, "eq" },
    { MixerSectionType::AudioFX, "audio-fx" },
    { MixerSectionType::AuxSends, "aux-sends" },
    { MixerSectionType::Balance, "balance" },
    { MixerSectionType::Volume, "volume" },
    { MixerSectionType::Fader, "fader" },
    { MixerSectionType::MuteAndSolo, "mute-and-solo" },
    { MixerSectionType::Title, "title" }
};

static inline std::string str_conv(MixerSectionType type)
{
    return muse::value(MIXER_SECTION_TYPE_STR, type);
}

static inline MixerSectionType str_conv(const std::string& str, MixerSectionType def)
{
    return muse::key(MIXER_SECTION_TYPE_STR, str, def);
}

inline QList<MixerSectionType> allMixerSectionTypes()
{
    static const QList<MixerSectionType> sections {
        MixerSectionType::Labels,
        MixerSectionType::Sound,
        MixerSectionType::Gain,
        MixerSectionType::Eq,
        MixerSectionType::AudioFX,
        MixerSectionType::AuxSends,
        MixerSectionType::Balance,
        MixerSectionType::Volume,
        MixerSectionType::Fader,
        MixerSectionType::MuteAndSolo,
        MixerSectionType::Title
    };

    return sections;
}

//! NOTE: the panels listing channels whose kinds of channels can each be shown or hidden (View menu)
enum class ChannelListPanel {
    Mixer,
    TrackList
};

enum class ChannelCategory {
    Video,
    Metronome,
    Instruments,
    AuxFx,
    AuxGroup
};

static const std::map<ChannelListPanel, std::string> CHANNEL_LIST_PANEL_STR = {
    { ChannelListPanel::Mixer, "mixer" },
    { ChannelListPanel::TrackList, "track-list" }
};

static const std::map<ChannelCategory, std::string> CHANNEL_CATEGORY_STR = {
    { ChannelCategory::Video, "video" },
    { ChannelCategory::Metronome, "metronome" },
    { ChannelCategory::Instruments, "instruments" },
    { ChannelCategory::AuxFx, "aux-fx" },
    { ChannelCategory::AuxGroup, "aux-group" }
};

static inline std::string str_conv(ChannelListPanel panel)
{
    return muse::value(CHANNEL_LIST_PANEL_STR, panel);
}

static inline std::optional<ChannelListPanel> channelListPanelFromString(const std::string& str)
{
    for (const auto& pair : CHANNEL_LIST_PANEL_STR) {
        if (pair.second == str) {
            return pair.first;
        }
    }
    return std::nullopt;
}

static inline std::string str_conv(ChannelCategory category)
{
    return muse::value(CHANNEL_CATEGORY_STR, category);
}

static inline std::optional<ChannelCategory> channelCategoryFromString(const std::string& str)
{
    for (const auto& pair : CHANNEL_CATEGORY_STR) {
        if (pair.second == str) {
            return pair.first;
        }
    }
    return std::nullopt;
}

inline QList<ChannelCategory> allChannelCategories()
{
    static const QList<ChannelCategory> categories {
        ChannelCategory::Video,
        ChannelCategory::Metronome,
        ChannelCategory::Instruments,
        ChannelCategory::AuxFx,
        ChannelCategory::AuxGroup
    };

    return categories;
}

static const QTime ZERO_TIME(0, 0, 0, 0);

inline QTime timeFromSeconds(muse::audio::secs_t seconds)
{
    return ZERO_TIME.addMSecs(muse::secs_to_msecs(seconds));
}

inline muse::audio::secs_t timeToSeconds(const QTime& time)
{
    return muse::msecs_to_secs(ZERO_TIME.msecsTo(time));
}

enum class SoundProfileType {
    Undefined = -1,
    Basic,
    Muse,
    Custom
};

using SoundProfileName = muse::String;
using SoundProfileData = std::map<muse::mpe::PlaybackSetupData, muse::audio::AudioResourceMeta>;

struct SoundProfile {
    SoundProfileType type = SoundProfileType::Undefined;
    SoundProfileName name;

    SoundProfileData data;

    const muse::audio::AudioResourceMeta& findResource(const muse::mpe::PlaybackSetupData& key) const
    {
        auto search = data.find(key);
        if (search != data.cend()) {
            return search->second;
        }

        static muse::audio::AudioResourceMeta empty;
        return empty;
    }

    bool isEnabled() const
    {
        return !data.empty();
    }

    bool isValid() const
    {
        return type != SoundProfileType::Undefined
               && !name.isEmpty()
               && isEnabled();
    }
};

using SoundProfilesMap = std::map<SoundProfileName, SoundProfile>;

enum OnlineSoundsShowProgressBarMode {
    Always = 0,
    DuringPlayback,
    Never,
};

using LoopBoundaryType = mu::engraving::LoopBoundaryType;
}
