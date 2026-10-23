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

#include "channelcolorchange.h"

using namespace mu::playback;
using namespace mu::project;

static std::optional<QColor> targetColor(const IProjectAudioSettingsPtr& settings, const ChannelColorChange::Target& target)
{
    if (target.auxIndex) {
        return settings->auxOutputParams(*target.auxIndex).color;
    }

    if (!settings->trackHasExistingOutputParams(target.instrumentTrackId)) {
        return std::nullopt;
    }

    return settings->trackOutputParams(target.instrumentTrackId).color;
}

static void setTargetColor(const IProjectAudioSettingsPtr& settings, const ChannelColorChange::Target& target, const QColor& color)
{
    if (target.auxIndex) {
        AudioOutputParams params = settings->auxOutputParams(*target.auxIndex);
        params.color = color;
        settings->setAuxOutputParams(*target.auxIndex, params);
        return;
    }

    if (!settings->trackHasExistingOutputParams(target.instrumentTrackId)) {
        return;
    }

    AudioOutputParams params = settings->trackOutputParams(target.instrumentTrackId);
    params.color = color;
    settings->setTrackOutputParams(target.instrumentTrackId, params);
}

static std::optional<muse::audio::TrackId> engineTrackId(const IPlaybackController* playbackController,
                                                         const ChannelColorChange::Target& target)
{
    if (!playbackController) {
        return std::nullopt;
    }

    if (target.auxIndex) {
        const IPlaybackController::AuxTrackIdMap& auxTrackIds = playbackController->auxTrackIdMap();
        const auto it = auxTrackIds.find(*target.auxIndex);
        return it != auxTrackIds.end() ? std::optional(it->second) : std::nullopt;
    }

    const IPlaybackController::InstrumentTrackIdMap& trackIds = playbackController->instrumentTrackIdMap();
    const auto it = trackIds.find(target.instrumentTrackId);
    return it != trackIds.end() ? std::optional(it->second) : std::nullopt;
}

void ChannelColorChange::apply(const IProjectAudioSettingsPtr& settings, const IProjectUndoStackPtr& undoStack,
                               const std::shared_ptr<IPlaybackController>& playbackController, const std::vector<Target>& targets,
                               const QColor& color, const muse::TranslatableString& actionName)
{
    if (!settings) {
        return;
    }

    struct OldColor {
        Target target;
        std::optional<muse::audio::TrackId> engineTrackId;
        QColor color;
    };

    std::vector<OldColor> oldColors;
    for (const Target& target : targets) {
        const std::optional<QColor> oldColor = targetColor(settings, target);
        if (oldColor && *oldColor != color) {
            oldColors.push_back({ target, engineTrackId(playbackController.get(), target), *oldColor });
            setTargetColor(settings, target, color);
        }
    }

    if (oldColors.empty() || !undoStack) {
        return;
    }

    //! NOTE: not the same track any more (removed, then maybe another one in its place): left as is
    const auto isSameTrack = [playbackController](const OldColor& oldColor) {
        return !oldColor.engineTrackId || engineTrackId(playbackController.get(), oldColor.target) == oldColor.engineTrackId;
    };

    undoStack->push(actionName, [settings, oldColors, color, isSameTrack]() {
        for (const OldColor& oldColor : oldColors) {
            if (isSameTrack(oldColor)) {
                setTargetColor(settings, oldColor.target, color);
            }
        }
    }, [settings, oldColors, isSameTrack]() {
        for (const OldColor& oldColor : oldColors) {
            if (isSameTrack(oldColor)) {
                setTargetColor(settings, oldColor.target, oldColor.color);
            }
        }
    });
}
