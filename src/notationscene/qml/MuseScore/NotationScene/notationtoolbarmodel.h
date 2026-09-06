/*
 * SPDX-License-Identifier: GPL-3.0-only
 * MuseScore-Studio-CLA-applies
 *
 * MuseScore Studio
 * Music Composition & Notation
 *
 * Copyright (C) 2021 MuseScore Limited and others
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

#include "modularity/ioc.h"
#include "context/iglobalcontext.h"

#include "uicomponents/qml/Muse/UiComponents/abstracttoolbarmodel.h"

namespace mu::notation {
class NotationToolBarModel : public muse::uicomponents::AbstractToolBarModel
{
    Q_OBJECT
    QML_ELEMENT;

    // Kept out of the model's own displayed items (see load()) - it's rendered as a SplitButton
    // by NotationToolBar.qml instead of a plain generic toolbar item, so this exposes the same
    // reactive ToolBarItem (checked/enabled/icon/title, and activate() to trigger it) for that.
    Q_PROPERTY(muse::uicomponents::ToolBarItem * automationItem READ automationItem NOTIFY automationItemChanged)

    muse::ContextInject<context::IGlobalContext> context = { this };

public:
    Q_INVOKABLE void load() override;

    muse::uicomponents::ToolBarItem* automationItem() const;

signals:
    void automationItemChanged();

protected:
    // m_automationItem isn't in the base's own tracked items list (it's excluded from setItems()
    // - see load()), so it would otherwise never receive enabled/checked updates when the action's
    // state changes (e.g. when a score opens/closes) and would stay stuck at its initial state.
    void onActionsStateChanges(const muse::actions::ActionCodeList& codes) override;

private:
    bool m_loaded = false;
    muse::uicomponents::ToolBarItem* m_automationItem = nullptr;
};
}
