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

#include "global/io/path.h"
#include "interactive/iinteractive.h"

#include "project/iprojectaudiosyncsettings.h"

#include "../iplaybackconfiguration.h"

namespace mu::playback::audiosync {
//! NOTE Whether a file can be loaded as the reference audio, judged by its extension (drag and drop)
bool isSupportedAudioFile(const muse::io::path_t& path);

//! NOTE Asks for an audio file and makes it the reference audio; false if nothing was chosen
bool chooseReferenceAudio(muse::IInteractive* interactive, IPlaybackConfiguration* configuration,
                          const project::IProjectAudioSyncSettingsPtr& settings);

//! NOTE Makes path the reference audio (keeping the other settings when replacing one), added to the recent files
void setReferenceAudioPath(IPlaybackConfiguration* configuration, const project::IProjectAudioSyncSettingsPtr& settings,
                           const muse::io::path_t& path);
}
