/*
 * SPDX-License-Identifier: GPL-3.0-only
 * MuseScore-Studio-CLA-applies
 *
 * MuseScore Studio
 * Music Composition & Notation
 *
 * Copyright (C) 2024 MuseScore Limited and others
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

pragma ComponentBehavior: Bound

import QtQuick

import Muse.Ui
import Muse.UiComponents
import MuseScore.NotationScene

Item {
    id: root

    required property var model
    required property int panelWidth

    property NavigationSection navigationSection: null
    property int navigationOrderStart: 1

    QtObject {
        id: prv
        // The central buttons are placed centrally relative to the panel, not the toolbar area
        readonly property int tabAreaLength: root.panelWidth - root.width
        readonly property int centerX: (root.panelWidth / 2) - prv.tabAreaLength

        readonly property int spacing: 4
    }

    NavigationPanel {
        id: centralNavPanel
        name: "PercussionPanelToolBarCentral"
        section: root.navigationSection
        order: root.navigationOrderStart
    }

    // a pad writes only while entering notes, else it's only heard (see PercussionPanelModel::onPadTriggered()):
    // no Write / Preview modes
    Row {
        id: centralButtonsRow

        anchors.verticalCenter: parent.verticalCenter
        x: prv.centerX - (centralButtonsRow.width / 2)

        spacing: prv.spacing

        // the three buttons as wide as the widest content
        readonly property real buttonWidth: Math.max(editPadButton.implicitWidth, layoutButton.implicitWidth,
                                                     customizeKitButton.implicitWidth)

        // as "Edit pad…" in a pad's menu: the pad of the note selected in the score, else the last one clicked, else the
        // first one
        FlatButton {
            id: editPadButton

            visible: root.model.currentPanelMode !== PanelMode.EDIT_LAYOUT
            width: centralButtonsRow.buttonWidth
            minWidth: 0
            enabled: root.model.enabled

            icon: IconCode.EDIT
            text: qsTrc("notation/percussion", "Edit pad")
            orientation: Qt.Horizontal

            navigation.panel: centralNavPanel
            navigation.row: 0

            onClicked: {
                root.model.editSelectedPad()
            }
        }

        // while editing the layout: next to Layout (its columns, reset...), in place of Edit pad
        FlatButton {
            id: finishEditingButton

            enabled: root.model.enabled

            visible: root.model.currentPanelMode === PanelMode.EDIT_LAYOUT
            text: qsTrc("notation/percussion", "Finish editing")
            orientation: Qt.Horizontal
            accentButton: true

            navigation.panel: centralNavPanel
            navigation.row: 0

            onClicked: {
                root.model.finishEditing()
            }
        }

        FlatButton {
            id: layoutButton

            width: centralButtonsRow.buttonWidth
            minWidth: 0
            enabled: root.model.enabled

            icon: IconCode.SPLIT_VIEW_HORIZONTAL
            text: qsTrc("notation/percussion", "Layout")
            orientation: Qt.Horizontal

            navigation.panel: centralNavPanel
            navigation.row: 1

            onClicked: {
                menuLoader.toggleOpened(root.model.layoutMenuItems)
            }

            StyledMenuLoader {
                id: menuLoader

                onHandleMenuItem: function(itemId) {
                    root.model.handleMenuItem(itemId)
                }
            }
        }

        FlatButton {
            id: customizeKitButton

            visible: root.model.currentPanelMode !== PanelMode.EDIT_LAYOUT
            width: centralButtonsRow.buttonWidth
            minWidth: 0
            enabled: root.model.enabled
            text: qsTrc("notation/percussion", "Customize kit")
            orientation: Qt.Horizontal

            navigation.panel: centralNavPanel
            navigation.row: 2

            onClicked: {
                root.model.customizeKit()
            }
        }
    }

}
