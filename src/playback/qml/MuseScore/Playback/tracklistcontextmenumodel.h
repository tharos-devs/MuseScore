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

#include "uicomponents/qml/Muse/UiComponents/zoomablemenumodel.h"

#include "channelcategoriesmenu.h"

namespace mu::playback {
//! NOTE: the Track list panel's "…" menu: the kinds of channels listed (View) and its zoom (one step smaller than
//! 100% by default), applied to the list
class TrackListContextMenuModel : public muse::uicomponents::ZoomableMenuModel
{
    Q_OBJECT
    QML_ELEMENT

public:
    explicit TrackListContextMenuModel(QObject* parent = nullptr);

    Q_INVOKABLE void load() override;

protected:
    //! NOTE: see MixerPanelContextMenuModel's: the View items' check marks are kept by ChannelCategoriesMenu
    void onCommandStateChanged(const muse::rcommand::Command& command, const muse::rcommand::CommandState& state) override;
#ifdef MUSE_MODULE_ACTIONS_SUPPORT
    void onActionsStateChanges(const muse::actions::ActionCodeList& codes) override;
#endif

private:
    ChannelCategoriesMenu m_channelCategoriesMenu { ChannelListPanel::TrackList };
};
}
