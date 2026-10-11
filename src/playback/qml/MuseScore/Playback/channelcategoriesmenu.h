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

#include <map>

#include <QPointer>

#include "async/asyncable.h"
#include "modularity/ioc.h"
#include "uicomponents/qml/Muse/UiComponents/menuitem.h"

#include "playback/iplaybackconfiguration.h"
#include "playback/playbacktypes.h"

namespace mu::playback {
//! NOTE: the checkable items of a panel's "…" menu showing/hiding each kind of channels (Video, Metronome,
//! Instruments, Aux FX, Aux Group), shared by the Mixer and the Track list. Their check marks follow the
//! setting, wherever it's changed from
class ChannelCategoriesMenu : public muse::async::Asyncable
{
    muse::GlobalInject<IPlaybackConfiguration> configuration;

public:
    explicit ChannelCategoriesMenu(ChannelListPanel panel);

    muse::uicomponents::MenuItemList makeItems(QObject* parent);

    //! NOTE: the menu model must leave these items' check marks alone (see
    //! MixerPanelContextMenuModel::onCommandStateChanged()): a command's state is shared by all its queries
    static bool isOwnCommand(const muse::rcommand::Command& command);
    static bool isOwnActionCode(const std::string& code);

private:
    ChannelListPanel m_panel;
    std::map<ChannelCategory, QPointer<muse::uicomponents::MenuItem> > m_items;
};
}
