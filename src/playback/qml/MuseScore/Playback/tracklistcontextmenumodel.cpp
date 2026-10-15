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

#include "tracklistcontextmenumodel.h"

#include "settings.h"
#include "types/translatablestring.h"

using namespace mu::playback;
using namespace muse;
using namespace muse::uicomponents;

static const Settings::Key CHANNEL_STRIP_VISIBLE_KEY("playback", "playback/trackList/channelStripVisible");
static const QString TOGGLE_CHANNEL_STRIP_ID("toggle-channel-strip");

TrackListContextMenuModel::TrackListContextMenuModel(QObject* parent)
    : ZoomableMenuModel(muse::Settings::Key("playback", "playback/trackList/zoom"), 0.9, parent)
{
    settings()->setDefaultValue(CHANNEL_STRIP_VISIBLE_KEY, Val(true));
    settings()->valueChanged(CHANNEL_STRIP_VISIBLE_KEY).onReceive(this, [this](const Val&) {
        if (m_channelStripItem) {
            m_channelStripItem->setChecked(channelStripVisible());
        }
        emit channelStripVisibleChanged();
    });
}

void TrackListContextMenuModel::load()
{
    ZoomableMenuModel::load();

    MenuItem* channelStripItem = new MenuItem(this);
    channelStripItem->setId(TOGGLE_CHANNEL_STRIP_ID);
    channelStripItem->setTitle(TranslatableString("playback", "Channel strip"));
    channelStripItem->setCheckable(true);
    channelStripItem->setChecked(channelStripVisible());
    channelStripItem->setEnabled(true);
    m_channelStripItem = channelStripItem;

    setItems({
        makeMenu(TranslatableString("playback", "View"), m_channelCategoriesMenu.makeItems(this)),
        makeSeparator(),
        makeZoomMenu(),
        makeSeparator(),
        channelStripItem
    });
}

void TrackListContextMenuModel::handleMenuItem(const QString& itemId)
{
    if (itemId == TOGGLE_CHANNEL_STRIP_ID) {
        settings()->setSharedValue(CHANNEL_STRIP_VISIBLE_KEY, Val(!channelStripVisible()));
        return;
    }

    ZoomableMenuModel::handleMenuItem(itemId);
}

bool TrackListContextMenuModel::channelStripVisible() const
{
    return settings()->value(CHANNEL_STRIP_VISIBLE_KEY).toBool();
}

void TrackListContextMenuModel::onCommandStateChanged(const rcommand::Command& command, const rcommand::CommandState& state)
{
    if (ChannelCategoriesMenu::isOwnCommand(command)) {
        return;
    }

    AbstractMenuModel::onCommandStateChanged(command, state);
}

#ifdef MUSE_MODULE_ACTIONS_SUPPORT
void TrackListContextMenuModel::onActionsStateChanges(const actions::ActionCodeList& codes)
{
    actions::ActionCodeList filtered;
    for (const actions::ActionCode& code : codes) {
        if (!ChannelCategoriesMenu::isOwnActionCode(code)) {
            filtered.push_back(code);
        }
    }

    if (!filtered.empty()) {
        AbstractMenuModel::onActionsStateChanges(filtered);
    }
}

#endif
