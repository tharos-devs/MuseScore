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

#include "notationundoredocoordinator.h"

#include "notation/inotationinteraction.h"
#include "notation/inotationundostack.h"
#include "project/iprojectundostack.h"

using namespace mu::notation;
using namespace mu::project;

void NotationUndoRedoCoordinator::track(const INotationPtr& notation, const INotationProjectPtr& project)
{
    if (!notation || !project) {
        return;
    }

    stateFor(notation, project);
}

NotationUndoRedoCoordinator::DocumentState& NotationUndoRedoCoordinator::stateFor(const INotationPtr& notation,
                                                                                  const INotationProjectPtr& project)
{
    auto it = m_documents.find(project.get());
    //! NOTE: the address match alone isn't enough - a closed project's freed heap slot
    //! can be reused by a later, unrelated INotationProject, which would otherwise
    //! silently inherit its stale log/position. project.lock() compares the ACTUAL
    //! object, not just the address.
    if (it != m_documents.end() && it->second->project.lock() == project) {
        return *it->second;
    }

    auto state = std::make_unique<DocumentState>();
    DocumentState* statePtr = state.get();
    statePtr->project = project;

    if (INotationUndoStackPtr scoreStack = notation->undoStack()) {
        scoreStack->stackChanged().onNotify(statePtr, [statePtr]() {
            if (statePtr->guard) {
                return;
            }

            statePtr->log.resize(statePtr->position);
            statePtr->log.push_back(Source::Score);
            statePtr->position = statePtr->log.size();
        });
    }

    if (IProjectUndoStackPtr projectStack = project->undoStack()) {
        projectStack->stackChanged().onNotify(statePtr, [statePtr]() {
            if (statePtr->guard) {
                return;
            }

            statePtr->log.resize(statePtr->position);
            statePtr->log.push_back(Source::Project);
            statePtr->position = statePtr->log.size();
        });
    }

    //! NOTE: insert_or_assign (not emplace) - a stale entry at this same (recycled)
    //! address must be REPLACED, not kept; the old DocumentState's destruction
    //! auto-disconnects its own stackChanged() subscriptions via the Asyncable base.
    auto result = m_documents.insert_or_assign(project.get(), std::move(state));
    return *result.first->second;
}

void NotationUndoRedoCoordinator::undo(const INotationPtr& notation, const INotationProjectPtr& project)
{
    if (!notation || !project) {
        return;
    }

    DocumentState& state = stateFor(notation, project);
    if (state.position == 0) {
        return;
    }

    Source source = state.log[state.position - 1];

    state.guard = true;
    if (source == Source::Score) {
        if (INotationInteractionPtr interaction = notation->interaction()) {
            interaction->undo();
        }
    } else if (IProjectUndoStackPtr projectStack = project->undoStack()) {
        projectStack->undo();
    }
    state.guard = false;

    --state.position;
}

void NotationUndoRedoCoordinator::redo(const INotationPtr& notation, const INotationProjectPtr& project)
{
    if (!notation || !project) {
        return;
    }

    DocumentState& state = stateFor(notation, project);
    if (state.position >= state.log.size()) {
        return;
    }

    Source source = state.log[state.position];

    state.guard = true;
    if (source == Source::Score) {
        if (INotationInteractionPtr interaction = notation->interaction()) {
            interaction->redo();
        }
    } else if (IProjectUndoStackPtr projectStack = project->undoStack()) {
        projectStack->redo();
    }
    state.guard = false;

    ++state.position;
}

bool NotationUndoRedoCoordinator::canUndo(const INotationPtr& notation, const INotationProjectPtr& project)
{
    if (!notation || !project) {
        return false;
    }

    return stateFor(notation, project).position > 0;
}

bool NotationUndoRedoCoordinator::canRedo(const INotationPtr& notation, const INotationProjectPtr& project)
{
    if (!notation || !project) {
        return false;
    }

    const DocumentState& state = stateFor(notation, project);
    return state.position < state.log.size();
}
