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

#include <vector>

#include "../iprojectundostack.h"

namespace mu::project {
class ProjectUndoStack : public IProjectUndoStack
{
public:
    ProjectUndoStack() = default;

    void push(const muse::TranslatableString& actionName, std::function<void()> redo, std::function<void()> undo) override;

    void undo() override;
    void redo() override;

    bool canUndo() const override;
    bool canRedo() const override;

    void clear() override;

    muse::async::Notification stackChanged() const override;

private:
    struct Command {
        muse::TranslatableString actionName;
        std::function<void()> redo;
        std::function<void()> undo;
    };

    std::vector<Command> m_commands;
    //! NOTE: index of the command that would be undone next - equal to m_commands.size()
    //! when there's nothing left to redo
    size_t m_position = 0;

    muse::async::Notification m_stackChanged;
};

using ProjectUndoStackPtr = std::shared_ptr<ProjectUndoStack>;
}
