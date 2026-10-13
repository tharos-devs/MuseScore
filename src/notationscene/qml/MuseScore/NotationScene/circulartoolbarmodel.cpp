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
#include "circulartoolbarmodel.h"

#include "types/translatablestring.h"

#include "engraving/dom/engravingitem.h"
#include "engraving/dom/part.h"
#include "engraving/dom/score.h"
#include "engraving/dom/staff.h"

#include "notation/imasternotation.h"
#include "notation/inotationarticulationmaps.h"
#include "notation/inotationautomation.h"
#include "notation/inotationelements.h"
#include "notation/inotationinteraction.h"
#include "notation/inotationselection.h"
#include "notation/inotationselectionrange.h"
#include "notationscene/notationcommands.h"
#include "appshell/appshellcommands.h"

#include "instrumenteditor.h"
#include "midiccautomation.h"

using namespace mu::notation;
using namespace mu::engraving;
using namespace muse;
using namespace muse::uicomponents;

static const QString INSTRUMENT_EDITOR_ID("instrument-editor");
static const QString MORE_MIDI_CC_ID("more-midi-cc");

//! NOTE: the MIDI CCs with their own button, the others are in the "…" menu
static const std::vector<int> RING_MIDI_CCS { 1, 11 };

void CircularToolbarModel::prepareOpening()
{
    m_replacedItems.deleteAll();
    updateLastMidiCc();

    const IMasterNotationPtr masterNotation = globalContext()->currentMasterNotation();
    const bool hasVstInstrument = masterNotation && midicc::hasVstInstrument(globalContext()->currentProject(),
                                                                             masterNotation->masterScore());

    const auto withId = [](MenuItem* item, const QString& id) {
        if (item) {
            item->setId(id);
        }
        return item;
    };

    MenuItemList newItems {
        withId(makeMenuItem(appshell::DOCK_TOGGLE_MIXER_COMMAND), "mixer"),
        withId(makeMenuItem(appshell::DOCK_TOGGLE_VIDEO_PANEL_COMMAND), "video"),
        withId(makeMenuItem(appshell::DOCK_TOGGLE_SECONDARY_WINDOW_COMMAND), "secondary-window"),
        withId(makeMenuItem(appshell::DOCK_TOGGLE_TIMELINE_COMMAND), "timeline"),

        makeAutomationItem("cc1", "midicc", 1, TranslatableString::untranslatable("CC01"), hasVstInstrument),
        makeAutomationItem("cc11", "midicc", 11, TranslatableString::untranslatable("CC11"), hasVstInstrument),
        makeAutomationItem("tempo", "tempo", -1, TranslatableString::untranslatable("Tempo"), true),
        makeLastMidiCcItem(hasVstInstrument),
        makeMoreMidiCcMenu(hasVstInstrument),

        withId(makeMenuItem(TOGGLE_NOTE_VELOCITY_EDITOR_COMMAND), "velocities"),
        makeArticulationMapEditorItem(),
        withId(makeMenuItem(TOGGLE_ARTICULATION_MAP_EDITOR_COMMAND), "articulations"),
        makeInstrumentEditorItem(),
        withId(makeMenuItem(TOGGLE_NOTE_OFFSET_EDITOR_COMMAND), "note-offsets"),
    };
    // a null item would break the toolbar (null entries in its QML model)
    newItems.removeAll(nullptr);

    const MenuItemList oldItems = items();
    setItems(newItems);
    m_replacedItems.add(oldItems);
}

void CircularToolbarModel::handleMenuItem(const QString& itemId)
{
    // A controller picked in the "…" menu becomes the one of the button next to it
    for (const MenuItem* item : items()) {
        if (item->id() != MORE_MIDI_CC_ID) {
            continue;
        }
        for (const MenuItem* subitem : item->subitems()) {
            if (subitem->id() == itemId && subitem->commandQuery().contains("cc")) {
                m_lastMidiCc = subitem->commandQuery().param("cc").toInt();
            }
        }
    }

    if (itemId != INSTRUMENT_EDITOR_ID) {
        AbstractMenuModel::handleMenuItem(itemId);
        return;
    }

    const std::optional<InstrumentTrackId> trackId = focusedTrackId();
    if (!trackId) {
        return;
    }

    const std::optional<instrumenteditor::Target> target
        = instrumenteditor::target(globalContext()->currentProject(), playbackController().get(), *trackId);
    if (target) {
        instrumenteditor::dispatch(actionsDispatcher().get(), globalContext()->currentProject(), *target,
                                   isInstrumentEditorOpened() ? "close" : "open");
    }
}

