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
import QtQuick

import Muse.UiComponents

import MuseScore.NotationScene

StyledToolBarView {
    property alias isCompactMode: toolBarModel.isCompactMode

    navigationPanel.name: "NotationToolBar"
    navigationPanel.accessible.name: qsTrc("notation", "Notation toolbar")

    spacing: 2

    NotationToolBarModel {
        id: toolBarModel
    }

    AutomationTypeMenuModel {
        id: automationTypeMenuModel

        Component.onCompleted: automationTypeMenuModel.init()
    }

    model: toolBarModel

    // "toggle-automation" is marked ToolBarItemType.USER_TYPE (see notationtoolbarmodel.cpp) so it
    // stays a normal item in the model's own order (keeping it correctly positioned between
    // "toggle-mixer" and "toggle-note-offset-editor" - the two were fighting for that spot when
    // automation was instead rendered as a separate sibling appended after this whole view), while
    // still getting its own delegate: a SplitButton whose dropdown arrow picks the automation type
    // directly, instead of a plain toggle-only button.
    sourceComponentCallback: function(type) {
        return type === ToolBarItemType.USER_TYPE ? automationButtonComponent : null
    }

    Component {
        id: automationButtonComponent

        SplitButton {
            id: control

            property var itemData

            icon: Boolean(itemData) ? itemData.icon : IconCode.NONE
            text: Boolean(itemData) && itemData.showTitle ? itemData.title : ""
            checked: Boolean(itemData) && itemData.checked
            enabled: Boolean(itemData) ? itemData.enabled : false

            toolTipTitle: Boolean(itemData) ? itemData.title : ""
            toolTipDescription: Boolean(itemData) ? itemData.description : ""

            menuItems: automationTypeMenuModel.items

            onClicked: {
                if (Boolean(itemData)) {
                    itemData.activate()
                }
            }

            onHandleMenuItem: function(itemId) {
                automationTypeMenuModel.handleMenuItem(itemId)
            }

            onAboutToOpenMenu: {
                // Refresh right before showing, rather than relying solely on the model's own
                // reactive updates - matches how the right-click "Automation type" submenu is
                // always rebuilt fresh on open, so it can't show a stale/no checkmark either.
                automationTypeMenuModel.init()
            }
        }
    }
}
