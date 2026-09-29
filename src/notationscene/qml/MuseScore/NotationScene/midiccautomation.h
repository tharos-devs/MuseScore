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

#include <array>
#include <functional>
#include <set>
#include <vector>

#include <QString>

#include "global/containers.h"
#include "uicomponents/qml/Muse/UiComponents/menuitem.h"

#include "notation/inotationautomation.h"
#include "notation/inotationconfiguration.h"
#include "notation/imasternotation.h"
#include "notationscene/notationcommands.h"

#include "engraving/dom/part.h"
#include "engraving/dom/masterscore.h"
#include "project/inotationproject.h"
#include "project/iprojectaudiosettings.h"

//! NOTE: helpers shared by the MIDI CC automation overlay, its menus and the "Other MIDI CC" dialog
namespace mu::notation::midicc {
//! NOTE: always listed in the MIDI CC menus, before the score's custom ones
inline const std::vector<int>& predefinedControllers()
{
    static const std::vector<int> CONTROLLERS { 1, 7, 11 };
    return CONTROLLERS;
}

inline QString controllerName(int controller)
{
    static const std::array<const char*, 128> NAMES = [] {
        std::array<const char*, 128> names {};
        names.fill("");
        names[0] = "Bank Select";
        names[1] = "Modulation";
        names[2] = "Breath";
        names[4] = "Foot";
        names[5] = "Portamento Time";
        names[6] = "Data Entry";
        names[7] = "Volume";
        names[8] = "Balance";
        names[10] = "Pan";
        names[11] = "Expression";
        names[12] = "Effect 1";
        names[13] = "Effect 2";
        names[64] = "Sustain Pedal";
        names[65] = "Portamento";
        names[66] = "Sostenuto";
        names[67] = "Soft Pedal";
        names[68] = "Legato";
        names[69] = "Hold 2";
        names[71] = "Resonance";
        names[72] = "Release Time";
        names[73] = "Attack Time";
        names[74] = "Cutoff";
        names[84] = "Portamento Control";
        names[91] = "Reverb";
        names[92] = "Tremolo";
        names[93] = "Chorus";
        names[94] = "Detune";
        names[95] = "Phaser";
        return names;
    }();

    if (controller < 0 || controller >= static_cast<int>(NAMES.size())) {
        return QString();
    }

    return QString::fromLatin1(NAMES[controller]);
}

//! NOTE: number first, on at least 2 digits so lists line up, e.g. "CC01 Modulation", "CC21"
inline QString controllerNumber(int controller)
{
    return QStringLiteral("CC%1").arg(controller, 2, 10, QLatin1Char('0'));
}

inline QString controllerTitle(int controller)
{
    const QString name = controllerName(controller);
    return name.isEmpty() ? controllerNumber(controller) : QStringLiteral("%1 %2").arg(controllerNumber(controller), name);
}

//! NOTE: MIDI CC automation only reaches VST instruments (MuseSounds/soundfonts don't get these controllers)
inline bool isVstInstrument(const project::INotationProjectPtr& project, const engraving::Part* part)
{
    if (!project || !part || !project->audioSettings()) {
        return false;
    }

    const engraving::InstrumentTrackId trackId { part->id(), part->instrumentId() };
    return project->audioSettings()->trackInputParams(trackId).type() == muse::audio::AudioSourceType::Vsti;
}

inline bool hasVstInstrument(const project::INotationProjectPtr& project, const engraving::Score* score)
{
    if (!score) {
        return false;
    }

    for (const engraving::Part* part : score->parts()) {
        if (isVstInstrument(project, part)) {
            return true;
        }
    }

    return false;
}

using MakeMenuItemFn = std::function<muse::uicomponents::MenuItem* (const muse::rcommand::CommandQuery&, const muse::TranslatableString&)>;
using MakeSeparatorFn = std::function<muse::uicomponents::MenuItem* ()>;

//! NOTE: the predefined controllers, then the score's custom ones, a separator and "Other MIDI CC…";
//! a controller that already has a curve somewhere in the score is marked with a dot
inline muse::uicomponents::MenuItemList makeMenuItems(const MakeMenuItemFn& makeMenuItem, const MakeSeparatorFn& makeSeparator,
                                                      const INotationConfiguration* configuration,
                                                      const INotationAutomationPtr& automation)
{
    std::vector<int> controllers = predefinedControllers();
    std::set<int> controllersWithCurve;

    if (automation) {
        for (const uint8_t controller : automation->customMidiCcs()) {
            if (!muse::contains(controllers, static_cast<int>(controller))) {
                controllers.push_back(controller);
            }
        }

        if (const AutomationDataConstPtr data = automation->automationData()) {
            for (const auto& [key, curve] : data->curves()) {
                if (key.type == AutomationType::MidiCC && !curve.empty()) {
                    controllersWithCurve.insert(key.controller);
                }
            }
        }
    }

    const bool isMidiCcCurrent = configuration->currentAutomationType() == AutomationType::MidiCC;
    const int currentController = configuration->currentAutomationMidiCc();
    if (isMidiCcCurrent && !muse::contains(controllers, currentController)) {
        controllers.push_back(currentController);
    }

    muse::uicomponents::MenuItemList items;

    for (const int controller : controllers) {
        muse::rcommand::CommandQuery query(SELECT_AUTOMATION_TYPE_COMMAND);
        query.addParam("type", muse::Val("midicc"));
        query.addParam("cc", muse::Val(controller));

        QString title = controllerTitle(controller);
        if (muse::contains(controllersWithCurve, controller)) {
            title += QStringLiteral("  \u2022");
        }

        muse::uicomponents::MenuItem* item
            = makeMenuItem(query, muse::TranslatableString::untranslatable(muse::String::fromQString(title)));
        if (item) {
            item->setChecked(isMidiCcCurrent && controller == currentController);
            items << item;
        }
    }

    items << makeSeparator();

    muse::rcommand::CommandQuery otherQuery(SELECT_AUTOMATION_TYPE_COMMAND);
    otherQuery.addParam("type", muse::Val("midicc"));
    if (muse::uicomponents::MenuItem* other = makeMenuItem(otherQuery, muse::TranslatableString::untranslatable("Other MIDI CC\u2026"))) {
        other->setCheckable(false);
        items << other;
    }

    return items;
}

using MakeMenuFn = std::function<muse::uicomponents::MenuItem* (const muse::TranslatableString&, const muse::uicomponents::MenuItemList&,
                                                                const QString& /*menuId*/, bool /*enabled*/)>;

//! NOTE: the "MIDI CC" submenu itself (see makeMenuItems()): greyed out without any VST instrument in the score,
//! checked while a MIDI CC curve is shown
inline muse::uicomponents::MenuItem* makeMenu(const MakeMenuFn& makeMenu, const MakeMenuItemFn& makeMenuItem,
                                              const MakeSeparatorFn& makeSeparator, const INotationConfiguration* configuration,
                                              const IMasterNotationPtr& masterNotation, const project::INotationProjectPtr& project,
                                              const QString& menuId)
{
    const INotationAutomationPtr automation = masterNotation ? masterNotation->automation() : nullptr;
    const muse::uicomponents::MenuItemList items = makeMenuItems(makeMenuItem, makeSeparator, configuration, automation);

    const bool enabled = masterNotation && hasVstInstrument(project, masterNotation->masterScore());
    muse::uicomponents::MenuItem* menu = makeMenu(muse::TranslatableString::untranslatable("MIDI CC"), items, menuId, enabled);
    if (menu) {
        menu->setChecked(configuration->currentAutomationType() == AutomationType::MidiCC);
    }
    return menu;
}
}
