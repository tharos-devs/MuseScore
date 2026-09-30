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
#ifndef MU_IMPORTEXPORT_VIDEOEXPORTTYPES_H
#define MU_IMPORTEXPORT_VIDEOEXPORTTYPES_H

#include <string>

namespace mu::iex::videoexport {
enum ViewMode {
    PageFull,
    Flexible,
};

enum PianoPosition {
    Bottom,
    Top
};

//! NOTE What the MP4 export's picture is: the score scrolling (with the score's audio), or the video
//! attached to the project (with the score's audio and the video's own audio, as mixed in the Mixer)
enum class VideoSource {
    Score,
    AttachedVideo
};

//! NOTE The video to export for VideoSource::AttachedVideo (video position = score position + offset)
struct AttachedVideo {
    std::string path; // UTF-8
    int offsetMs = 0;
};
}

#endif // MU_IMPORTEXPORT_VIDEOEXPORTTYPES_H
