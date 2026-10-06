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

#include "panelzoom.h"

using namespace mu::playback;
using namespace muse::actions;

static const ActionCode ZOOM_IN_ACTION("track-list-panel-zoom-in");
static const ActionCode ZOOM_OUT_ACTION("track-list-panel-zoom-out");
static const ActionCode ZOOM_RESET_ACTION("track-list-panel-zoom-reset");

TrackListContextMenuModel::TrackListContextMenuModel(QObject* parent)
    : AbstractMenuModel(parent)
{
}

qreal TrackListContextMenuModel::zoom() const
{
    return PanelZoom::clamped(configuration()->trackListZoom());
}

void TrackListContextMenuModel::load()
{
    AbstractMenuModel::load();

    dispatcher()->reg(this, ZOOM_IN_ACTION, [this]() { configuration()->setTrackListZoom(PanelZoom::stepped(zoom(), +1)); });
    dispatcher()->reg(this, ZOOM_OUT_ACTION, [this]() { configuration()->setTrackListZoom(PanelZoom::stepped(zoom(), -1)); });
    dispatcher()->reg(this, ZOOM_RESET_ACTION, [this]() { configuration()->setTrackListZoom(1.0); });

    configuration()->trackListZoomChanged().onReceive(this, [this](double) {
        emit zoomChanged();

        //! NOTE: rebuilt so Zoom in/out get disabled at the limits
        updateItems();
    });

    updateItems();
}

void TrackListContextMenuModel::updateItems()
{
    setItems(PanelZoom::menuItems(this, zoom(), ZOOM_IN_ACTION, ZOOM_OUT_ACTION, ZOOM_RESET_ACTION));
}
