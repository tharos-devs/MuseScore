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
#include <optional>
#include <unordered_map>

#include "global/allocator.h"

#include "transaction/undoablecommand.h"

#include "engraving/articulationmap/articulationmaptypes.h"

namespace mu::engraving {
class ArticulationMapData;
class Score;

//! NOTE: sets or removes articulation maps and/or articulation marks in one undoable step
class EditArticulationMap : public UndoableCommand
{
    OBJECT_ALLOCATOR(engraving, EditArticulationMap)

public:
    using Changes = EditArticulationMapChanges;

    EditArticulationMap(Score* score, ArticulationMapData* data, Changes changes);

    UNDO_TYPE(CommandType::EditArticulationMap)
    UNDO_NAME("EditArticulationMap")
    UNDO_CHANGED_OBJECTS({ m_score })

    std::optional<ChangedRange> changedRange() const override;

private:
    void flip() override;

    Score* m_score = nullptr;
    ArticulationMapData* m_data = nullptr;
    Changes m_changes;
    std::optional<ChangedRange> m_changedRange;
};
}
