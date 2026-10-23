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

#include "global/allocator.h"

#include "transaction/undoablecommand.h"

#include "engraving/dom/score.h"
#include "engraving/automation/automationtypes.h"

namespace mu::engraving {
class ScoreAutomationController;

class EditAutomationPoints : public UndoableCommand
{
    OBJECT_ALLOCATOR(engraving, EditAutomationPoints)

public:
    EditAutomationPoints(Score* score, ScoreAutomationController* controller, const AutomationCurveKey& key,
                         const AutomationPointEdits& edits);

    UNDO_TYPE(CommandType::EditAutomationPoints)
    UNDO_NAME("EditAutomationPoints")
    UNDO_CHANGED_OBJECTS({ m_score })

    std::optional<ChangedRange> changedRange() const override;

private:
    void flip() override;

    Score* m_score = nullptr;
    ScoreAutomationController* m_controller = nullptr;
    AutomationCurveKey m_key;
    std::map<utick_t, std::optional<AutomationPoint> > m_pointStates; // tick -> point to write next, or nullopt to erase
    std::optional<ChangedRange> m_changedRange;
};

//! NOTE: writes or removes whole curves (e.g. the curves of a removed part)
class EditAutomationCurves : public UndoableCommand
{
    OBJECT_ALLOCATOR(engraving, EditAutomationCurves)

public:
    EditAutomationCurves(Score* score, ScoreAutomationController* controller, AutomationCurveStates curves);

    UNDO_TYPE(CommandType::EditAutomationCurves)
    UNDO_NAME("EditAutomationCurves")
    UNDO_CHANGED_OBJECTS({ m_score })

private:
    void flip() override;

    Score* m_score = nullptr;
    ScoreAutomationController* m_controller = nullptr;
    AutomationCurveStates m_curves;
};

//! NOTE: turns audio sync on/off (see AutomationData::isAudioSyncEnabled): the whole tempo changes
class EditAudioSyncEnabled : public UndoableCommand
{
    OBJECT_ALLOCATOR(engraving, EditAudioSyncEnabled)

public:
    EditAudioSyncEnabled(Score* score, ScoreAutomationController* controller, bool enabled);

    UNDO_TYPE(CommandType::EditAudioSyncEnabled)
    UNDO_NAME("EditAudioSyncEnabled")
    UNDO_CHANGED_OBJECTS({ m_score })

    std::optional<ChangedRange> changedRange() const override;

private:
    void flip() override;

    Score* m_score = nullptr;
    ScoreAutomationController* m_controller = nullptr;
    bool m_enabled = false;
};

//! NOTE: replaces the tempo points not in use (see AutomationData::inactiveTempoCurve): the audio sync points while
//! audio sync is off
class EditInactiveTempoCurve : public UndoableCommand
{
    OBJECT_ALLOCATOR(engraving, EditInactiveTempoCurve)

public:
    EditInactiveTempoCurve(Score* score, ScoreAutomationController* controller, AutomationCurve curve);

    UNDO_TYPE(CommandType::EditInactiveTempoCurve)
    UNDO_NAME("EditInactiveTempoCurve")
    UNDO_CHANGED_OBJECTS({ m_score })

    std::optional<ChangedRange> changedRange() const override;

private:
    void flip() override;

    Score* m_score = nullptr;
    ScoreAutomationController* m_controller = nullptr;
    AutomationCurve m_curve;
};

//! NOTE: replaces the score's custom MIDI CC list (see AutomationData::customMidiCcs)
class EditCustomMidiCcs : public UndoableCommand
{
    OBJECT_ALLOCATOR(engraving, EditCustomMidiCcs)

public:
    EditCustomMidiCcs(Score* score, ScoreAutomationController* controller, std::vector<uint8_t> controllers);

    UNDO_TYPE(CommandType::EditCustomMidiCcs)
    UNDO_NAME("EditCustomMidiCcs")
    // Nothing to re-layout or re-render: it's only the list the MIDI CC menus show
    UNDO_CHANGED_OBJECTS({})

private:
    void flip() override;

    Score* m_score = nullptr;
    ScoreAutomationController* m_controller = nullptr;
    std::vector<uint8_t> m_controllers;
};
}
