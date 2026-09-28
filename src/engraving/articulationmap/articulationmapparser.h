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

#include <vector>

#include "articulationmaptypes.h"

namespace mu::engraving {
//! NOTE: parses an articulation map text file, one articulation per line:
//!
//!     ; comment
//!     @name     Violins 1          map name
//!     @middlec  C3                 octave naming: C3 = 60 (default, as in most samplers) or C4 = 60
//!     @offset   -15ms              default keyswitch lead, relative to the note
//!     *C0       Legato             '*' marks the default articulation
//!     D0        Spiccato  = staccato, staccatissimo       score articulations selecting it automatically
//!     cc32=10   Pizzicato = pizzicato  ks=-30ms delay=-60ms color=#D03B3B
//!     C0D1cc3=64 Legato > Fast     chained messages; '>' nests submenus
//!     C#0       Staccatissimo      without '=', a name matching a score articulation is its own alias
//!
//! Messages: note names (C0, F#-1, Bb2, optional velocity: C0v80), raw notes (n24),
//! controllers (cc32=10) and program changes (pc3)
class ArticulationMapParser
{
public:
    struct Error {
        size_t line = 0; // 1-based
        muse::String message;
    };

    struct Result {
        ExpressionMap map;
        std::vector<Error> errors;
    };

    static Result parse(const muse::String& text);

    //! NOTE: exposed for testing
    static bool parseMessages(const muse::String& code, int middleCOctave, std::vector<muse::mpe::MidiMessage>& messages);
};
}
