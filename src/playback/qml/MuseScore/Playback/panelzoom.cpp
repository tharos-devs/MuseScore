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

#include "panelzoom.h"

#include <algorithm>
#include <cmath>

#include "types/translatablestring.h"

using namespace mu::playback;
using namespace muse;
using namespace muse::ui;
using namespace muse::uicomponents;
using namespace muse::actions;

static constexpr int MIN_ZOOM_PERCENT = 50;
static constexpr int MAX_ZOOM_PERCENT = 200;
static constexpr int ZOOM_STEP_PERCENT = 10;

static int toPercent(double zoom)
{
    return static_cast<int>(std::lround(PanelZoom::clamped(zoom) * 100));
}

double PanelZoom::clamped(double zoom)
{
    return std::clamp(zoom, MIN_ZOOM_PERCENT / 100.0, MAX_ZOOM_PERCENT / 100.0);
}

double PanelZoom::stepped(double zoom, int direction)
{
    const int current = toPercent(zoom);
    const int next = direction > 0
                     ? (current / ZOOM_STEP_PERCENT + 1) * ZOOM_STEP_PERCENT
                     : ((current + ZOOM_STEP_PERCENT - 1) / ZOOM_STEP_PERCENT - 1) * ZOOM_STEP_PERCENT;

    return std::clamp(next, MIN_ZOOM_PERCENT, MAX_ZOOM_PERCENT) / 100.0;
}

static MenuItem* buildZoomItem(QObject* parent, const TranslatableString& title, const ActionCode& code, bool enabled)
{
    UiAction action;
    action.title = title;
    action.code = code;

    MenuItem* item = new MenuItem(action, parent);
    item->setId(QString::fromStdString(code));
    item->setState(UiActionState::make_enabled(enabled));
    return item;
}

MenuItemList PanelZoom::menuItems(QObject* parent, double zoom, const ActionCode& zoomInCode, const ActionCode& zoomOutCode,
                                  const ActionCode& resetCode)
{
    const int percent = toPercent(zoom);

    return {
        buildZoomItem(parent, TranslatableString("playback", "Zoom in"), zoomInCode, percent < MAX_ZOOM_PERCENT),
        buildZoomItem(parent, TranslatableString("playback", "Zoom out"), zoomOutCode, percent > MIN_ZOOM_PERCENT),
        buildZoomItem(parent, TranslatableString("playback", "Reset zoom"), resetCode, percent != 100),
    };
}
