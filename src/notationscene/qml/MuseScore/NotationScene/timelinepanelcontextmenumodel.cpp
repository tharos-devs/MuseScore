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

#include "timelinepanelcontextmenumodel.h"

#include "actions/actiontypes.h"
#include "types/translatablestring.h"

#include "notationscenetypes.h"

using namespace mu::notation;
using namespace muse;
using namespace muse::actions;
using namespace muse::ui;
using namespace muse::uicomponents;

static const ActionCode TOGGLE_ROW_CODE("timeline-toggle-row");

TimelinePanelContextMenuModel::TimelinePanelContextMenuModel(QObject* parent)
    : AbstractMenuModel(parent)
{
    setObjectName("TimelinePanelContextMenuModel");
}

void TimelinePanelContextMenuModel::load()
{
    m_rowItems.clear();

    //! NOTE: same titles (and translation context) as the Timeline's own row labels
    const std::vector<std::pair<TranslatableString, std::string> > rows {
        { TranslatableString("notation/timeline", "Video"), TIMELINE_ROW_VIDEO },
        { TranslatableString("notation/timeline", "Measures"), TIMELINE_ROW_MEASURES },
        { TranslatableString("notation/timeline", "Tempo"), TIMELINE_ROW_TEMPO },
        { TranslatableString("notation/timeline", "Time signature"), TIMELINE_ROW_TIME_SIGNATURE },
        { TranslatableString("notation/timeline", "Timecode"), TIMELINE_ROW_TIMECODE },
        { TranslatableString("notation/timeline", "Hit points"), TIMELINE_ROW_HIT_POINTS },
        { TranslatableString("notation/timeline", "Rehearsal mark"), TIMELINE_ROW_REHEARSAL_MARK },
        { TranslatableString("notation/timeline", "Key signature"), TIMELINE_ROW_KEY_SIGNATURE },
        { TranslatableString("notation/timeline", "Barlines"), TIMELINE_ROW_BARLINES },
        { TranslatableString("notation/timeline", "Jumps and markers"), TIMELINE_ROW_JUMPS_AND_MARKERS },
    };

    MenuItemList viewItems;
    for (const auto& [title, rowId] : rows) {
        viewItems << makeRowItem(title, rowId);
    }

    setItems({ makeMenu(TranslatableString("notation", "View"), viewItems) });

    configuration()->timelineRowsVisibilityChanged().onNotify(this, [this]() {
        updateRowItems();
    });
}

void TimelinePanelContextMenuModel::handleMenuItem(const QString& itemId)
{
    MenuItem& item = findItem(itemId);
    if (item.action().code == TOGGLE_ROW_CODE) {
        const std::string rowId = itemId.toStdString();
        configuration()->setTimelineRowVisible(rowId, !configuration()->isTimelineRowVisible(rowId));
    } else {
        AbstractMenuModel::handleMenuItem(itemId);
    }
}

MenuItem* TimelinePanelContextMenuModel::makeRowItem(const TranslatableString& title, const std::string& rowId)
{
    UiAction action;
    action.title = title;
    action.code = TOGGLE_ROW_CODE;
    action.checkable = Checkable::Yes;

    MenuItem* item = new MenuItem(action, this);
    item->setId(QString::fromStdString(rowId));
    item->setState(UiActionState::make_enabled(configuration()->isTimelineRowVisible(rowId)));

    m_rowItems << item;

    return item;
}

void TimelinePanelContextMenuModel::updateRowItems()
{
    for (MenuItem* item : m_rowItems) {
        const bool checked = configuration()->isTimelineRowVisible(item->id().toStdString());
        item->setState(UiActionState::make_enabled(checked));
    }
}
