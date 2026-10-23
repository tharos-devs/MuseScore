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

//! NOTE: where the bus with this type and display number is now: a bus removed then brought back by an undo gets
//! another index, and another bus can get its old index; a display number is never given to another bus of the same
//! type (see auxDisplayNumber())
static std::optional<muse::audio::aux_channel_idx_t> auxIndexOf(const IProjectAudioSettingsPtr& settings,
                                                                const IPlaybackController* playbackController,
                                                                bool isGroupBus, muse::audio::aux_channel_idx_t displayNumber)
{
    if (!playbackController) {
        return std::nullopt;
    }

    for (const auto& [index, trackId] : playbackController->auxTrackIdMap()) {
        if (settings->isAuxBusGroup(index) == isGroupBus && settings->auxDisplayNumber(index) == displayNumber) {
            return index;
        }
    }

    return std::nullopt;
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
        bool isGroupBus = false; //! NOTE: for an aux bus
        muse::audio::aux_channel_idx_t auxDisplayNumber = 0;
        QColor color;
    };

    std::vector<OldColor> oldColors;
    for (const Target& target : targets) {
        const std::optional<QColor> oldColor = targetColor(settings, target);
        if (oldColor && *oldColor != color) {
            const bool isGroupBus = target.auxIndex && settings->isAuxBusGroup(*target.auxIndex);
            const muse::audio::aux_channel_idx_t displayNumber = target.auxIndex ? settings->auxDisplayNumber(*target.auxIndex) : 0;
            oldColors.push_back({ target, isGroupBus, displayNumber, *oldColor });
            setTargetColor(settings, target, color);
        }
    }

    if (oldColors.empty() || !undoStack) {
        return;
    }

    //! NOTE: an aux bus is followed by its type and display number; a track by its id, never given to another one
    const auto setColors = [settings, playbackController, oldColors](bool undo, const QColor& newColor) {
        for (const OldColor& oldColor : oldColors) {
            Target target = oldColor.target;
            //! NOTE: 0: no display number (a bus from an older version), followed by its index
            if (target.auxIndex && oldColor.auxDisplayNumber != 0) {
                target.auxIndex = auxIndexOf(settings, playbackController.get(), oldColor.isGroupBus, oldColor.auxDisplayNumber);
                if (!target.auxIndex) {
                    continue;
                }
            }

            setTargetColor(settings, target, undo ? oldColor.color : newColor);
        }
    };

    undoStack->push(actionName, [setColors, color]() {
        setColors(false, color);
    }, [setColors, color]() {
        setColors(true, color);
    });
}
