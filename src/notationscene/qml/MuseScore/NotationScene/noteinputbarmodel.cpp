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
#include "noteinputbarmodel.h"

#include "rcommand/commandtypes.h"
#include "types/translatablestring.h"

#include "context/shortcutcontext.h"

#include "notation/inotationelements.h" // IWYU pragma: keep
#include "notation/inotationinteraction.h"
#include "notation/inotationnoteinput.h"
#include "notation/inotationselection.h"
#include "notation/inotationundostack.h"

#include "internal/notationuiactions.h"
#include "notationcommands.h"

using namespace mu;
using namespace mu::notation;
using namespace muse;
using namespace muse::actions;
using namespace muse::ui;
using namespace muse::uicomponents;

static const QString TOOLBAR_NAME("noteInput");

static const ActionCode ADD_ACTION_CODE("add");

const std::string NoteInputBarModel::CROSS_STAFF_BEAMING_SUBITEMS("cross-staff-beaming-subitems");
const std::string NoteInputBarModel::TUPLET_SUBITEMS("tuplets-subitems");

static const QString CARET_GRID_ITEM_PREFIX("caret-grid:");
// Invisible item placed after the caret input button, which spans its cell too
static const QString CARET_GRID_SPACER_ID("caret-grid-spacer");

struct CaretGridValue {
    DurationType type;
    int dots;
    IconCode::Code icon;
    const char* title;
};

// Rhythmic grid resolutions offered for caret input, finest first
static const std::vector<CaretGridValue> CARET_GRID_VALUES {
    { DurationType::V_32ND, 0, IconCode::Code::NOTE_32ND, QT_TRANSLATE_NOOP("notation", "32nd note") },
    { DurationType::V_16TH, 0, IconCode::Code::NOTE_16TH, QT_TRANSLATE_NOOP("notation", "16th note") },
    { DurationType::V_16TH, 1, IconCode::Code::NOTE_16TH, QT_TRANSLATE_NOOP("notation", "Dotted 16th note") },
    { DurationType::V_EIGHTH, 0, IconCode::Code::NOTE_8TH, QT_TRANSLATE_NOOP("notation", "Eighth note") },
    { DurationType::V_EIGHTH, 1, IconCode::Code::NOTE_8TH, QT_TRANSLATE_NOOP("notation", "Dotted eighth note") },
    { DurationType::V_QUARTER, 0, IconCode::Code::NOTE_QUARTER, QT_TRANSLATE_NOOP("notation", "Quarter note") },
    { DurationType::V_QUARTER, 1, IconCode::Code::NOTE_QUARTER, QT_TRANSLATE_NOOP("notation", "Dotted quarter note") },
    { DurationType::V_HALF, 0, IconCode::Code::NOTE_HALF, QT_TRANSLATE_NOOP("notation", "Half note") },
    { DurationType::V_HALF, 1, IconCode::Code::NOTE_HALF, QT_TRANSLATE_NOOP("notation", "Dotted half note") },
    { DurationType::V_WHOLE, 0, IconCode::Code::NOTE_WHOLE, QT_TRANSLATE_NOOP("notation", "Whole note") },
};

static int caretGridValueTicks(const CaretGridValue& value)
{
    return Duration(mu::engraving::DurationTypeWithDots(value.type, value.dots)).ticks().ticks();
}

NoteInputBarModel::NoteInputBarModel(QObject* parent)
    : AbstractMenuModel(parent)
{
}

QVariant NoteInputBarModel::data(const QModelIndex& index, int role) const
{
    int row = index.row();
    if (!isIndexValid(row)) {
        return QVariant();
    }

    const MenuItem* item = items().at(row);
    switch (role) {
    case OrderRole: return row;
    case SectionRole: return item->section();
    }

    return AbstractMenuModel::data(index, role);
}

QHash<int, QByteArray> NoteInputBarModel::roleNames() const
{
    QHash<int, QByteArray> roles = AbstractMenuModel::roleNames();
    roles[OrderRole] = "order";
    roles[SectionRole] = "section";

    return roles;
}

void NoteInputBarModel::classBegin()
{
    init();
}

void NoteInputBarModel::init()
{
    subscribeOnChanges();

    uiState()->toolConfigChanged(TOOLBAR_NAME).onNotify(this, [this]() {
        load();
    });

    configuration()->caretGridTicksChanged().onNotify(this, [this]() {
        updateCaretGridItems();
        emit caretGridChanged();
    });

    commandsController()->isNoteInputAllowedChanged().onReceive(this, [this](bool) {
        emit isInputAllowedChanged();
    });

    load();
}

