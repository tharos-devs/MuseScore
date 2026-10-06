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

#include "actions/actiontypes.h"
#include "uicomponents/qml/Muse/UiComponents/menuitem.h"

namespace mu::playback {
//! NOTE: the zoom of the panels' "…" menu (Mixer, Track list): 50% to 200% in 10% steps
class PanelZoom
{
public:
    static double clamped(double zoom);

    //! NOTE: in whole percents, so repeated steps land exactly on 100% again (no floating-point drift), and an
    //! off-grid stored value snaps onto the grid
    static double stepped(double zoom, int direction);

    //! NOTE: Zoom in/Zoom out/Reset zoom, disabled at the limits
    static muse::uicomponents::MenuItemList menuItems(QObject* parent, double zoom, const muse::actions::ActionCode& zoomInCode,
                                                      const muse::actions::ActionCode& zoomOutCode,
                                                      const muse::actions::ActionCode& resetCode);
};
}
