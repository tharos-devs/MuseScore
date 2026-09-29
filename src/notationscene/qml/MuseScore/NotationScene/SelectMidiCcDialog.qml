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
import MuseScore.NotationScene

StyledDialogView {
    id: root

    title: qsTrc("notation", "Other MIDI CC")

    contentWidth: 320
    contentHeight: 420
    margins: 16

    modal: true

    // Preselected controller, passed by the caller
    property int controller: 1

    MidiCcSelectModel {
        id: selectModel
    }

    TextMetrics {
        id: numberMetrics
        font: ui.theme.bodyFont
        text: "CC000"
    }

    QtObject {
        id: prv

        readonly property var allControllers: selectModel.controllers()
        property var shownControllers: allControllers

        function applyFilter(text) {
            const filter = text.trim().toLowerCase()
            shownControllers = filter === "" ? allControllers
                                             : allControllers.filter(function(item) {
                                                 return item.title.toLowerCase().indexOf(filter) !== -1
                                                        || String(item.number) === filter
                                             })

            // OK must never pick a controller the filter hides: fall back to the first one shown
            const isSelectionShown = shownControllers.some(function(item) { return item.number === root.controller })
            if (!isSelectionShown) {
                root.controller = shownControllers.length > 0 ? shownControllers[0].number : -1
            }
        }

        function accept() {
            if (root.controller < 0) {
                return
            }
            root.ret = { errcode: 0, value: root.controller }
            root.hide()
        }
    }

    onNavigationActivateRequested: {
        searchField.navigation.requestActive()
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: 12

        NavigationPanel {
            id: navPanel
            name: "SelectMidiCcPanel"
            section: root.navigationSection
            order: 1
            direction: NavigationPanel.Vertical
        }

        SearchField {
            id: searchField

            Layout.fillWidth: true

            hint: qsTrc("notation", "Search MIDI CC")

            navigation.name: "SelectMidiCcSearchField"
            navigation.panel: navPanel
            navigation.order: 0

            onSearchTextChanged: {
                prv.applyFilter(searchText)
            }
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

                model: prv.shownControllers

                Component.onCompleted: {
                    positionViewAtIndex(Math.max(root.controller, 0), ListView.Center)
                }

                delegate: ListItemBlank {
                    required property var modelData
                    required property int index

                    width: listView.width
                    height: 30

                    isSelected: modelData.number === root.controller

                    navigation.name: "MidiCc" + modelData.number
                    navigation.panel: navPanel
                    navigation.order: index + 1

                    Row {
                        anchors.left: parent.left
                        anchors.leftMargin: 12
                        anchors.right: parent.right
                        anchors.rightMargin: 12
                        anchors.verticalCenter: parent.verticalCenter
                        spacing: 12

                        // Fixed width, so the names line up whatever the number
                        StyledTextLabel {
                            width: numberMetrics.advanceWidth
                            horizontalAlignment: Text.AlignLeft
                            text: modelData.numberText
                        }

                        StyledTextLabel {
                            horizontalAlignment: Text.AlignLeft
                            text: modelData.name
                        }
                    }

                    onClicked: {
                        root.controller = modelData.number
                    }

                    onDoubleClicked: {
                        root.controller = modelData.number
                        prv.accept()
                    }
                }
            }
        }

        ButtonBox {
            Layout.fillWidth: true
            Layout.alignment: Qt.AlignRight | Qt.AlignBottom

            buttons: [ ButtonBoxModel.Cancel ]

            navigationPanel.section: root.navigationSection
            navigationPanel.order: 2

            FlatButton {
                text: qsTrc("global", "OK")
                buttonRole: ButtonBoxModel.AcceptRole
                buttonId: ButtonBoxModel.Ok
                accentButton: true
                enabled: root.controller >= 0

                onClicked: {
                    prv.accept()
                }
            }

            onStandardButtonClicked: function(buttonId) {
                if (buttonId === ButtonBoxModel.Cancel) {
                    root.reject()
                }
            }
        }
    }
}