const muse::ui::ToolConfig& NoteInputBarModel::defaultNoteInputConfig()
{
    static ToolConfig config;
    if (!config.isValid()) {
        config.items = {
            { TOGGLE_NOTE_INPUT_BY_NOTE_NAME_COMMAND, true },
            { TOGGLE_NOTE_INPUT_BY_DURATION_COMMAND, true },
            { TOGGLE_NOTE_INPUT_CARET_COMMAND, true },
            { TOGGLE_NOTE_INPUT_RHYTHM_COMMAND, false },
            { TOGGLE_NOTE_INPUT_REPITCH_COMMAND, false },
            { TOGGLE_NOTE_INPUT_REALTIME_AUTO_COMMAND, false },
            { TOGGLE_NOTE_INPUT_REALTIME_MANUAL_COMMAND, false },
            { TOGGLE_NOTE_INPUT_TIMEWISE_COMMAND, false },
            { ToolConfig::____________, true },
            { SET_DURATION_1024TH_COMMAND, false },
            { SET_DURATION_512TH_COMMAND, false },
            { SET_DURATION_256TH_COMMAND, false },
            { SET_DURATION_128TH_COMMAND, false },
            { SET_DURATION_64TH_COMMAND, true },
            { SET_DURATION_32ND_COMMAND, true },
            { SET_DURATION_16TH_COMMAND, true },
            { SET_DURATION_EIGHTH_COMMAND, true },
            { SET_DURATION_QUARTER_COMMAND, true },
            { SET_DURATION_HALF_COMMAND, true },
            { SET_DURATION_WHOLE_COMMAND, true },
            { SET_DURATION_BREVE_COMMAND, false },
            { SET_DURATION_LONGA_COMMAND, false },
            { ToolConfig::____________, true },
            { TOGGLE_DOT_COMMAND, true },
            { TOGGLE_DOT2_COMMAND, false },
            { TOGGLE_DOT3_COMMAND, false },
            { TOGGLE_DOT4_COMMAND, false },
            { TOGGLE_REST_COMMAND, true },
            { ToolConfig::____________, true },
            { TOGGLE_FLAT2_COMMAND, true },
            { TOGGLE_FLAT_COMMAND, true },
            { TOGGLE_NAT_COMMAND, true },
            { TOGGLE_SHARP_COMMAND, true },
            { TOGGLE_SHARP2_COMMAND, true },
            { ToolConfig::____________, true },
            { TOGGLE_TIE_COMMAND, true },
            { ADD_SLUR_COMMAND, true },
            { TOGGLE_LV_COMMAND, false },
            { ToolConfig::____________, true },
            { TOGGLE_MARCATO_COMMAND, true },
            { TOGGLE_SFORZATO_COMMAND, true },
            { TOGGLE_TENUTO_COMMAND, true },
            { TOGGLE_STACCATO_COMMAND, true },
            { ToolConfig::____________, true },
            { CROSS_STAFF_BEAMING_SUBITEMS, true }, // service
            { TUPLET_SUBITEMS, true },              // service
            { FLIP_COMMAND, true },
            { ToolConfig::____________, true },
            { USE_VOICE_1_COMMAND, true },
            { USE_VOICE_2_COMMAND, true },
            { USE_VOICE_3_COMMAND, false },
            { USE_VOICE_4_COMMAND, false }
        };
    }
    return config;
}

void NoteInputBarModel::load()
{
    MenuItemList items;

    ToolConfig noteInputConfig = uiState()->toolConfig(TOOLBAR_NAME, NoteInputBarModel::defaultNoteInputConfig());

    int section = 0;
    for (const ToolConfig::Item& citem : noteInputConfig.items) {
        if (!citem.show) {
            continue;
        }

        if (citem.isSeparator()) {
            section++;
            continue;
        }

        if (citem.intent == CROSS_STAFF_BEAMING_SUBITEMS) {
            MenuItem* item = makeServiceItem(CROSS_STAFF_BEAMING_SUBITEMS, QString::number(section));
            item->setSubitems(makeCrossStaffBeamingItems());
            items << item;
        } else if (citem.intent == TUPLET_SUBITEMS) {
            MenuItem* item = makeServiceItem(TUPLET_SUBITEMS, QString::number(section));
            item->setSubitems(makeTupletItems());
            items << item;
        } else {
            MenuItem* item = makeCommandItem(rcommand::Command(citem.intent), QString::number(section));
            if (rcommand::Command(citem.intent) == TOGGLE_NOTE_INPUT_CARET_COMMAND) {
                // icon with an attached dropdown: the rhythmic grid resolution
                item->setSubitems(makeCaretGridItems());
                items << item;

                // only a horizontal toolbar has room for a wide button
                if (m_horizontal) {
                    MenuItem* spacer = new MenuItem(this);
                    spacer->setId(CARET_GRID_SPACER_ID);
                    spacer->setSection(QString::number(section));
                    items << spacer;
                }
                continue;
            }
            items << item;
        }
    }

    items << makeAddItem(QString::number(++section));
    setItems(items);
}

