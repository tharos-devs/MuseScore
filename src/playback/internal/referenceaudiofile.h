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

#include "project/inotationproject.h"
#include "project/iprojectaudiosyncsettings.h"

#include "../iplaybackconfiguration.h"
#include "../iplaybackcontroller.h"

namespace mu::playback::audiosync {
//! NOTE Whether a file can be loaded as the reference audio, judged by its extension (drag and drop)
bool isSupportedAudioFile(const muse::io::path_t& path);

//! NOTE Asks for an audio file and makes it the reference audio; false if nothing was chosen
bool chooseReferenceAudio(muse::IInteractive* interactive, IPlaybackConfiguration* configuration, IPlaybackController* controller,
                          const project::INotationProjectPtr& project);

//! NOTE Makes path the reference audio, added to the recent files. Another file than the current one starts over
//! (see startOver()): the sync points of the previous one don't apply to it. A current file that can't be found is
//! the same recording located again: only its path changes
void setReferenceAudioPath(IPlaybackConfiguration* configuration, IPlaybackController* controller,
                           const project::INotationProjectPtr& project, const muse::io::path_t& path);

//! NOTE The reference audio's sync points removed (the score's start back at 00:00; audio sync, if on, starting over
//! from the score's tempo), its pitch, delay, loop and view reset, the playback speed too; newPath the reference
//! audio then (none if empty). Undoable: the previous audio back, then its sync points
void startOver(IPlaybackController* controller, const project::INotationProjectPtr& project, const muse::io::path_t& newPath);
}
