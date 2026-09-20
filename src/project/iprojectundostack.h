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

#include <functional>
#include <memory>

#include "async/notification.h"
#include "types/translatablestring.h"

namespace mu::project {
//! NOTE: a plain command-pattern undo/redo stack for project-level state that isn't part
//! of the score's own engraving::UndoStack (e.g. the Mixer's AudioOutputParams, which live
//! in IProjectAudioSettings and the live audio engine, not as EngravingItem properties). A
//! single generic command (redo/undo closures) is enough for every caller - no per-action
//! subclassing - because the closures themselves already capture whatever old/new state
//! each caller needs to restore.
class IProjectUndoStack
{
public:
    virtual ~IProjectUndoStack() = default;

    //! NOTE: assumes the caller has ALREADY performed the action once - this only records
    //! how to redo/undo it later, it does not invoke redo() itself. Truncates any existing
    //! redo tail, same as a normal undo stack.
    virtual void push(const muse::TranslatableString& actionName, std::function<void()> redo, std::function<void()> undo) = 0;

    virtual void undo() = 0;
    virtual void redo() = 0;

    virtual bool canUndo() const = 0;
    virtual bool canRedo() const = 0;

    virtual void clear() = 0;

    virtual muse::async::Notification stackChanged() const = 0;
};

using IProjectUndoStackPtr = std::shared_ptr<IProjectUndoStack>;
}
