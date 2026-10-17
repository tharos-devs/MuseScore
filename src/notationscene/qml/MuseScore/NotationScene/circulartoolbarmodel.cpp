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
#include "notation/inotationautomation.h"
#include "notation/inotationelements.h"
#include "notation/inotationinteraction.h"
#include "notation/inotationselection.h"
#include "notation/inotationselectionrange.h"
#include "notationscene/notationcommands.h"
#include "appshell/appshellcommands.h"

#include "midiccautomation.h"

using namespace mu::notation;
using namespace mu::engraving;
using namespace muse;
using namespace muse::uicomponents;

static const QString MORE_MIDI_CC_ID("more-midi-cc");

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

    bool hasArticulationMap = false;
    MenuItemList newItems {
        withId(makeMenuItem(appshell::DOCK_TOGGLE_MIXER_COMMAND), "mixer"),
        withId(makeMenuItem(appshell::DOCK_TOGGLE_VIDEO_PANEL_COMMAND), "video"),
        withId(makeMenuItem(appshell::DOCK_TOGGLE_SECONDARY_WINDOW_COMMAND), "secondary-window"),
        withId(makeMenuItem(appshell::DOCK_TOGGLE_TIMELINE_COMMAND), "timeline"),

        makeMidiCcItem("cc1", 1, hasVstInstrument),
        makeMidiCcItem("cc11", 11, hasVstInstrument),
        makeAutomationItem("tempo", "tempo", -1, TranslatableString::untranslatable("Tempo"), true),
        makeLastMidiCcItem(hasVstInstrument),
        makeMoreMidiCcMenu(hasVstInstrument),

        withId(makeMenuItem(TOGGLE_NOTE_VELOCITY_EDITOR_COMMAND), "velocities"),
        makeArticulationMapEditorItem(&hasArticulationMap),
        withId(makeMenuItem(TOGGLE_ARTICULATION_MAP_EDITOR_COMMAND), "articulations"),
        makeInstrumentEditorItem(),
        withId(makeMenuItem(TOGGLE_NOTE_OFFSET_EDITOR_COMMAND), "note-offsets"),
    };
    // a null item would break the toolbar (null entries in its QML model)
    newItems.removeAll(nullptr);

    if (m_focusedStaffHasArticulationMap != hasArticulationMap) {
        m_focusedStaffHasArticulationMap = hasArticulationMap;
        emit focusedStaffHasArticulationMapChanged();
    }

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
                setLastMidiCc(subitem->commandQuery().param("cc").toInt());
            }
        }
    }

    QuickActionsMenuModel::handleMenuItem(itemId);
}

//! NOTE: the score's other MIDI CCs, and "Other MIDI CC…" to pick one
MenuItem* CircularToolbarModel::makeMoreMidiCcMenu(bool enabled)
{
    const IMasterNotationPtr masterNotation = globalContext()->currentMasterNotation();
    const INotationAutomationPtr automation = masterNotation ? masterNotation->automation() : nullptr;

    const MenuItemList items = midicc::makeMenuItems(
        [this](const rcommand::CommandQuery& query, const TranslatableString& title) { return makeMenuItem(query, title); },
        [this]() { return makeSeparator(); },
        notationConfiguration().get(), automation, false /*withDeleteItem*/, buttonMidiCcs());

    MenuItem* menu = makeMenu(TranslatableString::untranslatable("More MIDI CC"), items, MORE_MIDI_CC_ID, enabled);
    if (menu) {
        const int controller = notationConfiguration()->currentAutomationMidiCc();
        menu->setCheckable(true);
        menu->setChecked(automation && automation->isAutomationModeEnabled()
                         && notationConfiguration()->currentAutomationType() == AutomationType::MidiCC
                         && !muse::contains(buttonMidiCcs(), controller));
    }

    return menu;
}

bool CircularToolbarModel::focusedStaffHasArticulationMap() const
{
    return m_focusedStaffHasArticulationMap;
}

std::optional<InstrumentTrackId> CircularToolbarModel::actionsTrackId() const
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
