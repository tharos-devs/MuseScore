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

#include <algorithm>

#include "uicomponents/qml/Muse/UiComponents/menuitem.h"

namespace mu::notation {
//! NOTE: for a menu model rebuilding its items before its menu opens, or when what they show changes:
//! AbstractMenuModel::setItems() doesn't delete the items it replaces, and they can't be deleted right away - the
//! menu may still show them (e.g. while it closes after one of them was clicked, which changes what they show),
//! it crashed once they were deleted. They're only deleted right before the menu opens again: no menu shows them
//! then (it's unloaded once closed, see StyledMenuLoader). Their subitems are deleted too
class ReplacedMenuItems
{
public:
    void add(const muse::uicomponents::MenuItemList& replacedItems)
    {
        m_items << replacedItems;
    }

    void deleteAll()
    {
        muse::uicomponents::MenuItemList items = m_items;
        m_items.clear();

        for (int i = 0; i < items.size(); ++i) {
            if (muse::uicomponents::MenuItem* item = items.at(i)) {
                items << item->subitems();
            }
        }

        // a separator or an item may be listed twice (e.g. as a subitem too)
        std::sort(items.begin(), items.end());
        items.erase(std::unique(items.begin(), items.end()), items.end());
        qDeleteAll(items);
    }

private:
    muse::uicomponents::MenuItemList m_items;
};
}
