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

#include "actions/actionable.h"
#include "uicomponents/qml/Muse/UiComponents/abstractmenumodel.h"

#include "playback/iplaybackconfiguration.h"

namespace mu::playback {
//! NOTE: the Track list panel's "…" menu: Zoom in/out/Reset zoom, same steps and limits as the Mixer's
class TrackListContextMenuModel : public muse::uicomponents::AbstractMenuModel, public muse::actions::Actionable
{
    Q_OBJECT

    //! NOTE: scale factor applied to the whole list by TrackListPanel.qml (a plain Qt Quick `scale`)
    Q_PROPERTY(qreal zoom READ zoom NOTIFY zoomChanged)

    QML_ELEMENT

    muse::GlobalInject<playback::IPlaybackConfiguration> configuration;

public:
    explicit TrackListContextMenuModel(QObject* parent = nullptr);

    qreal zoom() const;

    Q_INVOKABLE void load() override;

signals:
    void zoomChanged();

private:
    void updateItems();
};
}
