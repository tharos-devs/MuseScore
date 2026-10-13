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
#pragma once

#include <optional>

#include "actions/iactionsdispatcher.h"
#include "actions/actiontypes.h"
#include "audio/common/audiotypes.h"
#include "playback/iplaybackcontroller.h"
#include "project/inotationproject.h"
#include "project/iprojectaudiosettings.h"

//! NOTE: the window of the VST instrument plugin of a score track - shared by the articulation map editor and the
//! circular toolbar
namespace mu::notation::instrumenteditor {
struct Target {
    muse::audio::TrackId trackId = -1;
    muse::audio::AudioResourceId resourceId;
};

//! NOTE: none unless the track plays a VST instrument with its own window
inline std::optional<Target> target(const project::INotationProjectPtr& project, const playback::IPlaybackController* playbackController,
                                    const engraving::InstrumentTrackId& instrumentTrackId)
{
    if (!project || !project->audioSettings() || !playbackController) {
        return std::nullopt;
    }

    const muse::audio::AudioInputParams& params = project->audioSettings()->trackInputParams(instrumentTrackId);
    if (params.type() != muse::audio::AudioSourceType::Vsti || !muse::audio::hasNativeEditorSupport(params.resourceMeta)) {
        return std::nullopt;
    }

    const playback::IPlaybackController::InstrumentTrackIdMap& trackIds = playbackController->instrumentTrackIdMap();
    auto it = trackIds.find(instrumentTrackId);
    if (it == trackIds.end()) {
        return std::nullopt;
    }

    return Target { it->second, params.resourceMeta.id };
}

//! NOTE: operation is "open" or "close"
inline void dispatch(muse::actions::IActionsDispatcher* dispatcher, const project::INotationProjectPtr& project, const Target& target,
                     const std::string& operation = "open")
{
    if (!dispatcher) {
        return;
    }

    muse::actions::ActionQuery query("action://vst/instrument_editor");
    query.addParam("trackId", muse::Val(target.trackId));
    query.addParam("resourceId", muse::Val(target.resourceId));
    query.addParam("operation", muse::Val(operation));
    dispatcher->dispatch(query);

    //! NOTE: same as the Mixer's (see MixerChannelItem::openEditor()): a plugin may change its state without
    //! reporting it once its window is open
    if (project && project->audioSettings()) {
        project->audioSettings()->markAsChanged();
    }
}
}
