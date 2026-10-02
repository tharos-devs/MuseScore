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

#include "editarticulationmap.h"

#include "engraving/articulationmap/articulationmapdata.h"

#include "engraving/dom/chord.h"
#include "engraving/dom/masterscore.h"
#include "engraving/dom/measure.h"
#include "engraving/dom/part.h"
#include "engraving/infrastructure/eidregister.h"

#include "log.h"

using namespace mu::engraving;

EditArticulationMap::EditArticulationMap(Score* score, ArticulationMapData* data, Changes changes)
    : m_score(score), m_data(data), m_changes(std::move(changes))
{
    assert(score && data && !m_changes.empty());
}

std::optional<ChangedRange> EditArticulationMap::changedRange() const
{
    return m_changedRange;
}

//! NOTE: an articulation change can affect every following chord of the staff (latched marks, default articulation),
//! so the whole duration of the affected staves is invalidated
void EditArticulationMap::flip()
{
    IF_ASSERT_FAILED(m_score && m_data) {
        return;
    }

    staff_idx_t staffFrom = muse::nidx;
    staff_idx_t staffTo = muse::nidx;
    auto includeStaves = [&](staff_idx_t from, staff_idx_t to) {
        staffFrom = staffFrom == muse::nidx ? from : std::min(staffFrom, from);
        staffTo = staffTo == muse::nidx ? to : std::max(staffTo, to);
    };

    Changes previous;

    for (const auto& [trackId, map] : m_changes.maps) {
        const ExpressionMap* current = m_data->map(trackId);
        previous.maps.emplace(trackId, current ? std::optional<ExpressionMap>(*current) : std::nullopt);
        m_data->setMap(trackId, map);

        if (const Part* part = m_score->partById(trackId.partId)) {
            const TrackRange trackRange = part->trackRange();
            includeStaves(trackRange.startTrack / VOICES, trackRange.endTrack / VOICES - 1);
        }
    }

    const EIDRegister* eidRegister = m_score->masterScore()->eidRegister();
    for (const auto& [chordId, mark] : m_changes.marks) {
        previous.marks.emplace(chordId, m_data->mark(chordId));
        m_data->setMark(chordId, mark);

        const EngravingObject* obj = eidRegister->itemFromEID(chordId);
        if (obj && obj->isChordRest()) {
            const staff_idx_t staffIdx = toChordRest(obj)->staffIdx();
            includeStaves(staffIdx, staffIdx);
        }
    }

    m_changes = std::move(previous);

    const Measure* lastMeasure = m_score->lastMeasure();
    if (staffFrom != muse::nidx && lastMeasure) {
        m_changedRange = ChangedRange { Fraction(0, 1), lastMeasure->endTick(), staffFrom, staffTo };
    } else {
        m_changedRange = std::nullopt;
    }
}
