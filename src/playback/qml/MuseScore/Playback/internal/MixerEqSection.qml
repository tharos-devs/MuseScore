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

//! NOTE A thumbnail of each channel's EQ curve: a click opens its editor above it (one at a time: opening
//! another one, or clicking elsewhere, closes it), a right click its menu (on/off, reset)
MixerPanelSection {
    id: root

    headerTitle: qsTrc("playback", "EQ")

    Item {
        id: content

        required property MixerChannelItem channelItem

        height: thumbnail.height
        width: root.rowWidthFor(channelItem)

        property string accessibleName: (Boolean(root.needReadChannelName) ? channelItem.title + " " : "") + root.headerTitle

        Rectangle {
            id: thumbnail

            anchors.horizontalCenter: parent.horizontalCenter

            width: content.width - 12
            height: 36

            radius: 2
            color: "transparent"
            border.width: (thumbnailMouseArea.containsMouse || editorLoader.isOpened) ? 1 : 0
            border.color: ui.theme.accentColor

            EqCurveView {
                anchors.fill: parent
                anchors.margins: 1

                eq: content.channelItem.eq
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
                        contextMenuLoader.toggleOpened(content.contextMenuItems(), mouse.x, mouse.y)
                        return
                    }

                    editorLoader.toggleOpened()
                }
            }

            NavigationControl {
                id: navCtrl

                name: "EqThumbnail"
                enabled: thumbnail.visible
                panel: content.channelItem.panel
                row: root.navigationRowStart
                column: 0

                accessible.role: MUAccessible.Button
                accessible.name: content.accessibleName
                accessible.visualItem: thumbnail

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
                    channelItem: content.channelItem
                }
            }

            StyledMenuLoader {
                id: contextMenuLoader

                onHandleMenuItem: function(itemId) {
                    switch (itemId) {
                    case "toggle-eq":
                        content.channelItem.setEqEnabled(!content.channelItem.eq.enabled)
                        break
                    case "reset-eq":
                        content.channelItem.resetEq()
                        break
                    }
                }
            }
        }

        function contextMenuItems() {
            return [
                {
                    id: "toggle-eq",
                    title: content.channelItem.eq.enabled ? qsTrc("playback", "Disable EQ") : qsTrc("playback", "Enable EQ"),
                    enabled: true
                },
                {
                    id: "reset-eq",
                    title: qsTrc("playback", "Reset EQ"),
                    enabled: true
                }
            ]
        }
    }
}
