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
import QtQuick.Layouts

import Muse.Ui
import Muse.UiComponents
import MuseScore.NotationScene

// The articulation lane's column picker (see ArticulationPickerModel), opened where the lane was clicked
Item {
    id: root

    property ArticulationPickerModel model: null

    readonly property int rowHeight: 24
    // Up to 7 columns: smaller than the rest of the UI to keep the window compact
    readonly property real zoom: 0.9
    readonly property int maxColumnHeight: 16 * rowHeight

    x: root.model ? root.model.position.x : 0
    y: root.model ? root.model.position.y : 0
    width: 1
    height: 1

    function columnWidth(items) {
        let widest = 0
        for (let i = 0; i < items.length; ++i) {
            widest = Math.max(widest, fontMetrics.advanceWidth(items[i].name))
        }
        // the text's margins and the folder arrow
        return Math.min(260, Math.max(120, Math.ceil(widest) + 48))
    }

    FontMetrics {
        id: fontMetrics
        font: ui.theme.bodyFont
    }

    Connections {
        target: root.model

        function onIsOpenChanged() {
            if (root.model.isOpen) {
                popup.open()
            } else if (popup.isOpened) {
                popup.close()
            }
        }
    }

    StyledPopupView {
        id: popup

        placementPolicies: PopupView.PreferBelow
        showArrow: false
        x: 0
        y: 0
        margins: 4

        contentWidth: zoomContainer.width
        contentHeight: zoomContainer.height

        onOpened: {
            escapeHandler.forceActiveFocus()
        }

        onClosed: {
            if (root.model) {
                root.model.close()
            }
        }

        // The app's shortcuts only work in the main window: this window handles Escape itself
        Item {
            id: escapeHandler

            focus: true

            Keys.onShortcutOverride: function(event) {
                event.accepted = (event.key === Qt.Key_Escape)
            }

            Keys.onEscapePressed: {
                popup.close()
            }
        }

        ZoomContainer {
            id: zoomContainer

            zoom: root.zoom
            width: Math.ceil(content.implicitWidth * root.zoom)
            height: Math.ceil(content.implicitHeight * root.zoom)

            ColumnLayout {
                id: content

                anchors.fill: parent
                spacing: 0

                RowLayout {
                    spacing: 2
                    Layout.fillWidth: true

                    // The articulation last placed on this track, to repeat it in one click
                    ListItemBlank {
                        enabled: Boolean(root.model) && root.model.lastPlacedTitle !== ""
                        Layout.fillWidth: true
                        implicitWidth: lastPlacedLabel.implicitWidth + 16
                        implicitHeight: root.rowHeight

                        StyledTextLabel {
                            id: lastPlacedLabel

                            anchors.fill: parent
                            anchors.leftMargin: 8
                            anchors.rightMargin: 8
                            horizontalAlignment: Text.AlignLeft
                            text: root.model ? root.model.lastPlacedTitle : ""
                            font.italic: true
                        }

                        onClicked: root.model.chooseLastPlaced()
                    }

                    FlatButton {
                        Layout.preferredWidth: root.rowHeight
                        Layout.preferredHeight: root.rowHeight
                        transparent: true
                        icon: IconCode.EDIT
                        toolTipTitle: qsTrc("notation", "Edit articulation map")

                        onClicked: root.model.editMap()
                    }

                    FlatButton {
                        visible: Boolean(root.model) && root.model.hasInstrumentEditor
                        Layout.preferredWidth: root.rowHeight
                        Layout.preferredHeight: root.rowHeight
                        transparent: true
                        icon: IconCode.PLUGIN
                        toolTipTitle: qsTrc("notation", "Open instrument")

                        onClicked: root.model.openInstrumentEditor()
                    }
                }

                SeparatorLine {
                    Layout.topMargin: 4
                    Layout.bottomMargin: 4
                }

                RowLayout {
                    spacing: 0

                    Repeater {
                        model: root.model ? root.model.columns : []

                        delegate: RowLayout {
                            id: columnDelegate

                            required property var modelData
                            required property int index

                            spacing: 0
                            Layout.alignment: Qt.AlignTop

                            SeparatorLine {
                                visible: columnDelegate.index > 0
                                orientation: Qt.Vertical
                                Layout.fillHeight: true
                                Layout.leftMargin: 4
                                Layout.rightMargin: 4
                            }

                            StyledListView {
                                Layout.alignment: Qt.AlignTop
                                Layout.preferredWidth: root.columnWidth(columnDelegate.modelData)
                                Layout.preferredHeight: Math.min(contentHeight, root.maxColumnHeight)

                                model: columnDelegate.modelData

                                delegate: ListItemBlank {
                                    id: itemDelegate

                                    required property var modelData

                                    height: root.rowHeight
                                    isSelected: itemDelegate.modelData.isSelected

                                    StyledTextLabel {
                                        anchors.left: parent.left
                                        anchors.leftMargin: 8
                                        anchors.right: folderArrow.left
                                        anchors.rightMargin: 4
                                        anchors.verticalCenter: parent.verticalCenter
                                        horizontalAlignment: Text.AlignLeft
                                        text: itemDelegate.modelData.name
                                    }

                                    StyledIconLabel {
                                        id: folderArrow

                                        anchors.right: parent.right
                                        anchors.rightMargin: 6
                                        anchors.verticalCenter: parent.verticalCenter
                                        visible: itemDelegate.modelData.isFolder
                                        iconCode: IconCode.SMALL_ARROW_RIGHT
                                    }

                                    onClicked: {
                                        if (itemDelegate.modelData.isFolder) {
                                            root.model.openFolder(columnDelegate.index, itemDelegate.modelData.name)
                                        } else {
                                            root.model.chooseEntry(itemDelegate.modelData.entryId)
                                        }
                                    }
                                }
                            }
                        }
                    }
                }

                SeparatorLine {
                    Layout.topMargin: 4
                    Layout.bottomMargin: 4
                }

                // This note only, Reset position, Remove articulation change...
                Repeater {
                    model: root.model ? root.model.actions : []

                    delegate: ListItemBlank {
                        id: actionDelegate

                        required property var modelData
                        required property int index

                        Layout.fillWidth: true
                        implicitWidth: checkIcon.width + actionLabel.implicitWidth + 18
                        implicitHeight: root.rowHeight
                        enabled: actionDelegate.modelData.enabled

                        StyledIconLabel {
                            id: checkIcon

                            anchors.left: parent.left
                            anchors.leftMargin: 6
                            anchors.verticalCenter: parent.verticalCenter
                            width: 16
                            iconCode: actionDelegate.modelData.checkable && actionDelegate.modelData.checked
                                      ? IconCode.TICK_RIGHT_ANGLE : IconCode.NONE
                        }

                        StyledTextLabel {
                            id: actionLabel

                            anchors.left: checkIcon.right
                            anchors.leftMargin: 4
                            anchors.right: parent.right
                            anchors.rightMargin: 8
                            anchors.verticalCenter: parent.verticalCenter
                            horizontalAlignment: Text.AlignLeft
                            text: actionDelegate.modelData.title
                        }

                        onClicked: root.model.triggerAction(actionDelegate.index)
                    }
                }
            }
        }
    }
}
