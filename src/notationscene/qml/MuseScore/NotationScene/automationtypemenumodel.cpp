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

#include "midiccautomation.h"

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
        emit currentTitleChanged();
    }, muse::async::Asyncable::Mode::SetReplace);
}

QString AutomationTypeMenuModel::currentTitle() const
{
    switch (notationConfiguration()->currentAutomationType()) {
    case AutomationType::Dynamics: return QStringLiteral("Dynamics");
    case AutomationType::Tempo: return QStringLiteral("Tempo");
    case AutomationType::Volume: return QStringLiteral("Volume");
    case AutomationType::Pan: return QStringLiteral("Pan");
    case AutomationType::MidiCC: {
        // Just the name, the button's width being fixed - the number only for a controller without a standard name
        const int controller = notationConfiguration()->currentAutomationMidiCc();
        const QString name = midicc::controllerName(controller);
        return name.isEmpty() ? midicc::controllerNumber(controller) : name;
    }
    case AutomationType::Unknown: break;
    }

    return QString();
}

muse::ui::IconCode::Code AutomationTypeMenuModel::automationTypeIcon(AutomationType type)
{
    using Code = muse::ui::IconCode::Code;

    switch (type) {
    case AutomationType::Dynamics: return Code::DYNAMIC_FORTE;
    case AutomationType::Tempo: return Code::NOTE_HEAD_QUARTER;
    case AutomationType::Volume: return Code::AUDIO;
    case AutomationType::Pan: return Code::NO_BREAK;
    case AutomationType::MidiCC: return Code::AUTOMATION;
    case AutomationType::Unknown: break;
    }

    return Code::AUTOMATION;
}

int AutomationTypeMenuModel::currentIcon() const
{
    return static_cast<int>(automationTypeIcon(notationConfiguration()->currentAutomationType()));
}

QString AutomationTypeMenuModel::currentIconSuffix() const
{
    return notationConfiguration()->currentAutomationType() == AutomationType::Tempo ? QStringLiteral("=") : QString();
}

void AutomationTypeMenuModel::updateItems()
{
    // AbstractMenuModel::setItems() doesn't delete the items it's replacing (unlike
    // AbstractToolBarModel's own setItems), and this runs on every dropdown open - without this,
    // each open leaks the previous batch.
    qDeleteAll(items());

    MenuItem* midiCcMenu = makeMidiCcMenu();
    if (midiCcMenu) {
        midiCcMenu->setIcon(automationTypeIcon(AutomationType::MidiCC));
        midiCcMenu->setCheckable(true);
    }

    setItems({
        makeAutomationTypeItem(AutomationType::Dynamics, "dynamics", TranslatableString::untranslatable("Dynamics")),
        makeAutomationTypeItem(AutomationType::Tempo, "tempo", TranslatableString::untranslatable("Tempo")),
        makeAutomationTypeItem(AutomationType::Volume, "volume", TranslatableString::untranslatable("Volume")),
        makeAutomationTypeItem(AutomationType::Pan, "pan", TranslatableString::untranslatable("Pan")),
        makeSeparator(),
        midiCcMenu,
    });
}

MenuItem* AutomationTypeMenuModel::makeMidiCcMenu()
{
    return midicc::makeMenu(
        [this](const TranslatableString& title, const MenuItemList& items, const QString& id, bool enabled) {
        return makeMenu(title, items, id, enabled);
    },
        [this](const muse::rcommand::CommandQuery& query, const TranslatableString& title) { return makeMenuItem(query, title); },
        [this]() { return makeSeparator(); },
        notationConfiguration().get(), globalContext()->currentMasterNotation(), globalContext()->currentProject(), "midi-cc",
        false /*withDeleteItem: only in the staff context menu*/);
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

    // A checkable item with an icon: the menu shows the checkmark in front of the icon
    item->setIcon(automationTypeIcon(type));
    // the command has no icon color: an empty one keeps the theme's (instead of black)
    item->setIconColor(QString());
    item->setCheckable(true);
    item->setChecked(notationConfiguration()->currentAutomationType() == type);

    return item;
}
