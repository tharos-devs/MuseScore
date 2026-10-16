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
import QtQuick
import QtQuick.Layouts

import Muse.Ui
import Muse.UiComponents

StyledDialogView {
    id: root

    title: qsTrc("notation", "Articulation map files changed")

    //! NOTE: fixed size, the list scrolls when many files changed
    contentWidth: 460
    contentHeight: 340
    margins: 16

    modal: true

    //! NOTE: passed by the caller: [{ path, fileName, parts }]
    property var maps: []

    //! NOTE: all checked: the files are usually what the user just edited
    property var checkedPaths: maps.map(function(map) { return map.path })

    QtObject {
        id: prv

        function isChecked(path) {
            return root.checkedPaths.indexOf(path) !== -1
        }

        function toggle(path) {
            const paths = root.checkedPaths.slice()
            const index = paths.indexOf(path)
            if (index === -1) {
                paths.push(path)
            } else {
                paths.splice(index, 1)
            }
            root.checkedPaths = paths
        }

        function accept() {
            root.ret = { errcode: 0, value: root.checkedPaths }
            root.hide()
        }
    }

    onNavigationActivateRequested: {
        //! NOTE: the delegates may not be created yet
        const firstItem = listView.itemAtIndex(0)
        if (firstItem) {
            firstItem.navigation.requestActive()
        }
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: 12

        NavigationPanel {
            id: navPanel
            name: "ChangedArticulationMapsPanel"
            section: root.navigationSection
            order: 1
            direction: NavigationPanel.Vertical
        }

        StyledTextLabel {
            Layout.fillWidth: true
            horizontalAlignment: Text.AlignLeft
            wrapMode: Text.WordWrap
            text: qsTrc("notation", "These articulation map files changed since this score was saved. Reload the checked ones? The others keep the versions saved with the score.")
        }

        Rectangle {
            Layout.fillWidth: true
            Layout.fillHeight: true

            color: ui.theme.textFieldColor
            border.color: ui.theme.strokeColor
            border.width: 1

            StyledListView {
                id: listView

                anchors.fill: parent
                anchors.margins: 1
                clip: true

                model: root.maps

                delegate: ListItemBlank {
                    id: mapItem

                    required property var modelData
                    required property int index

                    width: listView.width
                    height: 44

                    navigation.name: "ChangedArticulationMap" + index
                    navigation.panel: navPanel
                    navigation.order: index
                    navigation.accessible.role: MUAccessible.CheckBox
                    navigation.accessible.name: modelData.fileName
                    navigation.accessible.checked: prv.isChecked(modelData.path)

                    RowLayout {
                        anchors.left: parent.left
                        anchors.leftMargin: 12
                        anchors.right: parent.right
                        anchors.rightMargin: 12
                        anchors.verticalCenter: parent.verticalCenter
                        spacing: 10

                        CheckBox {
                            checked: prv.isChecked(mapItem.modelData.path)
                            onClicked: prv.toggle(mapItem.modelData.path)
                        }

                        ColumnLayout {
                            Layout.fillWidth: true
                            spacing: 2

                            StyledTextLabel {
                                Layout.fillWidth: true
                                horizontalAlignment: Text.AlignLeft
                                font: ui.theme.bodyBoldFont
                                text: mapItem.modelData.fileName
                            }

                            StyledTextLabel {
                                Layout.fillWidth: true
                                horizontalAlignment: Text.AlignLeft
                                visible: text !== ""
                                opacity: 0.7
                                text: mapItem.modelData.parts
                            }
                        }
                    }

                    onClicked: {
                        prv.toggle(modelData.path)
                    }
                }
            }
        }

        ButtonBox {
            Layout.fillWidth: true
            Layout.alignment: Qt.AlignRight | Qt.AlignBottom

            navigationPanel.section: root.navigationSection
            navigationPanel.order: 2

            FlatButton {
                text: qsTrc("notation", "Keep saved versions")
                buttonRole: ButtonBoxModel.RejectRole
                buttonId: ButtonBoxModel.CustomButton + 1

                onClicked: {
                    root.reject()
                }
            }

            FlatButton {
                text: qsTrc("notation", "Reload checked")
                buttonRole: ButtonBoxModel.AcceptRole
                buttonId: ButtonBoxModel.CustomButton + 2
                accentButton: true
                enabled: root.checkedPaths.length > 0

                onClicked: {
                    prv.accept()
                }
            }
        }
    }
}
