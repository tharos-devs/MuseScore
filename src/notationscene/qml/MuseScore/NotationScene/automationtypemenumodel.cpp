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

#include "automationtypemenumodel.h"

#include "types/translatablestring.h"

#include "notationscene/notationcommands.h"

using namespace mu::notation;
using namespace mu::engraving;
using namespace muse::uicomponents;

void AutomationTypeMenuModel::init()
{
    updateItems();

    // Without this, items keep whatever enabled/checked state the command had at construction
    // time (this model is built once, at NotationToolBar.qml startup) and never refresh - unlike
    // NotationContextMenuModel's items, which are rebuilt fresh on every right-click.
    subscribeOnChanges();

    // SetReplace: init() is called again every time the dropdown is about to open (see
    // NotationToolBar.qml's aboutToOpenMenu handler), so this must tolerate being re-registered
    // rather than asserting like the default SetOnce mode does.
    notationConfiguration()->currentAutomationTypeChanged().onNotify(this, [this]() {
        updateItems();
    }, muse::async::Asyncable::Mode::SetReplace);
}

void AutomationTypeMenuModel::updateItems()
{
    // AbstractMenuModel::setItems() doesn't delete the items it's replacing (unlike
    // AbstractToolBarModel's own setItems), and this runs on every dropdown open - without this,
    // each open leaks the previous batch.
    qDeleteAll(items());

    setItems({
        makeAutomationTypeItem(AutomationType::Dynamics, "dynamics", TranslatableString::untranslatable("Dynamics")),
        makeAutomationTypeItem(AutomationType::Tempo, "tempo", TranslatableString::untranslatable("Tempo")),
        makeAutomationTypeItem(AutomationType::Volume, "volume", TranslatableString::untranslatable("Volume")),
        makeAutomationTypeItem(AutomationType::Pan, "pan", TranslatableString::untranslatable("Pan")),
    });
}

MenuItem* AutomationTypeMenuModel::makeAutomationTypeItem(AutomationType type, const std::string& queryTypeParam,
                                                           const TranslatableString& title)
{
    MenuItem* item = makeMenuItem(SELECT_AUTOMATION_TYPE_COMMAND, title);
    if (!item) {
        return item;
    }

    muse::rcommand::CommandQuery query(SELECT_AUTOMATION_TYPE_COMMAND);
    query.addParam("type", muse::Val(queryTypeParam));
    item->setCommandQuery(query);

    item->setChecked(notationConfiguration()->currentAutomationType() == type);

    return item;
}
