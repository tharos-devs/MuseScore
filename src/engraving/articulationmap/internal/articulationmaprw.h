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

#include <functional>

#include "global/types/bytearray.h"

namespace mu::engraving {
class ArticulationMapData;
class EID;

class ArticulationMapRW
{
public:
    static void read(ArticulationMapData& data, const muse::ByteArray& json);
    using MarkFilter = std::function<bool (const EID& chordId)>;

    //! NOTE: marks rejected by the filter (e.g. of deleted chords) are not written
    static muse::ByteArray write(const ArticulationMapData& data, const MarkFilter& keepMark = nullptr);
};
}
