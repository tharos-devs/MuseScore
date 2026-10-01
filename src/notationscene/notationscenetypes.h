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
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program. If not, see <https://www.gnu.org/licenses/>.
 */

#pragma once

#include <string>
#include <vector>

namespace mu::notation {
enum class PercussionPanelAutoShowMode {
    UNPITCHED_STAFF,
    UNPITCHED_STAFF_NOTE_INPUT,
    NEVER,
};

//! NOTE: stable (untranslated) ids of the Timeline's meta rows, used as their
//! persisted visibility settings keys -- the rows themselves are matched by their
//! translated label, which can't be stored.
inline const std::string TIMELINE_ROW_TEMPO("tempo");
inline const std::string TIMELINE_ROW_TIME_SIGNATURE("timeSignature");
inline const std::string TIMELINE_ROW_TIMECODE("timecode");
inline const std::string TIMELINE_ROW_HIT_POINTS("hitPoints");
inline const std::string TIMELINE_ROW_REHEARSAL_MARK("rehearsalMark");
inline const std::string TIMELINE_ROW_KEY_SIGNATURE("keySignature");
inline const std::string TIMELINE_ROW_BARLINES("barlines");
inline const std::string TIMELINE_ROW_JUMPS_AND_MARKERS("jumpsAndMarkers");
inline const std::string TIMELINE_ROW_MEASURES("measures");

inline const std::vector<std::string> TIMELINE_ROW_IDS {
    TIMELINE_ROW_TEMPO,
    TIMELINE_ROW_TIME_SIGNATURE,
    TIMELINE_ROW_TIMECODE,
    TIMELINE_ROW_HIT_POINTS,
    TIMELINE_ROW_REHEARSAL_MARK,
    TIMELINE_ROW_KEY_SIGNATURE,
    TIMELINE_ROW_BARLINES,
    TIMELINE_ROW_JUMPS_AND_MARKERS,
    TIMELINE_ROW_MEASURES,
};
}
