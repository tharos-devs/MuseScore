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

void ChannelColorChange::apply(const IProjectAudioSettingsPtr& settings, const IProjectUndoStackPtr& undoStack,
                               const std::vector<Target>& targets, const QColor& color, const muse::TranslatableString& actionName)
{
    if (!settings) {
        return;
    }

    std::vector<std::pair<Target, QColor> > oldColors;
    for (const Target& target : targets) {
        const std::optional<QColor> oldColor = targetColor(settings, target);
        if (oldColor && *oldColor != color) {
            oldColors.push_back({ target, *oldColor });
            setTargetColor(settings, target, color);
        }
    }

    if (oldColors.empty() || !undoStack) {
        return;
    }

    undoStack->push(actionName, [settings, oldColors, color]() {
        for (const auto& [target, oldColor] : oldColors) {
            setTargetColor(settings, target, color);
        }
    }, [settings, oldColors]() {
        for (const auto& [target, oldColor] : oldColors) {
            setTargetColor(settings, target, oldColor);
        }
    });
}
