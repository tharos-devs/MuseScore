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

#include <memory>
#include <unordered_map>
#include <vector>

#include "async/asyncable.h"

#include "notation/inotation.h"
#include "project/inotationproject.h"

namespace mu::notation {
//! NOTE: the score's own undo/redo (engraving::UndoStack, reached via
//! INotation::undoStack()/interaction()) and the Mixer's project-level undo/redo
//! (project::IProjectUndoStack, reached via INotationProject::undoStack()) are two
//! independent stacks with no shared clock. This class merges them into one
//! chronological Ctrl+Z/Ctrl+Shift+Z history per open document, without touching
//! engraving::UndoStack itself: it keeps a small parallel log of WHICH stack each
//! push landed on (not the commands themselves, just source tags), built by
//! observing both stacks' stackChanged() notifications, and is the sole caller of
//! undo()/redo() on either real stack - a reentrancy guard stops its own calls from
//! being misread as fresh pushes.
class NotationUndoRedoCoordinator
{
public:
    //! NOTE: must be called as soon as a document becomes current (e.g. on
    //! globalContext()->currentNotationChanged()), NOT lazily from undo()/redo() -
    //! the log below is only ever appended to by observing stackChanged(), so any
    //! push that happens before this document's subscriptions exist is invisible to
    //! it forever. Idempotent - safe to call again for the same still-open document.
    void track(const INotationPtr& notation, const project::INotationProjectPtr& project);

    void undo(const INotationPtr& notation, const project::INotationProjectPtr& project);
    void redo(const INotationPtr& notation, const project::INotationProjectPtr& project);

    //! NOTE: authoritative for the Edit menu's Undo/Redo enabled state - deliberately
    //! NOT a plain OR of the two real stacks' own canUndo()/canRedo() (which is what a
    //! naive caller would reach for): after this coordinator undoes ONE of the two
    //! stacks, that stack's OWN canRedo() stays true (its other entries are untouched),
    //! even though the NEXT thing THIS coordinator would actually redo may be a push
    //! that later landed on the OTHER stack. Querying the real stacks directly would
    //! report Redo as enabled while pressing Ctrl+Shift+Z silently does nothing.
    bool canUndo(const INotationPtr& notation, const project::INotationProjectPtr& project);
    bool canRedo(const INotationPtr& notation, const project::INotationProjectPtr& project);

private:
    enum class Source {
        Score,
        Project
    };

    struct DocumentState : public muse::async::Asyncable {
        //! NOTE: identifies which INotationProject this state belongs to - compared by
        //! stateFor() against the live project on every lookup (not just used as the
        //! map key) so a closed project's freed heap address being reused by an
        //! unrelated, later-opened one can never inherit its stale log/position.
        std::weak_ptr<project::INotationProject> project;
        //! NOTE: log[0..position) is the undo-able history in chronological order,
        //! log[position..end) is the redo-able tail - same shape as ProjectUndoStack's
        //! own m_commands/m_position.
        std::vector<Source> log;
        size_t position = 0;
        //! NOTE: true only while this coordinator itself is calling undo()/redo() on
        //! one of the two real stacks - stops that call's own stackChanged() firing
        //! from being appended to the log as if it were a fresh edit.
        bool guard = false;
    };

    DocumentState& stateFor(const INotationPtr& notation, const project::INotationProjectPtr& project);

    std::unordered_map<const project::INotationProject*, std::unique_ptr<DocumentState> > m_documents;
};
}
