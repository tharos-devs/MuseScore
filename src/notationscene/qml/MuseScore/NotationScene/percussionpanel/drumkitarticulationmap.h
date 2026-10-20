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

#include "notation/imasternotation.h"
#include "notation/inotationarticulationmaps.h"

#include "engraving/articulationmap/articulationmapdata.h"
#include "engraving/articulationmap/articulationmapparser.h"

//! NOTE: the articulation map of a drum kit's instrument, shared by the Percussion panel and its Edit pad dialog
namespace mu::notation::drumkit {
inline const engraving::ExpressionMap* articulationMap(const IMasterNotationPtr& masterNotation,
                                                       const engraving::InstrumentTrackId& trackId)
{
    const INotationArticulationMapsPtr articulationMaps = masterNotation ? masterNotation->articulationMaps() : nullptr;
    const ArticulationMapDataConstPtr data = articulationMaps ? articulationMaps->data() : nullptr;
    return data ? data->map(trackId) : nullptr;
}

//! NOTE: the octave of middle C (60) in note names: the map's (as its keyswitches are named), else 4 (as in MuseScore)
inline int noteNamesMiddleCOctave(const engraving::ExpressionMap* map)
{
    return map ? engraving::ArticulationMapParser::parse(map->sourceText).middleCOctave : 4;
}
}
