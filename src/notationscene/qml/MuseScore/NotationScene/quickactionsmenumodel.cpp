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
#include "quickactionsmenumodel.h"

#include "types/translatablestring.h"

#include "notation/imasternotation.h"
#include "notation/inotationarticulationmaps.h"
#include "notation/inotationautomation.h"
#include "notationscene/notationcommands.h"

#include "instrumenteditor.h"
#include "midiccautomation.h"

using namespace mu::notation;
using namespace mu::engraving;
using namespace muse;
using namespace muse::uicomponents;

static const QString INSTRUMENT_EDITOR_ID("instrument-editor");

static int s_lastMidiCc = -1;

QuickActionsMenuModel::QuickActionsMenuModel(QObject* parent)
    : AbstractMenuModel(parent)
{
}

void QuickActionsMenuModel::handleMenuItem(const QString& itemId)
{
    if (itemId != INSTRUMENT_EDITOR_ID) {
        AbstractMenuModel::handleMenuItem(itemId);
        return;
    }

    const std::optional<InstrumentTrackId> trackId = actionsTrackId();
    if (!trackId) {
        return;
    }

    const std::optional<instrumenteditor::Target> target
        = instrumenteditor::target(projectSession()->project(), playbackController().get(), *trackId);
    if (target) {
        instrumenteditor::dispatch(commandDispatcher().get(), projectSession()->project(), *target,
                                   isInstrumentEditorOpened() ? "close" : "open");
    }
}

const std::vector<int>& QuickActionsMenuModel::buttonMidiCcs()
{
    static const std::vector<int> CONTROLLERS { 1, 11 };
    return CONTROLLERS;
}

int QuickActionsMenuModel::lastMidiCc()
{
    return s_lastMidiCc;
}

void QuickActionsMenuModel::setLastMidiCc(int controller)
{
    s_lastMidiCc = controller;
}

//! NOTE: also a controller picked with "Other MIDI CC…" (only known once shown), or elsewhere; without any yet, the
//! score's first own controller
void QuickActionsMenuModel::updateLastMidiCc()
{
    const IMasterNotationPtr masterNotation = projectSession()->masterNotation();
    const INotationAutomationPtr automation = masterNotation ? masterNotation->automation() : nullptr;
    const int current = notationConfiguration()->currentAutomationMidiCc();

    if (automation && automation->isAutomationModeEnabled()
        && notationConfiguration()->currentAutomationType() == AutomationType::MidiCC
        && !muse::contains(buttonMidiCcs(), current)) {
        s_lastMidiCc = current;
        return;
    }

    if (s_lastMidiCc >= 0 || !automation) {
        return;
    }

    for (const uint8_t controller : automation->customMidiCcs()) {
        if (!muse::contains(buttonMidiCcs(), static_cast<int>(controller))) {
            s_lastMidiCc = controller;
            return;
        }
    }
}

//! NOTE: shows this curve, or hides it when it's the one shown - see NotationActionController::toggleAutomationType()
MenuItem* QuickActionsMenuModel::makeAutomationItem(const QString& id, const std::string& type, int controller,
                                                    const TranslatableString& title, bool enabled)
{
    rcommand::CommandQuery query(TOGGLE_AUTOMATION_TYPE_COMMAND);
    query.addParam("type", Val(type));
    if (controller >= 0) {
        query.addParam("cc", Val(controller));
    }

    MenuItem* item = makeMenuItem(query, title);
    if (!item) {
        return nullptr;
    }

    const IMasterNotationPtr masterNotation = projectSession()->masterNotation();
    const bool isAutomationShown = masterNotation && masterNotation->automation()->isAutomationModeEnabled();
    const AutomationType currentType = notationConfiguration()->currentAutomationType();

    bool checked = false;
    if (controller >= 0) {
        checked = currentType == AutomationType::MidiCC && notationConfiguration()->currentAutomationMidiCc() == controller;
    } else {
        checked = currentType == AutomationType::Tempo;
    }

    item->setId(id);
    item->setCheckable(true);
    item->setChecked(isAutomationShown && checked);
    item->setEnabled(enabled);

    return item;
}

//! NOTE: titled with the controller's number, e.g. "CC01", described with its name
MenuItem* QuickActionsMenuModel::makeMidiCcItem(const QString& id, int controller, bool enabled)
{
    MenuItem* item = makeAutomationItem(id, "midicc", controller,
                                        TranslatableString::untranslatable(String::fromQString(midicc::controllerNumber(controller))),
                                        enabled);
    if (item) {
        item->setDescription(TranslatableString::untranslatable(String::fromQString(midicc::controllerTitle(controller))));
    }

    return item;
}

