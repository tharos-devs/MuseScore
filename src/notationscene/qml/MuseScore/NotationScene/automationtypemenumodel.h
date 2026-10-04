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

#include "context/iglobalcontext.h"
#include "notation/inotationconfiguration.h"
#include "engraving/automation/automationtypes.h"

namespace mu::notation {
// Backs the toolbar "Automation" split-button's dropdown - lets the user pick which curve to view
// directly, without needing the right-click "Automation type" submenu (see
// NotationContextMenuModel::makeAutomationTypeItems(), which builds an equivalent set of items for
// that submenu). Kept as a separate small copy here rather than shared: sharing would mean either
// exposing makeMenuItem() (protected on the shared AbstractMenuModel base, for good reason) or
// constructing a NotationContextMenuModel by hand as a helper, which risks not getting its
// Contextable/IOC context set up the way QML-instantiated objects normally get it for free - not
// worth risking for 4 nearly-identical menu items.
class AutomationTypeMenuModel : public muse::uicomponents::AbstractMenuModel
{
    Q_OBJECT
    QML_ELEMENT;

    Q_PROPERTY(QString currentTitle READ currentTitle NOTIFY currentTitleChanged)
    Q_PROPERTY(int currentIcon READ currentIcon NOTIFY currentTitleChanged)
    Q_PROPERTY(QString currentIconSuffix READ currentIconSuffix NOTIFY currentTitleChanged)

    muse::GlobalInject<INotationConfiguration> notationConfiguration;
    muse::ContextInject<context::IGlobalContext> globalContext = { this };

public:
    Q_INVOKABLE void init();

    //! NOTE: the name of the curve currently shown, e.g. "Tempo", "Expression" or "CC21"
    QString currentTitle() const;

    //! NOTE: the icon of the current automation type, and the text drawn right after it ("=" for a tempo)
    int currentIcon() const;
    QString currentIconSuffix() const;

signals:
    void currentTitleChanged();

private:
    void updateItems();
    muse::uicomponents::MenuItem* makeMidiCcMenu();
    muse::uicomponents::MenuItem* makeAutomationTypeItem(mu::engraving::AutomationType type, const std::string& queryTypeParam,
                                                         const muse::TranslatableString& title);
    static muse::ui::IconCode::Code automationTypeIcon(mu::engraving::AutomationType type);
};
}
