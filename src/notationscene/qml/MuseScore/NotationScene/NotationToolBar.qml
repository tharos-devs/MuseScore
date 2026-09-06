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
import QtQuick.Layouts

import Muse.UiComponents

import MuseScore.NotationScene

RowLayout {
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

        navigationPanel.name: "NotationToolBar"
        navigationPanel.accessible.name: qsTrc("notation", "Notation toolbar")

        spacing: 2

        model: toolBarModel
    }

    SplitButton {
        id: automationButton

        readonly property var itemData: toolBarModel.automationItem

        Layout.preferredHeight: 32

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
    }
}