//! NOTE: toggles the last other controller picked, greyed out before any
MenuItem* QuickActionsMenuModel::makeLastMidiCcItem(bool enabled)
{
    if (s_lastMidiCc < 0) {
        MenuItem* item = makeAutomationItem("last-midi-cc", "midicc", 0, TranslatableString::untranslatable("CC"), false);
        if (item) {
            item->setChecked(false);
        }
        return item;
    }

    return makeMidiCcItem("last-midi-cc", s_lastMidiCc, enabled);
}

//! NOTE: the instrument's map, as its "Articulation map" context menu would do; an empty editor without any
MenuItem* QuickActionsMenuModel::makeArticulationMapEditorItem(bool* hasMapOut)
{
    const std::optional<InstrumentTrackId> trackId = actionsTrackId();
    const IMasterNotationPtr masterNotation = projectSession()->masterNotation();
    const INotationArticulationMapsPtr maps = masterNotation ? masterNotation->articulationMaps() : nullptr;
    const bool isVst = trackId && midicc::isVstTrack(projectSession()->project(), *trackId);
    const bool hasMap = isVst && maps && maps->data() && maps->data()->map(*trackId);

    if (hasMapOut) {
        *hasMapOut = hasMap;
    }

    rcommand::CommandQuery query(hasMap ? EDIT_ARTICULATION_MAP_COMMAND : OPEN_ARTICULATION_MAP_EDITOR_COMMAND);
    if (hasMap) {
        query.addParam("partId", Val(std::to_string(trackId->partId.toUint64())));
        query.addParam("instrumentId", Val(trackId->instrumentId.toStdString()));
    }

    MenuItem* item = makeMenuItem(query, TranslatableString("notation", "Articulation map editor"));
    if (!item) {
        return nullptr;
    }

    item->setId("articulation-map-editor");
    item->setIcon(ui::IconCode::Code::EDIT);
    item->setCheckable(true);
    item->setChecked(hasMap && maps->isEditorOpened(*trackId));
    item->setEnabled(true);

    return item;
}

MenuItem* QuickActionsMenuModel::makeInstrumentEditorItem()
{
    const std::optional<InstrumentTrackId> trackId = actionsTrackId();
    const bool hasEditor = trackId
                           && instrumenteditor::target(projectSession()->project(), playbackController().get(), *trackId);

    MenuItem* item = new MenuItem(this);
    item->setId(INSTRUMENT_EDITOR_ID);
    item->setTitle(TranslatableString("notation", "VST instrument"));
    item->setIcon(ui::IconCode::Code::PLUGIN);
    item->setCheckable(true);
    item->setChecked(hasEditor && isInstrumentEditorOpened());
    item->setEnabled(hasEditor);

    return item;
}

MenuItem* QuickActionsMenuModel::makeIconRow(const QString& id)
{
    updateLastMidiCc();

    const auto withIdAndIcon = [](MenuItem* item, const QString& id, ui::IconCode::Code icon) {
        if (item) {
            item->setId(id);
            item->setIcon(icon);
        }
        return item;
    };

    MenuItemList items {
        withIdAndIcon(makeMenuItem(TOGGLE_NOTE_VELOCITY_EDITOR_COMMAND), "velocities", ui::IconCode::Code::WAVEFORM_HALFWAVE),
        withIdAndIcon(makeMenuItem(TOGGLE_NOTE_OFFSET_EDITOR_COMMAND), "note-offsets", ui::IconCode::Code::MULTIMEASURE_REST),
        withIdAndIcon(makeMenuItem(TOGGLE_ARTICULATION_MAP_EDITOR_COMMAND), "articulations", ui::IconCode::Code::ARTICULATION),
        makeMidiCcItem("cc1", 1, true),
        makeMidiCcItem("cc11", 11, true),
        makeLastMidiCcItem(true),
        makeArticulationMapEditorItem(),
        makeInstrumentEditorItem(),
    };
    items.removeAll(nullptr);

    MenuItem* row = new MenuItem(this);
    row->setId(id);
    row->setTitle(TranslatableString::untranslatable("Quick actions"));
    row->setSubitems(items);
    row->setIconRow(true);

    return row;
}

bool QuickActionsMenuModel::isInstrumentEditorOpened() const
{
#ifdef MUSE_MODULE_VST
    const std::optional<InstrumentTrackId> trackId = actionsTrackId();
    if (!trackId || !vstPluginStateProvider()) {
        return false;
    }

    const std::optional<instrumenteditor::Target> target
        = instrumenteditor::target(projectSession()->project(), playbackController().get(), *trackId);
    return target && vstPluginStateProvider()->isInstrumentEditorOpened(target->resourceId, target->trackId);
#else
    return false;
#endif
}
