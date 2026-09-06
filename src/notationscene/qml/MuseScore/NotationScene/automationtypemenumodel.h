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

#include "modularity/ioc.h"

#include "uicomponents/qml/Muse/UiComponents/abstractmenumodel.h"

#include "notation/inotationconfiguration.h"
#include "engraving/automation/automationtypes.h"

namespace mu::notation {
// Backs the toolbar "Automation" split-button's dropdown - lets the user pick which curve to view
// directly, without needing the right-click "Automation type" submenu (see
// NotationContextMenuModel::makeAutomationTypeItems(), which builds an equivalent set of items for
// that submenu - kept as a separate small copy here rather than shared, since makeMenuItem() is a
// protected AbstractMenuModel member and the two models don't otherwise share a common owner).
class AutomationTypeMenuModel : public muse::uicomponents::AbstractMenuModel
{
    Q_OBJECT
    QML_ELEMENT;

    muse::GlobalInject<INotationConfiguration> notationConfiguration;

public:
    Q_INVOKABLE void init();

private:
    void updateItems();
    muse::uicomponents::MenuItem* makeAutomationTypeItem(mu::engraving::AutomationType type, const std::string& queryTypeParam,
                                                         const muse::TranslatableString& title);
};
}