//! NOTE: shows this curve, or hides it when it's the one shown - see NotationActionController::toggleAutomationType()
MenuItem* CircularToolbarModel::makeAutomationItem(const QString& id, const std::string& type, int controller,
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

    const IMasterNotationPtr masterNotation = globalContext()->currentMasterNotation();
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

//! NOTE: the score's other MIDI CCs, and "Other MIDI CC…" to pick one
MenuItem* CircularToolbarModel::makeMoreMidiCcMenu(bool enabled)
{
    const IMasterNotationPtr masterNotation = globalContext()->currentMasterNotation();
    const INotationAutomationPtr automation = masterNotation ? masterNotation->automation() : nullptr;

    const MenuItemList items = midicc::makeMenuItems(
        [this](const rcommand::CommandQuery& query, const TranslatableString& title) { return makeMenuItem(query, title); },
        [this]() { return makeSeparator(); },
        notationConfiguration().get(), automation, false /*withDeleteItem*/, RING_MIDI_CCS);

    MenuItem* menu = makeMenu(TranslatableString::untranslatable("More MIDI CC"), items, MORE_MIDI_CC_ID, enabled);
    if (menu) {
        const int controller = notationConfiguration()->currentAutomationMidiCc();
        menu->setCheckable(true);
        menu->setChecked(automation && automation->isAutomationModeEnabled()
                         && notationConfiguration()->currentAutomationType() == AutomationType::MidiCC
                         && !muse::contains(RING_MIDI_CCS, controller));
    }

    return menu;
}

//! NOTE: also a controller picked with "Other MIDI CC…" (only known once shown), or elsewhere; without any yet, the
//! score's first own controller
void CircularToolbarModel::updateLastMidiCc()
{
    const IMasterNotationPtr masterNotation = globalContext()->currentMasterNotation();
    const INotationAutomationPtr automation = masterNotation ? masterNotation->automation() : nullptr;
    const int current = notationConfiguration()->currentAutomationMidiCc();

    if (automation && automation->isAutomationModeEnabled()
        && notationConfiguration()->currentAutomationType() == AutomationType::MidiCC
        && !muse::contains(RING_MIDI_CCS, current)) {
        m_lastMidiCc = current;
        return;
    }

    if (m_lastMidiCc >= 0 || !automation) {
        return;
    }

    for (const uint8_t controller : automation->customMidiCcs()) {
        if (!muse::contains(RING_MIDI_CCS, static_cast<int>(controller))) {
            m_lastMidiCc = controller;
            return;
        }
    }
}

//! NOTE: toggles the last controller picked in the "…" menu, greyed out before any
MenuItem* CircularToolbarModel::makeLastMidiCcItem(bool enabled)
{
    if (m_lastMidiCc < 0) {
        MenuItem* item = makeAutomationItem("last-midi-cc", "midicc", 0, TranslatableString::untranslatable("CC"), false);
        if (item) {
            item->setChecked(false);
        }
        return item;
    }

    return makeAutomationItem("last-midi-cc", "midicc", m_lastMidiCc,
                              TranslatableString::untranslatable(muse::String::fromQString(midicc::controllerNumber(m_lastMidiCc))),
                              enabled);
}

bool CircularToolbarModel::focusedStaffHasArticulationMap() const
{
    return m_focusedStaffHasArticulationMap;
}

//! NOTE: the selected staff's map, as its "Articulation map" context menu would do; an empty editor without any
MenuItem* CircularToolbarModel::makeArticulationMapEditorItem()
{
    const std::optional<InstrumentTrackId> trackId = focusedTrackId();
    const IMasterNotationPtr masterNotation = globalContext()->currentMasterNotation();
    const INotationArticulationMapsPtr maps = masterNotation ? masterNotation->articulationMaps() : nullptr;
    const bool isVst = trackId && midicc::isVstTrack(globalContext()->currentProject(), *trackId);
    const bool hasMap = isVst && maps && maps->data() && maps->data()->map(*trackId);

    if (m_focusedStaffHasArticulationMap != hasMap) {
        m_focusedStaffHasArticulationMap = hasMap;
        emit focusedStaffHasArticulationMapChanged();
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
    item->setCheckable(true);
    item->setChecked(hasMap && maps->isEditorOpened(*trackId));
    item->setEnabled(true);

    return item;
}

MenuItem* CircularToolbarModel::makeInstrumentEditorItem()
{
    const std::optional<InstrumentTrackId> trackId = focusedTrackId();
    const bool hasEditor = trackId
                           && instrumenteditor::target(globalContext()->currentProject(), playbackController().get(), *trackId);

    MenuItem* item = new MenuItem(this);
    item->setId(INSTRUMENT_EDITOR_ID);
    item->setTitle(TranslatableString("notation", "VST instrument"));
    item->setIcon(ui::IconCode::Code::PLUGIN);
    item->setCheckable(true);
    item->setChecked(hasEditor && isInstrumentEditorOpened());
    item->setEnabled(hasEditor);

    return item;
}

std::optional<InstrumentTrackId> CircularToolbarModel::focusedTrackId() const
{
    const INotationPtr notation = globalContext()->currentNotation();
    if (!notation) {
        return std::nullopt;
    }

    const INotationSelectionPtr selection = notation->interaction()->selection();
    const Staff* staff = nullptr;
    Fraction tick(0, 1);

    if (selection->isRange()) {
        const Score* score = notation->elements()->msScore();
        staff = score ? score->staff(selection->range()->startStaffIndex()) : nullptr;
        tick = selection->range()->startTick();
    } else if (const EngravingItem* element = selection->element()) {
        staff = element->staff();
        tick = element->tick();
    } else if (!selection->elements().empty()) {
        const EngravingItem* element = selection->elements().front();
        staff = element->staff();
        tick = element->tick();
    }

    const Part* part = staff ? staff->part() : nullptr;
    if (!part) {
        return std::nullopt;
    }

    // an instrument change makes it another track
    const Instrument* instrument = part->instrument(tick);
    return InstrumentTrackId { part->id(), instrument ? instrument->id() : part->instrumentId() };
}

bool CircularToolbarModel::isInstrumentEditorOpened() const
{
#ifdef MUSE_MODULE_VST
    const std::optional<InstrumentTrackId> trackId = focusedTrackId();
    if (!trackId || !vstPluginStateProvider()) {
        return false;
    }

    const std::optional<instrumenteditor::Target> target
        = instrumenteditor::target(globalContext()->currentProject(), playbackController().get(), *trackId);
    return target && vstPluginStateProvider()->isInstrumentEditorOpened(target->resourceId, target->trackId);
#else
    return false;
#endif
}
