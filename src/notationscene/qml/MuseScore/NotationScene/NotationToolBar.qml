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

// A plain Row, not RowLayout: StyledToolBarView already manages its own width/height
// internally, which conflicts with RowLayout's layout-managed sizing (that fight is what made
// the automation button render out of place) - Row just positions children using their own
// existing size, without renegotiating it.
Row {
    id: root

    property alias isCompactMode: toolBarModel.isCompactMode
    property alias navigationPanel: styledToolBarView.navigationPanel

    spacing: 4

    NotationToolBarModel {
        id: toolBarModel
    }

    AutomationTypeMenuModel {
        id: automationTypeMenuModel

        Component.onCompleted: automationTypeMenuModel.init()
    }

    StyledToolBarView {
        id: styledToolBarView

        anchors.verticalCenter: parent.verticalCenter

        navigationPanel.name: "NotationToolBar"
        navigationPanel.accessible.name: qsTrc("notation", "Notation toolbar")

        spacing: 2

        model: toolBarModel
    }

    SplitButton {
        id: automationButton

        anchors.verticalCenter: parent.verticalCenter

        readonly property var itemData: toolBarModel.automationItem

        // Not part of styledToolBarView's own Repeater (see notationtoolbarmodel.cpp), so it
        // never gets this wiring "for free" the way StyledToolBarView.qml's onLoaded does for
        // its own items - without it, this button is unreachable via keyboard/screen-reader
        // toolbar navigation.
        navigation.panel: styledToolBarView.navigationPanel
        navigation.row: -1
        navigation.column: 200

        icon: Boolean(itemData) ? itemData.icon : IconCode.NONE
        text: Boolean(itemData) && itemData.showTitle ? itemData.title : ""
        checked: Boolean(itemData) && itemData.checked
        enabled: Boolean(itemData) && itemData.enabled
        visible: Boolean(itemData)

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
            // reactive updates - matches how the right-click "Automation type" submenu is always
            // rebuilt fresh on open, so it can't show a stale/no checkmark either.
            automationTypeMenuModel.init()
        }
    }
}
