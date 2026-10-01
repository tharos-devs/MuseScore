/*
 * SPDX-License-Identifier: GPL-3.0-only
 * MuseScore-Studio-CLA-applies
 *
 * MuseScore Studio
 * Music Composition & Notation
 *
 * Copyright (C) 2021 MuseScore Limited and others
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

#include "notationtoolbarmodel.h"

#include "uicomponents/qml/Muse/UiComponents/toolbaritem.h"

#include "notationcommands.h"
#include "appshell/appshellcommands.h"

using namespace muse;
using namespace mu::notation;
using namespace muse::uicomponents;
using namespace muse::actions;

static constexpr int ARTICULATION_MAP_ITEM_TYPE = ToolBarItemType::USER_TYPE + 1;

void NotationToolBarModel::load()
{
    if (m_loaded) {
        return;
    }

    AbstractToolBarModel::load();

    std::vector<rcommand::Command> commands = {
        OPEN_PARTS_COMMAND,
        appshell::DOCK_TOGGLE_MIXER_COMMAND,
        appshell::DOCK_TOGGLE_VIDEO_PANEL_COMMAND,
        TOGGLE_AUTOMATION_COMMAND,
        TOGGLE_NOTE_OFFSET_EDITOR_COMMAND,
        TOGGLE_NOTE_VELOCITY_EDITOR_COMMAND,
        TOGGLE_ARTICULATION_MAP_EDITOR_COMMAND
    };

    ToolBarItemList items;
    for (const rcommand::Command& command : commands) {
        ToolBarItem* item = makeItem(command);
        if (!item) {
            continue;
        }

        item->setShowTitle(!isCompactMode());
        item->setIsTitleBold(true);

        // Rendered as a SplitButton instead of a plain button (see NotationToolBar.qml's
        // sourceComponentCallback) so it can offer a dropdown to pick the automation type
        // directly, alongside its usual toggle behavior. USER_TYPE keeps it a normal tracked
        // item (state/compact-mode updates apply to it exactly like every other item) while
        // letting the view pick a different delegate component for it.
        if (command == TOGGLE_AUTOMATION_COMMAND) {
            item->setType(ToolBarItemType::USER_TYPE);
        }

        // Its glyph is drawn larger than the others by the icon font - a delegate of its own
        // (see NotationToolBar.qml) scales it down, without changing the shared toolbar item
        if (command == TOGGLE_ARTICULATION_MAP_EDITOR_COMMAND) {
            item->setType(static_cast<ToolBarItemType::Type>(ARTICULATION_MAP_ITEM_TYPE));
        }

        items << item;
    }

    setItems(items);

    context()->currentMasterNotationChanged().onNotify(this, [this]() {
        load();
    });

    m_loaded = true;
}
