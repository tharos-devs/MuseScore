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

    // "toggle-automation" is deliberately NOT in this list: it's rendered as a SplitButton
    // (see NotationToolBar.qml) instead of a plain generic ToolBarItem, so it can offer a
    // dropdown to pick the automation type directly, alongside its usual toggle behavior.
    muse::actions::ActionCodeList itemsCodes = {
        "parts",
        "toggle-mixer"
    };

    ToolBarItemList items;
    for (const ActionCode& code : itemsCodes) {
        ToolBarItem* item = makeItem(code);
        if (!item) {
            continue;
        }

        item->setShowTitle(!isCompactMode());
        item->setIsTitleBold(true);

        items << item;
    }

    setItems(items);

    if (!m_automationItem) {
        m_automationItem = makeItem("toggle-automation");
        if (m_automationItem) {
            m_automationItem->setShowTitle(!isCompactMode());
            m_automationItem->setIsTitleBold(true);
            emit automationItemChanged();
        }
    }

    context()->currentMasterNotationChanged().onNotify(this, [this]() {
        load();
    });

    AbstractToolBarModel::load();

    m_loaded = true;
}

muse::uicomponents::ToolBarItem* NotationToolBarModel::automationItem() const
{
    return m_automationItem;
}
