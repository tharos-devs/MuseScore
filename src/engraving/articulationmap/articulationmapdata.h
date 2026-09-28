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

#include "articulationmaptypes_fwd.h"

#include <memory>
#include <optional>

#include "global/async/notification.h"

#include "articulationmaptypes.h"

namespace mu::engraving {
class Chord;
class MasterScore;

//! NOTE: per score: which articulation map each instrument uses, and the manual articulation marks
class ArticulationMapData
{
public:
    const ExpressionMapsByTrack& maps() const;
    const ExpressionMap* map(const InstrumentTrackId& trackId) const;
    void setMap(const InstrumentTrackId& trackId, const std::optional<ExpressionMap>& map);

    const ArticulationMarksByChord& marks() const;
    std::optional<ArticulationMark> mark(const EID& chordId) const;
    void setMark(const EID& chordId, const std::optional<ArticulationMark>& mark);

    bool isEmpty() const;

    //! NOTE: the chord a mark belongs to, if it is still part of the score - a deleted chord is kept alive
    //! (and its EID registered) by the undo stack, but its mark must not affect anything anymore
    static const Chord* chordOfMark(const MasterScore* score, const EID& chordId);

    muse::async::Notification changed() const;

private:
    ExpressionMapsByTrack m_maps;
    ArticulationMarksByChord m_marks;
    muse::async::Notification m_changed;
};
}