bool NoteInputBarModel::isInputAllowed() const
{
    return commandsController()->isNoteInputAllowed();
}

NoteInputBarModel::ServiceItemInfo NoteInputBarModel::serviceItemInfo(const std::string& intent)
{
    if (intent == CROSS_STAFF_BEAMING_SUBITEMS) {
        return { TranslatableString("action", "Cross-staff beaming"), IconCode::Code::CROSS_STAFF_BEAMING };
    } else if (intent == TUPLET_SUBITEMS) {
        return { TranslatableString("action", "Tuplet"), IconCode::Code::NOTE_TUPLET };
    }
    return { };
}

MenuItem* NoteInputBarModel::makeServiceItem(const std::string& intent, const QString& section)
{
    const ServiceItemInfo info = serviceItemInfo(intent);
    MenuItem* item = new MenuItem(this);
    item->setTitle(info.title);
    item->setIcon(info.icon);
    item->setSection(section);
    return item;
}

MenuItem* NoteInputBarModel::makeCommandItem(const muse::rcommand::Command& command, const QString& section)
{
    const rcommand::CommandInfo info = commandsRegister()->commandInfo(command);
    MenuItem* item = new MenuItem(info, this);
    item->setSection(section);
    return item;
}

MenuItem* NoteInputBarModel::makeAddItem(const QString& section)
{
    MenuItem* item = new MenuItem(this);
    item->setTitle(TranslatableString("global", "Add"));
    item->setIcon(IconCode::Code::PLUS);
    item->setSection(section);
    item->setSubitems(makeAddItems());
    return item;
}

MenuItemList NoteInputBarModel::makeCrossStaffBeamingItems()
{
    MenuItemList items {
        makeMenuItem(MOVE_UP_COMMAND),
        makeMenuItem(MOVE_DOWN_COMMAND)
    };

    return items;
}

static const CaretGridValue* currentCaretGridValue(int ticks)
{
    for (const CaretGridValue& value : CARET_GRID_VALUES) {
        if (caretGridValueTicks(value) == ticks) {
            return &value;
        }
    }
    return nullptr;
}

bool NoteInputBarModel::horizontal() const
{
    return m_horizontal;
}

void NoteInputBarModel::setHorizontal(bool horizontal)
{
    if (m_horizontal == horizontal) {
        return;
    }

    m_horizontal = horizontal;
    emit horizontalChanged();

    load();
}

int NoteInputBarModel::caretGridIcon() const
{
    const CaretGridValue* value = currentCaretGridValue(configuration()->caretGridTicks());
    return static_cast<int>(value ? value->icon : IconCode::Code::NOTE_8TH);
}

bool NoteInputBarModel::caretGridDotted() const
{
    const CaretGridValue* value = currentCaretGridValue(configuration()->caretGridTicks());
    return value && value->dots > 0;
}

void NoteInputBarModel::handleMenuItem(const QString& itemId)
{
    if (itemId.startsWith(CARET_GRID_ITEM_PREFIX)) {
        const int ticks = itemId.mid(CARET_GRID_ITEM_PREFIX.size()).toInt();
        if (ticks > 0) {
            configuration()->setCaretGridTicks(ticks);
        }
        return;
    }

    AbstractMenuModel::handleMenuItem(itemId);
}

MenuItemList NoteInputBarModel::makeCaretGridItems()
{
    const int currentTicks = configuration()->caretGridTicks();

    MenuItemList items;
    for (const CaretGridValue& value : CARET_GRID_VALUES) {
        const int ticks = caretGridValueTicks(value);
        MenuItem* item = new MenuItem(this);
        item->setId(CARET_GRID_ITEM_PREFIX + QString::number(ticks));
        item->setTitle(TranslatableString("notation", value.title));
        item->setIcon(value.icon);
        item->setCheckable(true);
        item->setChecked(ticks == currentTicks);
        items << item;
    }

    return items;
}

