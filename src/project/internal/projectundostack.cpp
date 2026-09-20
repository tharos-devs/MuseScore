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

#include "projectundostack.h"

#include "log.h"

using namespace mu::project;

void ProjectUndoStack::push(const muse::TranslatableString& actionName, std::function<void()> redo, std::function<void()> undo)
{
    IF_ASSERT_FAILED(redo && undo) {
        return;
    }

    m_commands.resize(m_position);
    m_commands.push_back(Command { actionName, std::move(redo), std::move(undo) });
    m_position = m_commands.size();

    m_stackChanged.notify();
}

void ProjectUndoStack::undo()
{
    if (!canUndo()) {
        return;
    }

    --m_position;
    m_commands[m_position].undo();

    m_stackChanged.notify();
}

void ProjectUndoStack::redo()
{
    if (!canRedo()) {
        return;
    }

    m_commands[m_position].redo();
    ++m_position;

    m_stackChanged.notify();
}

bool ProjectUndoStack::canUndo() const
{
    return m_position > 0;
}

bool ProjectUndoStack::canRedo() const
{
    return m_position < m_commands.size();
}

void ProjectUndoStack::clear()
{
    if (m_commands.empty()) {
        return;
    }

    m_commands.clear();
    m_position = 0;

    m_stackChanged.notify();
}

muse::async::Notification ProjectUndoStack::stackChanged() const
{
    return m_stackChanged;
}
