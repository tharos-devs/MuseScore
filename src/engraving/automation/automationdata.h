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

#include "global/async/channel.h"

#include "automationtypes.h"

#include <memory>
#include <string>
#include <vector>

namespace mu::engraving {
class Score;

class AutomationData
{
public:
    //! NOTE: whether the item a point was generated from (e.g. a tempo marking) is still in the score - a deleted
    //! item stays registered (and parented) while the undo stack keeps it, so the register alone can't tell
    static bool isLinkedItemInScore(const Score* score, const EID& itemId);
    //! NOTE: a point the score drives (generated, or from an item still in the score): not edited/removed by hand
    static bool isScoreDrivenPoint(const Score* score, const AutomationPoint& point);

    const AutomationCurveMap& curves() const;
    const AutomationCurve& curve(const AutomationCurveKey& key) const;

    bool isEmpty() const;

    //! NOTE: full replacement; any existing key absent from the argument is removed
    void setCurves(const AutomationCurveMap& curves);

    //! NOTE: replaces only the given curves, keeping all others; keys absent from the argument are untouched
    void replaceCurves(const AutomationCurveMap& curves);

    //! NOTE: writes, moves, or erases points in one batch, per each edit's SetPoint/MovePoint/ErasePoint
    void editPoints(const AutomationCurveKey& key, const AutomationPointEdits& edits);

    muse::async::Channel<AutomationChanges> changed() const;

    //! NOTE: MIDI CC numbers the user picked for this score (besides the predefined ones), in the order they were added
    const std::vector<uint8_t>& customMidiCcs() const;
    void setCustomMidiCcs(const std::vector<uint8_t>& controllers);

    std::string dump() const;

private:
    void notifyChanged(const AutomationChanges& changes);

    AutomationCurveMap m_curveMap;
    std::vector<uint8_t> m_customMidiCcs;
    muse::async::Channel<AutomationChanges> m_changesChannel;
};

using AutomationDataPtr = std::shared_ptr<AutomationData>;
using AutomationDataConstPtr = std::shared_ptr<const AutomationData>;
}