void NoteInputBarModel::updateCaretGridItems()
{
    const QString currentId = CARET_GRID_ITEM_PREFIX + QString::number(configuration()->caretGridTicks());

    for (MenuItem* item : items()) {
        if (item->subitems().empty() || !item->subitems().front()->id().startsWith(CARET_GRID_ITEM_PREFIX)) {
            continue;
        }
        for (MenuItem* subitem : item->subitems()) {
            subitem->setChecked(subitem->id() == currentId);
        }
    }
}

MenuItemList NoteInputBarModel::makeTupletItems()
{
    MenuItemList items {
        makeMenuItem(ADD_DUPLET_COMMAND),
        makeMenuItem(ADD_TRIPLET_COMMAND),
        makeMenuItem(ADD_QUADRUPLET_COMMAND),
        makeMenuItem(ADD_QUINTUPLET_COMMAND),
        makeMenuItem(ADD_SEXTUPLET_COMMAND),
        makeMenuItem(ADD_SEPTUPLET_COMMAND),
        makeMenuItem(ADD_OCTUPLET_COMMAND),
        makeMenuItem(ADD_NONUPLET_COMMAND),
        makeMenuItem(OPEN_TUPLET_CONFIGURE_COMMAND)
    };

    return items;
}

MenuItemList NoteInputBarModel::makeAddItems()
{
    MenuItemList items {
        makeMenu(TranslatableString("notation", "Notes"), makeNotesItems()),
        makeMenu(TranslatableString("notation", "Intervals"), makeIntervalsItems()),
        makeMenu(TranslatableString("notation", "Measures"), makeMeasuresItems()),
        makeMenu(TranslatableString("notation", "Frames"), makeFramesItems()),
        makeMenu(TranslatableString("notation", "Text"), makeTextItems()),
        makeMenu(TranslatableString("notation", "Lines"), makeLinesItems()),
        makeMenu(TranslatableString("notation", "Chords and fretboard diagrams"), makeChordAndFretboardDiagramsItems()),
    };

    return items;
}

MenuItemList NoteInputBarModel::makeNotesItems()
{
    MenuItemList items {
        makeMenuItem(ENTER_NOTE_C_COMMAND),
        makeMenuItem(ENTER_NOTE_D_COMMAND),
        makeMenuItem(ENTER_NOTE_E_COMMAND),
        makeMenuItem(ENTER_NOTE_F_COMMAND),
        makeMenuItem(ENTER_NOTE_G_COMMAND),
        makeMenuItem(ENTER_NOTE_A_COMMAND),
        makeMenuItem(ENTER_NOTE_B_COMMAND),
        makeSeparator(),
        makeMenuItem(ADD_NOTE_C_COMMAND),
        makeMenuItem(ADD_NOTE_D_COMMAND),
        makeMenuItem(ADD_NOTE_E_COMMAND),
        makeMenuItem(ADD_NOTE_F_COMMAND),
        makeMenuItem(ADD_NOTE_G_COMMAND),
        makeMenuItem(ADD_NOTE_A_COMMAND),
        makeMenuItem(ADD_NOTE_B_COMMAND)
    };

    return items;
}

MenuItemList NoteInputBarModel::makeIntervalsItems()
{
    MenuItemList items {
        makeMenuItem(ADD_INTERVAL_PLUS_1_COMMAND),
        makeMenuItem(ADD_INTERVAL_PLUS_2_COMMAND),
        makeMenuItem(ADD_INTERVAL_PLUS_3_COMMAND),
        makeMenuItem(ADD_INTERVAL_PLUS_4_COMMAND),
        makeMenuItem(ADD_INTERVAL_PLUS_5_COMMAND),
        makeMenuItem(ADD_INTERVAL_PLUS_6_COMMAND),
        makeMenuItem(ADD_INTERVAL_PLUS_7_COMMAND),
        makeMenuItem(ADD_INTERVAL_PLUS_8_COMMAND),
        makeMenuItem(ADD_INTERVAL_PLUS_9_COMMAND),
        makeMenuItem(ADD_INTERVAL_PLUS_10_COMMAND),
        makeSeparator(),
        makeMenuItem(ADD_INTERVAL_MINUS_2_COMMAND),
        makeMenuItem(ADD_INTERVAL_MINUS_3_COMMAND),
        makeMenuItem(ADD_INTERVAL_MINUS_4_COMMAND),
        makeMenuItem(ADD_INTERVAL_MINUS_5_COMMAND),
        makeMenuItem(ADD_INTERVAL_MINUS_6_COMMAND),
        makeMenuItem(ADD_INTERVAL_MINUS_7_COMMAND),
        makeMenuItem(ADD_INTERVAL_MINUS_8_COMMAND),
        makeMenuItem(ADD_INTERVAL_MINUS_9_COMMAND),
        makeMenuItem(ADD_INTERVAL_MINUS_10_COMMAND)
    };

    return items;
}

