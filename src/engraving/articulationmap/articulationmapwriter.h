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

#include "articulationmapparser.h"

namespace mu::engraving {
//! NOTE: writes an articulation map back to the text format read by ArticulationMapParser,
//! so that parse(write(x)) gives x again (comments of a hand-written file are not kept)
class ArticulationMapWriter
{
public:
    static muse::String write(const ArticulationMapParser::Result& file);

    //! NOTE: e.g. "C#0v80", "cc32=10", "pc3" - several messages are chained
    static muse::String messagesCode(const std::vector<muse::mpe::MidiMessage>& messages, int middleCOctave);
    static muse::String noteName(int pitch, int middleCOctave);
};
}
