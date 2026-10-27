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

#include "automationvalues.h"

#include <algorithm>
#include <cmath>
#include <iterator>

using namespace mu::engraving;

static AutomationPoint valuePoint(double value)
{
    AutomationPoint point;
    point.value.outValue = value;
    point.value.inValue = AutomationPoint::ExplicitArrival { point.value.outValue, AutomationPoint::Ease::none() };
    point.generated = false;
    return point;
}

muse::real_t mu::engraving::curveValueAt(const AutomationCurve& curve, utick_t utick)
{
    if (curve.empty()) {
        return 0.;
    }

    auto next = curve.upper_bound(utick);
    if (next == curve.cbegin()) {
        return next->second.value.outValue;
    }

    const auto it = std::prev(next);
    if (next == curve.cend() || it->first == utick) {
        return it->second.value.outValue;
    }

    const muse::real_t t = muse::real_t(utick - it->first) / muse::real_t(next->first - it->first);
    return muse::mpe::evaluateAt(next->second.value, it->second.value.outValue, t);
}

namespace {
using ValuePoint = std::pair<utick_t, double>;

void thinPoints(const std::vector<ValuePoint>& points, size_t from, size_t to, double tolerance, std::vector<bool>& keep)
{
    if (to <= from + 1) {
        return;
    }

    const ValuePoint& a = points[from];
    const ValuePoint& b = points[to];

    size_t farthest = from;
    double maxDistance = 0.;

    for (size_t i = from + 1; i < to; ++i) {
        const double t = b.first == a.first ? 0. : double(points[i].first - a.first) / double(b.first - a.first);
        const double distance = std::abs(points[i].second - (a.second + t * (b.second - a.second)));
        if (distance > maxDistance) {
            maxDistance = distance;
            farthest = i;
        }
    }

    if (maxDistance <= tolerance) {
        return;
    }

    keep[farthest] = true;
    thinPoints(points, from, farthest, tolerance, keep);
    thinPoints(points, farthest, to, tolerance, keep);
}
}

std::map<utick_t, double> mu::engraving::thinnedValues(const std::map<utick_t, double>& values, double tolerance)
{
    if (values.size() <= 2) {
        return values;
    }

    const std::vector<ValuePoint> points(values.cbegin(), values.cend());
    std::vector<bool> keep(points.size(), false);
    keep.front() = true;
    keep.back() = true;
    thinPoints(points, 0, points.size() - 1, tolerance, keep);

    std::map<utick_t, double> result;
    for (size_t i = 0; i < points.size(); ++i) {
        if (keep[i]) {
            result.insert(points[i]);
        }
    }

    return result;
}

AutomationCurve mu::engraving::curveWithValues(const AutomationCurve& existing, const std::map<utick_t, double>& values, bool joinAfter)
{
    AutomationCurve result = existing;
    if (values.empty()) {
        return result;
    }

    const utick_t fromUtick = values.cbegin()->first;
    const utick_t toUtick = values.crbegin()->first;

    const bool hasCurve = !result.empty();
    const double valueBefore = curveValueAt(result, fromUtick);
    const double valueAfter = curveValueAt(result, toUtick);

    result.erase(result.lower_bound(fromUtick), result.upper_bound(toUtick));

    for (auto it = values.cbegin(); it != values.cend(); ++it) {
        AutomationPoint point = valuePoint(it->second);

        if (hasCurve && it == values.cbegin()) {
            point.value.inValue = AutomationPoint::ExplicitArrival { valueBefore, AutomationPoint::Ease::none() };
        }

        if (hasCurve && joinAfter && std::next(it) == values.cend() && values.size() > 1) {
            point.value.outValue = valueAfter;
        }

        result.insert({ it->first, point });
    }

    return result;
}

AutomationPointEdits mu::engraving::editsWithinRanges(const AutomationCurve& existing, const AutomationCurve& result,
                                                     const std::vector<std::pair<utick_t, utick_t> >& ranges)
{
    const auto isInRanges = [&ranges](utick_t utick) {
        return std::any_of(ranges.cbegin(), ranges.cend(), [utick](const auto& range) {
            return utick >= range.first && utick <= range.second;
        });
    };

    // Whatever the curve had there goes, the result's points come
    AutomationPointEdits edits;
    for (const auto& [utick, point] : existing) {
        if (isInRanges(utick) && result.find(utick) == result.cend()) {
            edits.push_back({ utick, AutomationPointEdit::ErasePoint {} });
        }
    }
    for (const auto& [utick, point] : result) {
        if (isInRanges(utick)) {
            edits.push_back({ utick, AutomationPointEdit::SetPoint { point } });
        }
    }

    return edits;
}
