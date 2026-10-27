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

#include <map>
#include <utility>
#include <vector>

#include "automationtypes.h"

namespace mu::engraving {
//! NOTE: the value a curve plays at a utick (the first point's value applies before it)
muse::real_t curveValueAt(const AutomationCurve& curve, utick_t utick);

//! NOTE: Ramer-Douglas-Peucker: drops the values a straight line between the kept ones already plays, within tolerance
std::map<utick_t, double> thinnedValues(const std::map<utick_t, double>& values, double tolerance);

//! NOTE: "touch": the curve with these values in place of its points from the first value's utick to the last one's
//! (e.g. a recorded take, a freehand stroke). It joins them with a step on each side, so that it keeps its own shape
//! outside of them - after them only if joinAfter, and there's more than one value (a single one is a step that holds)
AutomationCurve curveWithValues(const AutomationCurve& existing, const std::map<utick_t, double>& values, bool joinAfter);

//! NOTE: the edits that turn existing into result within these utick ranges (both ends included)
using UtickRange = std::pair<utick_t, utick_t>;
AutomationPointEdits editsWithinRanges(const AutomationCurve& existing, const AutomationCurve& result,
                                       const std::vector<UtickRange>& ranges);
}
