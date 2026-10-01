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

#include <qqmlintegration.h>

#include "uicomponents/qml/Muse/UiComponents/abstractmenumodel.h"

#include "modularity/ioc.h"
#include "inotationsceneconfiguration.h"

namespace muse {
class TranslatableString;
}

namespace mu::notation {
//! NOTE: the Timeline panel's "..." menu: a View submenu to show/hide each meta row,
//! persisted through INotationSceneConfiguration (shared with the Timeline's own
//! right-click menu on the row labels, so both always agree).
class TimelinePanelContextMenuModel : public muse::uicomponents::AbstractMenuModel
{
    Q_OBJECT
    QML_ELEMENT

    muse::GlobalInject<INotationSceneConfiguration> configuration;

public:
    explicit TimelinePanelContextMenuModel(QObject* parent = nullptr);

    Q_INVOKABLE void load() override;
    Q_INVOKABLE void handleMenuItem(const QString& itemId) override;

private:
    muse::uicomponents::MenuItem* makeRowItem(const muse::TranslatableString& title, const std::string& rowId);
    void updateRowItems();

    muse::uicomponents::MenuItemList m_rowItems;
};
}
