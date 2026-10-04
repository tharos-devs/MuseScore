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
#include "expressionmenumodel.h"

#include "types/translatablestring.h"

#include "notationscene/notationcommands.h"

using namespace mu::notation;
using namespace muse;
using namespace muse::ui;
using namespace muse::uicomponents;

namespace {
struct ExpressionEditorInfo {
    ExpressionEditor editor;
    rcommand::Command command;
    const char* title;
    IconCode::Code icon;
};

const std::vector<ExpressionEditorInfo>& editorInfos()
{
    static const std::vector<ExpressionEditorInfo> infos {
        { ExpressionEditor::NoteOffsets, TOGGLE_NOTE_OFFSET_EDITOR_COMMAND, "Note offset", IconCode::Code::CLOCK },
        { ExpressionEditor::NoteVelocities, TOGGLE_NOTE_VELOCITY_EDITOR_COMMAND, "Note velocities", IconCode::Code::WAVEFORM_HALFWAVE },
        { ExpressionEditor::Articulations, TOGGLE_ARTICULATION_MAP_EDITOR_COMMAND, "Articulations", IconCode::Code::ARTICULATION },
    };
    return infos;
}

const ExpressionEditorInfo& currentInfo(int editor)
{
    for (const ExpressionEditorInfo& info : editorInfos()) {
        if (static_cast<int>(info.editor) == editor) {
            return info;
        }
    }
    return editorInfos().front();
}
}

void ExpressionMenuModel::init()
{
    updateItems();

    // Keeps the items' checked state in sync with the editors (see AutomationTypeMenuModel::init())
    subscribeOnChanges();

    notationConfiguration()->currentExpressionEditorChanged().onNotify(this, [this]() {
        emit currentChanged();
    }, async::Asyncable::Mode::SetReplace);
}

void ExpressionMenuModel::updateItems()
{
    // setItems() doesn't delete the replaced items, and this runs on every dropdown open
    qDeleteAll(items());

    MenuItemList items;
    for (const ExpressionEditorInfo& info : editorInfos()) {
        MenuItem* item = makeMenuItem(info.command, TranslatableString::untranslatable(info.title));
        if (!item) {
            continue;
        }
        // A checkable item with an icon: the menu shows the checkmark in front of the icon
        item->setIcon(info.icon);
        // the command has no icon color: an empty one keeps the theme's (instead of black)
        item->setIconColor(QString());
        item->setCheckable(true);
        items << item;
    }

    setItems(items);
}

QString ExpressionMenuModel::currentTitle() const
{
    return QString::fromUtf8(currentInfo(notationConfiguration()->currentExpressionEditor()).title);
}

int ExpressionMenuModel::currentIcon() const
{
    return static_cast<int>(currentInfo(notationConfiguration()->currentExpressionEditor()).icon);
}

bool ExpressionMenuModel::currentIconIsLarge() const
{
    return currentInfo(notationConfiguration()->currentExpressionEditor()).editor == ExpressionEditor::Articulations;
}

QStringList ExpressionMenuModel::titles() const
{
    QStringList result;
    for (const ExpressionEditorInfo& info : editorInfos()) {
        result << QString::fromUtf8(info.title);
    }
    return result;
}
