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

#include "channelcategoriesmenu.h"

#include "types/translatablestring.h"

#include "playback/playbackcommands.h"

using namespace mu::playback;
using namespace muse;
using namespace muse::uicomponents;

static TranslatableString channelCategoryTitle(ChannelCategory category)
{
    switch (category) {
    case ChannelCategory::Video: return TranslatableString("playback", "Video");
    case ChannelCategory::Metronome: return TranslatableString("playback", "Metronome");
    case ChannelCategory::Instruments: return TranslatableString("playback", "Instruments");
    case ChannelCategory::AuxFx: return TranslatableString("playback", "Aux FX");
    case ChannelCategory::AuxGroup: return TranslatableString("playback", "Aux Group");
    }

    return {};
}

ChannelCategoriesMenu::ChannelCategoriesMenu(ChannelListPanel panel)
    : m_panel(panel)
{
    configuration()->channelCategoryVisibleChanged().onReceive(
        this, [this](ChannelListPanel panel, ChannelCategory category, bool visible) {
        if (panel != m_panel) {
            return;
        }

        auto it = m_items.find(category);
        if (it != m_items.end() && it->second) {
            it->second->setChecked(visible);
        }
    });
}

MenuItemList ChannelCategoriesMenu::makeItems(QObject* parent)
{
    MenuItemList items;
    m_items.clear();

    for (ChannelCategory category : allChannelCategories()) {
        MenuItem* item = new MenuItem(parent);
        item->setTitle(channelCategoryTitle(category));
        item->setCheckable(true);
        item->setChecked(configuration()->isChannelCategoryVisible(m_panel, category));
        item->setCommandQuery(rcommand::make_query(TOGGLE_CHANNEL_CATEGORY_COMMAND,
                                                   { { "panel", Val(str_conv(m_panel)) },
                                                     { "category", Val(str_conv(category)) } }));

        m_items[category] = item;
        items << item;
    }

    return items;
}

bool ChannelCategoriesMenu::isOwnCommand(const rcommand::Command& command)
{
    return command == TOGGLE_CHANNEL_CATEGORY_COMMAND;
}

bool ChannelCategoriesMenu::isOwnActionCode(const std::string& code)
{
    return code.rfind(TOGGLE_CHANNEL_CATEGORY_COMMAND.toString(), 0) == 0;
}
