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

using namespace mu::notation;
using namespace muse::uicomponents;
using namespace muse::actions;

void NotationToolBarModel::load()
{
    if (m_loaded) {
        return;
    }

    muse::actions::ActionCodeList itemsCodes = {
        "parts",
        "toggle-mixer",
        "toggle-automation",
        "toggle-note-offset-editor",
        "toggle-note-velocity-editor"
    };

    ToolBarItemList items;
    for (const ActionCode& code : itemsCodes) {
        ToolBarItem* item = makeItem(code);
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
        if (code == "toggle-automation") {
            item->setType(ToolBarItemType::USER_TYPE);
        }

        items << item;
    }

    setItems(items);

    context()->currentMasterNotationChanged().onNotify(this, [this]() {
        load();
    });

    AbstractToolBarModel::load();

    m_loaded = true;
}
