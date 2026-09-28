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

#include "articulationmapdata.h"

#include "engraving/dom/chord.h"
#include "engraving/dom/masterscore.h"
#include "engraving/dom/segment.h"
#include "engraving/infrastructure/eidregister.h"

using namespace mu::engraving;

const ExpressionMapsByTrack& ArticulationMapData::maps() const
{
    return m_maps;
}

const ExpressionMap* ArticulationMapData::map(const InstrumentTrackId& trackId) const
{
    auto it = m_maps.find(trackId);
    return it != m_maps.cend() ? &it->second : nullptr;
}

void ArticulationMapData::setMap(const InstrumentTrackId& trackId, const std::optional<ExpressionMap>& map)
{
    if (map) {
        m_maps.insert_or_assign(trackId, *map);
    } else {
        m_maps.erase(trackId);
    }

    m_changed.notify();
}

const ArticulationMarksByChord& ArticulationMapData::marks() const
{
    return m_marks;
}

std::optional<ArticulationMark> ArticulationMapData::mark(const EID& chordId) const
{
    auto it = m_marks.find(chordId);
    if (it == m_marks.cend()) {
        return std::nullopt;
    }

    return it->second;
}

void ArticulationMapData::setMark(const EID& chordId, const std::optional<ArticulationMark>& mark)
{
    if (mark) {
        m_marks.insert_or_assign(chordId, *mark);
    } else {
        m_marks.erase(chordId);
    }

    m_changed.notify();
}

bool ArticulationMapData::isEmpty() const
{
    return m_maps.empty() && m_marks.empty();
}

muse::async::Notification ArticulationMapData::changed() const
{
    return m_changed;
}

const Chord* ArticulationMapData::chordOfMark(const MasterScore* score, const EID& chordId)
{
    if (!score || !chordId.isValid()) {
        return nullptr;
    }

    const EngravingObject* obj = score->eidRegister()->itemFromEID(chordId);
    if (!obj || !obj->isChord()) {
        return nullptr;
    }

    const Chord* chord = toChord(obj);
    const Segment* segment = chord->segment();
    if (!segment || segment->element(chord->track()) != chord || !segment->measure()) {
        return nullptr;
    }

    return chord;
}
