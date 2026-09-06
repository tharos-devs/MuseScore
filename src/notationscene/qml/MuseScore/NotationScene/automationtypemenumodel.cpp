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

    notationConfiguration()->currentAutomationTypeChanged().onNotify(this, [this]() {
        updateItems();
    });
}

void AutomationTypeMenuModel::updateItems()
{
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
