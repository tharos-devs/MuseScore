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
pragma ComponentBehavior: Bound

import QtQuick

import Muse.Ui
import Muse.UiComponents

import MuseScore.Playback

//! NOTE: a thumbnail of a channel's EQ curve (Mixer, Track list): a click opens its editor above it (one at a time:
//! opening another one, or clicking elsewhere, closes it), a right click its menu (on/off, reset)
Rectangle {
    id: root

    required property MixerChannelItem channelItem

    property NavigationPanel navigationPanel: channelItem?.panel ?? null
    property int navigationRowStart: 0
    property int navigationColumn: 0
    property string accessibleName: ""

    signal navigateControlIndexChanged(var index)

    width: 96
    height: 36

    radius: 2
    color: "transparent"
    border.width: (thumbnailMouseArea.containsMouse || editorLoader.isOpened) ? 1 : 0
    border.color: ui.theme.accentColor

    function contextMenuItems() {
        return [
            {
                id: "toggle-eq",
                title: root.channelItem.eq.enabled ? qsTrc("playback", "Disable EQ") : qsTrc("playback", "Enable EQ"),
                enabled: true
            },
            {
                id: "reset-eq",
                title: qsTrc("playback", "Reset EQ"),
                enabled: true
            }
        ]
    }

    EqCurveView {
        anchors.fill: parent
        anchors.margins: 1

        eq: root.channelItem?.eq ?? null
        editable: false

        backgroundColor: ui.theme.textFieldColor
        curveColor: ui.theme.accentColor
        gridColor: Utils.colorWithAlpha(ui.theme.fontPrimaryColor, 0.15)
        textColor: ui.theme.fontPrimaryColor
    }

    MouseArea {
        id: thumbnailMouseArea

        anchors.fill: parent

        hoverEnabled: true
        acceptedButtons: Qt.LeftButton | Qt.RightButton

        onClicked: function(mouse) {
            if (mouse.button === Qt.RightButton) {
                contextMenuLoader.toggleOpened(root.contextMenuItems(), mouse.x, mouse.y)
                return
            }

            editorLoader.toggleOpened()
        }
    }

    NavigationControl {
        id: navCtrl

        name: "EqThumbnail"
        enabled: root.visible
        panel: root.navigationPanel
        row: root.navigationRowStart
        column: root.navigationColumn

        accessible.role: MUAccessible.Button
        accessible.name: root.accessibleName
        accessible.visualItem: root

        onTriggered: editorLoader.toggleOpened()

        onActiveChanged: {
            if (active) {
                root.navigateControlIndexChanged({ row: row, column: column })
            }
        }
    }

    NavigationFocusBorder {
        navigationCtrl: navCtrl
    }

    StyledPopupLoader {
        id: editorLoader

        sourceComponent: MixerEqEditorPopup {
            channelItem: root.channelItem
        }
    }

    StyledMenuLoader {
        id: contextMenuLoader

        onHandleMenuItem: function(itemId) {
            switch (itemId) {
            case "toggle-eq":
                root.channelItem.setEqEnabled(!root.channelItem.eq.enabled)
                break
            case "reset-eq":
                root.channelItem.resetEq()
                break
            }
        }
    }
}
