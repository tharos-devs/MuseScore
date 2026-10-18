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

namespace mu::engraving {
class Score;
}

namespace mu::notation {
// Where the score's systems and their staves sit on the canvas. A relayout outside of any transaction (e.g. the
// thumbnail on save, in Continuous view) recreates them without ScoreChanges, only notationChanged(): comparing
// this tells an overlay holding cached System pointers and positions that it has to be rebuilt
class LayoutSignature
{
public:
    static LayoutSignature of(const mu::engraving::Score* score);

    // Takes the score's current signature: true when it differs from the one held
    bool refresh(const mu::engraving::Score* score);

    bool operator==(const LayoutSignature&) const = default;

private:
    std::vector<const void*> m_systems;
    std::vector<double> m_geometry;
};
}