MenuItemList NoteInputBarModel::makeMeasuresItems()
{
    MenuItemList items {
        makeMenuItem(INSERT_MEASURES_COMMAND),
        makeMenuItem(APPEND_MEASURES_COMMAND),
        makeSeparator(),
        makeMenuItem(INSERT_MEASURES_COMMAND),
        makeMenuItem(INSERT_MEASURES_AFTER_SELECTION_COMMAND),
        makeSeparator(),
        makeMenuItem(INSERT_MEASURES_AT_START_OF_SCORE_COMMAND),
        makeMenuItem(APPEND_MEASURES_COMMAND)
    };

    return items;
}

MenuItemList NoteInputBarModel::makeFramesItems()
{
    MenuItemList items {
        makeMenuItem(INSERT_HBOX_COMMAND),
        makeMenuItem(INSERT_VBOX_COMMAND),
        makeMenuItem(INSERT_TEXTFRAME_COMMAND),
        makeMenuItem(INSERT_FRETFRAME_COMMAND),
        makeSeparator(),
        makeMenu(TranslatableString("notation", "Insert at end of score"), {
            makeMenuItem(APPEND_HBOX_COMMAND),
            makeMenuItem(APPEND_VBOX_COMMAND),
            makeMenuItem(APPEND_TEXTFRAME_COMMAND),
            makeMenuItem(APPEND_FRETFRAME_COMMAND)
        })
    };

    return items;
}

MenuItemList NoteInputBarModel::makeTextItems()
{
    MenuItemList items {
        makeMenuItem(ADD_TITLE_TEXT_COMMAND),
        makeMenuItem(ADD_SUBTITLE_TEXT_COMMAND),
        makeMenuItem(ADD_COMPOSER_TEXT_COMMAND),
        makeMenuItem(ADD_LYRICIST_TEXT_COMMAND),
        makeMenuItem(ADD_PART_TEXT_COMMAND),
        makeSeparator(),
        makeMenuItem(ADD_SYSTEM_TEXT_COMMAND),
        makeMenuItem(ADD_STAFF_TEXT_COMMAND),
        makeMenuItem(ADD_DYNAMIC_COMMAND),
        makeMenuItem(ADD_EXPRESSION_TEXT_COMMAND),
        makeMenuItem(ADD_REHEARSALMARK_TEXT_COMMAND),
        makeMenuItem(ADD_INSTRUMENT_CHANGE_TEXT_COMMAND),
        makeMenuItem(ADD_FINGERING_TEXT_COMMAND),
        makeSeparator(),
        makeMenuItem(ADD_STICKING_TEXT_COMMAND),
        makeMenuItem(ADD_CHORD_TEXT_COMMAND),
        makeMenuItem(ADD_ROMAN_NUMERAL_TEXT_COMMAND),
        makeMenuItem(ADD_NASHVILLE_NUMBER_TEXT_COMMAND),
        makeMenuItem(ADD_LYRICS_COMMAND),
        makeMenuItem(ADD_FIGURED_BASS_COMMAND),
        makeMenuItem(ADD_TEMPO_COMMAND)
    };

    return items;
}

MenuItemList NoteInputBarModel::makeLinesItems()
{
    MenuItemList items {
        makeMenuItem(ADD_SLUR_COMMAND),
        makeMenuItem(ADD_HAIRPIN_COMMAND),
        makeMenuItem(ADD_HAIRPIN_REVERSE_COMMAND),
        makeMenuItem(ADD_OTTAVA_8VA_COMMAND),
        makeMenuItem(ADD_OTTAVA_8VB_COMMAND),
        makeMenuItem(ADD_NOTELINE_COMMAND)
    };

    return items;
}

MenuItemList NoteInputBarModel::makeChordAndFretboardDiagramsItems()
{
    MenuItemList items {
        makeMenuItem(ADD_CHORD_TEXT_COMMAND),
        makeMenuItem(ADD_FRETBOARD_DIAGRAM_COMMAND),
        makeSeparator(),
        makeMenuItem(INSERT_FRETFRAME_COMMAND, TranslatableString("notation", "Fretboard diagram legend"))
    };

    return items;
}
