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
import MuseScore.Playback

StyledDialogView {
    id: root

    title: qsTrc("playback", "Track presets")

    contentWidth: 920
    contentHeight: 580

    modal: false
    resizable: false

    // the instrument it was opened for (see the Mixer's and the Track list's "Load track preset…")
    property string partId: ""
    property string instrumentId: ""

    Component.onCompleted: {
        presetsModel.load(partId, instrumentId)
    }

    onIsOpenedChanged: {
        // on by default: the window is used next to the score and the Mixer
        if (isOpened && !prv.alwaysOnTopInitialized) {
            prv.alwaysOnTopInitialized = true
            prv.setAlwaysOnTop(true)
        }
    }

    QtObject {
        id: prv

        property bool alwaysOnTop: false
        property bool alwaysOnTopInitialized: false

        function setAlwaysOnTop(on) {
            if (!root.window) {
                return
            }

            alwaysOnTop = on
            root.window.flags = on ? (root.window.flags | Qt.WindowStaysOnTopHint)
                                   : (root.window.flags & ~Qt.WindowStaysOnTopHint)
        }
    }

    TrackPresetsModel {
        id: presetsModel
    }

    //! NOTE: a column of names, one selected (the two grouping columns)
    component NamesColumn: ColumnLayout {
        id: namesColumn

        property string title: ""
        property var names: []
        property string selectedName: ""

        signal nameClicked(string name)

        spacing: 4

        StyledTextLabel {
            font: ui.theme.bodyBoldFont
            text: namesColumn.title
        }

        Rectangle {
            Layout.fillWidth: true
            Layout.fillHeight: true
            color: ui.theme.textFieldColor
            border.color: ui.theme.strokeColor
            border.width: 1

            StyledListView {
                id: namesView

                anchors.fill: parent
                anchors.margins: 1
                clip: true

                model: namesColumn.names

                delegate: Item {
                    id: nameRow

                    required property string modelData

                    width: namesView.width
                    height: 26

                    Rectangle {
                        anchors.fill: parent
                        color: nameRow.modelData === namesColumn.selectedName ? ui.theme.accentColor
                               : (nameMouseArea.containsMouse ? ui.theme.buttonColor : "transparent")
                        opacity: 0.5
                    }

                    StyledTextLabel {
                        anchors.left: parent.left
                        anchors.leftMargin: 8
                        anchors.right: parent.right
                        anchors.rightMargin: 8
                        anchors.verticalCenter: parent.verticalCenter
                        horizontalAlignment: Text.AlignLeft
                        elide: Text.ElideRight
                        text: nameRow.modelData
                    }

                    MouseArea {
                        id: nameMouseArea

                        anchors.fill: parent
                        hoverEnabled: true
                        onClicked: namesColumn.nameClicked(nameRow.modelData)
                    }
                }
            }
        }
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 12
        spacing: 12

        RowLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            spacing: 12

            // ---- the score's instruments
            // a child layout fills the width by default: the same width whatever the right side shows
            ColumnLayout {
                Layout.preferredWidth: 220
                Layout.fillWidth: false
                Layout.fillHeight: true
                spacing: 6

                StyledTextLabel {
                    font: ui.theme.bodyBoldFont
                    text: qsTrc("playback", "Instruments")
                }

                Rectangle {
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    color: ui.theme.textFieldColor
                    border.color: ui.theme.strokeColor
                    border.width: 1

                    StyledListView {
                        id: instrumentsView

                        anchors.fill: parent
                        anchors.margins: 1
                        clip: true

                        // a count: the list keeps its position when an instrument changes (see the model's NOTE)
                        model: presetsModel.instrumentCount

                        delegate: Item {
                            id: instrumentRow

                            required property int index
                            readonly property var modelData: {
                                presetsModel.instrumentsRevision // re-read when it changes
                                return presetsModel.instrumentAt(index)
                            }

                            width: instrumentsView.width
                            height: 36

                            Rectangle {
                                anchors.fill: parent
                                color: instrumentRow.modelData.selected ? ui.theme.accentColor
                                       : (instrumentMouseArea.containsMouse ? ui.theme.buttonColor : "transparent")
                                opacity: 0.5
                            }

                            Rectangle {
                                id: instrumentColor

                                anchors.left: parent.left
                                anchors.leftMargin: 6
                                anchors.verticalCenter: parent.verticalCenter
                                width: 6
                                height: 24
                                radius: 2
                                color: instrumentRow.modelData.hasColor ? instrumentRow.modelData.color : ui.theme.accentColor
                            }

                            Column {
                                anchors.left: instrumentColor.right
                                anchors.leftMargin: 8
                                anchors.right: parent.right
                                anchors.rightMargin: 6
                                anchors.verticalCenter: parent.verticalCenter

                                StyledTextLabel {
                                    width: parent.width
                                    horizontalAlignment: Text.AlignLeft
                                    elide: Text.ElideRight
                                    text: instrumentRow.modelData.name
                                }

                                StyledTextLabel {
                                    width: parent.width
                                    horizontalAlignment: Text.AlignLeft
                                    elide: Text.ElideRight
                                    opacity: 0.6
                                    font.family: ui.theme.bodyFont.family
                                    font.pixelSize: Math.round(ui.theme.bodyFont.pixelSize * 0.85)
                                    text: instrumentRow.modelData.sound
                                }
                            }

                            MouseArea {
                                id: instrumentMouseArea

                                anchors.fill: parent
                                hoverEnabled: true

                                // Qt.ControlModifier is Cmd on macOS and Ctrl on Windows/Linux
                                onClicked: function(mouse) {
                                    presetsModel.selectInstrument(instrumentRow.index, (mouse.modifiers & Qt.ControlModifier) !== 0,
                                                                  (mouse.modifiers & Qt.ShiftModifier) !== 0)
                                }
                            }
                        }
                    }
                }
            }

            // ---- the presets or the sounds
            ColumnLayout {
                Layout.fillWidth: true
                Layout.fillHeight: true
                spacing: 6

                //! NOTE: track presets (a whole track), or MuseSounds sounds and SoundFont presets (the sound only, no
                //! preset needed)
                // the tabs, and the search on the same line
                RowLayout {
                    Layout.fillWidth: true
                    spacing: 8

                    RadioButtonGroup {
                        Layout.preferredHeight: 28
                        orientation: ListView.Horizontal

                        model: [
                            { text: qsTrc("playback", "Track presets"), value: TrackPresetsModel.Presets },
                            { text: qsTrc("playback", "MuseSounds"), value: TrackPresetsModel.MuseSounds },
                            { text: qsTrc("playback", "SoundFonts"), value: TrackPresetsModel.SoundFonts }
                        ]

                        delegate: FlatRadioButton {
                            required property var modelData

                            width: 112
                            height: 28
                            text: modelData.text
                            checked: presetsModel.source === modelData.value
                            onToggled: presetsModel.source = modelData.value
                        }
                    }

                    Item { Layout.fillWidth: true }

                    SearchField {
                        Layout.preferredWidth: 260
                        hint: presetsModel.source === TrackPresetsModel.Presets
                              ? qsTrc("playback", "Search track presets")
                              : (presetsModel.source === TrackPresetsModel.MuseSounds ? qsTrc("playback", "Search MuseSounds")
                                                                                      : qsTrc("playback", "Search SoundFonts"))
                        onSearchTextChanged: {
                            presetsModel.searchText = searchText
                        }
                    }
                }

                // the presets' columns order on the left, Load on the right
                RowLayout {
                    Layout.fillWidth: true
                    spacing: 8

                    //! NOTE: the order of the presets' two grouping columns
                    StyledTextLabel {
                        visible: presetsModel.source === TrackPresetsModel.Presets
                        text: qsTrc("playback", "Group by:")
                    }

                    StyledDropdown {
                        visible: presetsModel.source === TrackPresetsModel.Presets
                        Layout.preferredWidth: 180
                        model: presetsModel.groupByOptions
                        currentIndex: indexOfValue(presetsModel.groupBy)
                        onActivated: function(index, value) {
                            presetsModel.groupBy = value
                        }
                    }

                    Item { Layout.fillWidth: true }

                    //! NOTE: like a double click: the selected preset or sound, to the selected instruments
                    FlatButton {
                        minWidth: 0
                        accentButton: true
                        enabled: presetsModel.canApplySelectedItem
                        text: qsTrc("playback", "Load")
                        onClicked: presetsModel.applySelectedItem()
                    }
                }

                RowLayout {
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    spacing: 6

                    // a child layout fills the width by default: the items' column takes the rest
                    NamesColumn {
                        Layout.preferredWidth: 215
                        Layout.fillWidth: false
                        Layout.fillHeight: true
                        title: presetsModel.firstColumnTitle
                        names: presetsModel.firstColumnNames
                        selectedName: presetsModel.selectedFirst
                        onNameClicked: function(name) {
                            presetsModel.selectedFirst = name
                        }
                    }

                    NamesColumn {
                        Layout.preferredWidth: 215
                        Layout.fillWidth: false
                        Layout.fillHeight: true
                        title: presetsModel.secondColumnTitle
                        names: presetsModel.secondColumnNames
                        selectedName: presetsModel.selectedSecond
                        onNameClicked: function(name) {
                            presetsModel.selectedSecond = name
                        }
                    }

                    ColumnLayout {
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        spacing: 4

                        StyledTextLabel {
                            font: ui.theme.bodyBoldFont
                            text: presetsModel.itemsColumnTitle
                        }

                        Rectangle {
                            Layout.fillWidth: true
                            Layout.fillHeight: true
                            color: ui.theme.textFieldColor
                            border.color: ui.theme.strokeColor
                            border.width: 1

                            StyledTextLabel {
                                anchors.centerIn: parent
                                width: parent.width - 32
                                visible: itemsView.count === 0
                                wrapMode: Text.WordWrap
                                opacity: 0.7
                                text: presetsModel.source === TrackPresetsModel.Presets
                                      ? qsTrc("playback", "No track preset. Save one from a track: “Save as track preset…” in the Mixer or the Track list.")
                                      : (presetsModel.source === TrackPresetsModel.MuseSounds ? qsTrc("playback", "No MuseSounds sound")
                                                                                              : qsTrc("playback", "No SoundFont"))
                            }

                            StyledListView {
                                id: itemsView

                                anchors.fill: parent
                                anchors.margins: 1
                                clip: true

                                model: presetsModel.columnItems

                                delegate: Item {
                                    id: itemRow

                                    required property var modelData

                                    width: itemsView.width
                                    height: 26

                                    Rectangle {
                                        anchors.fill: parent
                                        color: itemRow.modelData.index === presetsModel.selectedItemIndex ? ui.theme.accentColor
                                               : (itemMouseArea.containsMouse ? ui.theme.buttonColor : "transparent")
                                        opacity: 0.5
                                    }

                                    Row {
                                        anchors.left: parent.left
                                        anchors.leftMargin: 8
                                        anchors.right: parent.right
                                        anchors.rightMargin: 8
                                        anchors.verticalCenter: parent.verticalCenter
                                        spacing: 12
                                        opacity: itemRow.modelData.isAvailable ? 1 : 0.4

                                        StyledTextLabel {
                                            anchors.verticalCenter: parent.verticalCenter
                                            horizontalAlignment: Text.AlignLeft
                                            text: itemRow.modelData.name
                                        }

                                        // e.g. the preset's plugin, smaller and dimmed
                                        StyledTextLabel {
                                            anchors.verticalCenter: parent.verticalCenter
                                            width: Math.max(0, parent.width - x)
                                            horizontalAlignment: Text.AlignLeft
                                            elide: Text.ElideRight
                                            opacity: 0.6
                                            font.family: ui.theme.bodyFont.family
                                            font.pixelSize: Math.round(ui.theme.bodyFont.pixelSize * 0.85)
                                            text: itemRow.modelData.details
                                        }
                                    }

                                    //! NOTE: a double click applies it to the selected instruments
                                    MouseArea {
                                        id: itemMouseArea

                                        anchors.fill: parent
                                        hoverEnabled: true

                                        onPressed: {
                                            presetsModel.selectItem(itemRow.modelData.index)
                                        }

                                        onDoubleClicked: {
                                            presetsModel.applyItem(itemRow.modelData.index)
                                        }
                                    }
                                }
                            }
                        }
                    }
                }

                // ---- the selected preset: its plugin, and its name and tags to edit (a MuseSounds sound has none)
                GridLayout {
                    id: presetFields

                    // the edited values, saved by Update; back to the preset's when another one is selected (set, not
                    // bound: the fields' edits assign them)
                    property string editedName: ""
                    property string editedTags: ""

                    Connections {
                        target: presetsModel

                        function onSelectionChanged() {
                            presetFields.editedName = presetsModel.selectedPresetName
                            presetFields.editedTags = presetsModel.selectedPresetTags
                        }
                    }

                    Layout.fillWidth: true
                    visible: presetsModel.source === TrackPresetsModel.Presets
                    enabled: presetsModel.hasSelectedPreset
                    columns: 2
                    columnSpacing: 8
                    rowSpacing: 6

                    StyledTextLabel {
                        text: qsTrc("playback", "Plugin")
                    }

                    TextInputField {
                        Layout.fillWidth: true
                        enabled: false
                        currentText: presetsModel.selectedPresetPlugin
                    }

                    StyledTextLabel {
                        text: qsTrc("playback", "Name")
                    }

                    TextInputField {
                        Layout.fillWidth: true
                        currentText: presetFields.editedName
                        onTextChanged: function(newTextValue) {
                            presetFields.editedName = newTextValue
                        }
                    }

                    StyledTextLabel {
                        text: qsTrc("playback", "Tags")
                    }

                    TextInputField {
                        Layout.fillWidth: true
                        currentText: presetFields.editedTags
                        hint: qsTrc("playback", "Keywords separated by commas")
                        onTextChanged: function(newTextValue) {
                            presetFields.editedTags = newTextValue
                        }
                    }

                    Item { width: 1; height: 1 }

                    RowLayout {
                        Layout.fillWidth: true

                        StyledTextLabel {
                            Layout.fillWidth: true
                            horizontalAlignment: Text.AlignLeft
                            wrapMode: Text.WordWrap
                            opacity: 0.7
                            text: presetsModel.selectedItemDetails
                        }

                        FlatButton {
                            minWidth: 0
                            text: qsTrc("playback", "Delete")
                            onClicked: presetsModel.removeSelectedPreset()
                        }

                        FlatButton {
                            minWidth: 0
                            accentButton: true
                            enabled: presetFields.editedName !== presetsModel.selectedPresetName
                                     || presetFields.editedTags !== presetsModel.selectedPresetTags
                            text: qsTrc("playback", "Update")
                            onClicked: presetsModel.updateSelectedPreset(presetFields.editedName, presetFields.editedTags)
                        }
                    }
                }

                StyledTextLabel {
                    Layout.fillWidth: true
                    visible: presetsModel.source !== TrackPresetsModel.Presets
                    horizontalAlignment: Text.AlignLeft
                    wrapMode: Text.WordWrap
                    opacity: 0.7
                    text: presetsModel.selectedItemDetails
                }
            }
        }

        RowLayout {
            Layout.fillWidth: true

            CheckBox {
                text: qsTrc("notation", "Always on top")
                checked: prv.alwaysOnTop
                onClicked: prv.setAlwaysOnTop(!checked)
            }

            Item { Layout.fillWidth: true }

            FlatButton {
                text: qsTrc("global", "Close")
                onClicked: root.close()
            }
        }
    }
}
