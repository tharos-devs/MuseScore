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

    title: {
        let name = editorModel.fileName !== "" ? editorModel.fileName : qsTrc("notation", "Untitled")
        return qsTrc("notation", "Articulation map editor") + " – " + name + (editorModel.isDirty ? " *" : "")
    }

    contentWidth: 880
    contentHeight: 520

    modal: false
    resizable: true

    // set when opened for a track's map (see the Mixer)
    property string mapFilePath: ""
    property string mapText: ""
    property string partId: ""
    property string instrumentId: ""

    property bool closeConfirmed: false

    onIsOpenedChanged: {
        // on by default: the editor is used next to the score and the Mixer
        if (isOpened && !prv.alwaysOnTopInitialized) {
            prv.alwaysOnTopInitialized = true
            prv.setAlwaysOnTop(true)
        }
    }

    Component.onCompleted: {
        if (partId !== "") {
            editorModel.setTargetTrack(partId, instrumentId)
        }

        if (mapFilePath !== "") {
            editorModel.openMapFile(mapFilePath)
        } else if (mapText !== "") {
            editorModel.openMapText(mapText)
        }
    }

    onAboutToClose: function(closeEvent) {
        if (closeConfirmed || !editorModel.isDirty) {
            return
        }

        closeEvent.accepted = false
        editorModel.closeRequested()
    }

    ArticulationMapEditorModel {
        id: editorModel

        onCloseAccepted: {
            root.closeConfirmed = true
            root.close()
        }
    }

    ColorPickerModel {
        id: colorPickerModel

        property int row: -1

        onColorSelected: function(color) {
            editorModel.setColor(row, color)
        }
    }

    QtObject {
        id: prv

        readonly property int rowHeight: 26
        readonly property int indentWidth: 16
        readonly property int numberColumnWidth: 32
        readonly property int colorColumnWidth: 24
        readonly property int nameColumnWidth: 260
        readonly property int sidePanelWidth: 340

        property int editingRow: -1

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

        // drag & drop
        property int dragFromRow: -1
        property int dropRow: -1
        property int dropPosition: ArticulationMapEditorModel.Before
        // below the last row: to the end of the map, outside of any folder (the indicator shows on the last row)
        property bool dropAtEnd: false
        readonly property bool isDragging: dragFromRow >= 0

        function cancelDrag() {
            dragFromRow = -1
            dropRow = -1
            dropAtEnd = false
        }

        function drop() {
            const from = dragFromRow
            const atEnd = dropAtEnd
            const to = atEnd ? -1 : dropRow
            const position = dropPosition
            const hasTarget = atEnd || dropRow >= 0
            cancelDrag()

            // the move rebuilds the list, destroying the delegate whose handler is running
            // (-1 = the end of the map, outside of any folder)
            if (from >= 0 && hasTarget) {
                Qt.callLater(editorModel.move, from, to, position)
            }
        }
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 12
        spacing: 12

        // ---- file & map header
        RowLayout {
            Layout.fillWidth: true
            spacing: 8

            FlatButton {
                icon: IconCode.NEW_FILE
                toolTipTitle: qsTrc("notation", "New articulation map")
                onClicked: editorModel.newMap()
            }

            FlatButton {
                icon: IconCode.OPEN_FILE
                toolTipTitle: qsTrc("notation", "Open…")
                onClicked: editorModel.openMap()
            }

            FlatButton {
                icon: IconCode.SAVE
                toolTipTitle: qsTrc("notation", "Save")
                onClicked: editorModel.saveMap()
            }

            FlatButton {
                minWidth: 0
                text: qsTrc("notation", "Save as…")
                onClicked: editorModel.saveMapAs()
            }

            Item { Layout.preferredWidth: 12 }

            StyledTextLabel {
                text: qsTrc("notation", "Name:")
            }

            TextInputField {
                Layout.preferredWidth: 200
                currentText: editorModel.mapName
                hint: qsTrc("notation", "Sample library")
                onTextEditingFinished: function(newTextValue) {
                    editorModel.mapName = newTextValue
                }
            }

            StyledTextLabel {
                text: qsTrc("notation", "Middle C:")
            }

            StyledDropdown {
                Layout.preferredWidth: 80
                model: [
                    { text: "C3", value: 3 },
                    { text: "C4", value: 4 }
                ]
                currentIndex: editorModel.middleCOctave === 4 ? 1 : 0
                onActivated: function(index, value) {
                    editorModel.middleCOctave = value
                }
            }

            StyledTextLabel {
                text: qsTrc("notation", "Track delay:")
            }

            IncrementalPropertyControl {
                Layout.preferredWidth: 90
                currentValue: editorModel.keyswitchOffsetMs
                decimals: 0
                step: 1
                minValue: -1000
                maxValue: 1000
                measureUnitsSymbol: qsTrc("global", "ms")
                onValueEditingFinished: function(newValue) {
                    editorModel.keyswitchOffsetMs = newValue
                }
            }

            Item { Layout.fillWidth: true }
        }

        RowLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            spacing: 12

            // ---- articulations list
            ColumnLayout {
                Layout.fillWidth: true
                Layout.fillHeight: true
                spacing: 0

                Rectangle {
                    Layout.fillWidth: true
                    Layout.preferredHeight: prv.rowHeight
                    color: ui.theme.backgroundSecondaryColor

                    Row {
                        anchors.fill: parent

                        StyledTextLabel {
                            width: prv.numberColumnWidth
                            height: parent.height
                            text: "#"
                        }

                        Item { width: prv.colorColumnWidth; height: 1 }

                        StyledTextLabel {
                            width: prv.nameColumnWidth
                            height: parent.height
                            horizontalAlignment: Text.AlignLeft
                            text: qsTrc("notation", "Name")
                            font: ui.theme.bodyBoldFont
                        }

                        StyledTextLabel {
                            height: parent.height
                            horizontalAlignment: Text.AlignLeft
                            text: qsTrc("notation", "Activation sequence")
                            font: ui.theme.bodyBoldFont
                        }
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

                        model: editorModel

                        delegate: Item {
                            id: rowItem

                            required property int index
                            required property string name
                            required property bool isFolder
                            required property int depth
                            required property bool isExpanded
                            required property bool hasChildren
                            required property int number
                            required property color articulationColor
                            required property bool hasOwnColor
                            required property string sequence
                            required property bool isDefault
                            required property bool isDisabled

                            width: listView.width
                            height: prv.rowHeight

                            Rectangle {
                                anchors.fill: parent
                                color: editorModel.selectedRow === rowItem.index ? ui.theme.accentColor
                                       : (rowMouseArea.containsMouse ? ui.theme.buttonColor : "transparent")
                                opacity: editorModel.selectedRow === rowItem.index ? 0.5 : 0.6
                            }

                            // drop indicator
                            Rectangle {
                                visible: prv.isDragging && prv.dropRow === rowItem.index
                                anchors.fill: prv.dropPosition === ArticulationMapEditorModel.Into ? parent : undefined
                                anchors.left: parent.left
                                anchors.right: parent.right
                                y: prv.dropPosition === ArticulationMapEditorModel.After ? parent.height - height : 0
                                height: prv.dropPosition === ArticulationMapEditorModel.Into ? parent.height : 2
                                color: prv.dropPosition === ArticulationMapEditorModel.Into ? "transparent" : ui.theme.accentColor
                                border.color: ui.theme.accentColor
                                border.width: prv.dropPosition === ArticulationMapEditorModel.Into ? 2 : 0
                            }

                            MouseArea {
                                id: rowMouseArea

                                anchors.fill: parent
                                hoverEnabled: true
                                acceptedButtons: Qt.LeftButton | Qt.RightButton
                                preventStealing: true // the list would otherwise take the drag to scroll

                                property point pressPoint

                                onPressed: function(mouse) {
                                    editorModel.selectedRow = rowItem.index
                                    pressPoint = Qt.point(mouse.x, mouse.y)
                                }

                                onPositionChanged: function(mouse) {
                                    if (!pressed || mouse.buttons !== Qt.LeftButton) {
                                        return
                                    }

                                    if (!prv.isDragging) {
                                        if (Math.abs(mouse.y - pressPoint.y) < 6) {
                                            return
                                        }
                                        prv.dragFromRow = rowItem.index
                                    }

                                    const p = mapToItem(listView.contentItem, mouse.x, mouse.y)
                                    const targetRow = listView.indexAt(10, p.y)
                                    prv.dropAtEnd = false
                                    if (targetRow < 0) {
                                        const lastItem = listView.itemAtIndex(listView.count - 1)
                                        if (lastItem && p.y >= lastItem.y + lastItem.height
                                                && editorModel.canMove(prv.dragFromRow, -1, ArticulationMapEditorModel.After)) {
                                            prv.dropAtEnd = true
                                            prv.dropRow = listView.count - 1
                                            prv.dropPosition = ArticulationMapEditorModel.After
                                        } else {
                                            prv.dropRow = -1
                                        }
                                        return
                                    }

                                    const target = listView.itemAtIndex(targetRow)
                                    const yInRow = p.y - target.y
                                    let position = yInRow < target.height / 2 ? ArticulationMapEditorModel.Before
                                                                              : ArticulationMapEditorModel.After
                                    if (target.isFolder && yInRow > target.height / 4 && yInRow < target.height * 3 / 4) {
                                        position = ArticulationMapEditorModel.Into
                                    }

                                    // After the very last row = the end of the map, outside of any folder (the list may
                                    // have no empty space below it); "Into" its folder is for the end of that folder
                                    if (targetRow === listView.count - 1 && position === ArticulationMapEditorModel.After
                                            && editorModel.canMove(prv.dragFromRow, -1, ArticulationMapEditorModel.After)) {
                                        prv.dropAtEnd = true
                                        prv.dropRow = targetRow
                                        prv.dropPosition = position
                                        return
                                    }

                                    if (editorModel.canMove(prv.dragFromRow, targetRow, position)) {
                                        prv.dropRow = targetRow
                                        prv.dropPosition = position
                                    } else {
                                        prv.dropRow = -1
                                    }
                                }

                                onReleased: {
                                    prv.drop()
                                }

                                onCanceled: {
                                    prv.cancelDrag()
                                }

                                onDoubleClicked: {
                                    prv.editingRow = rowItem.index
                                }
                            }

                            Row {
                                anchors.fill: parent
                                opacity: rowItem.isDisabled ? 0.4 : 1.0

                                StyledTextLabel {
                                    width: prv.numberColumnWidth
                                    height: parent.height
                                    text: rowItem.isFolder ? "" : rowItem.number
                                }

                                Item {
                                    width: prv.colorColumnWidth
                                    height: parent.height

                                    Rectangle {
                                        visible: !rowItem.isFolder
                                        anchors.centerIn: parent
                                        width: 14
                                        height: 14
                                        radius: 2
                                        color: rowItem.articulationColor
                                        border.color: ui.theme.strokeColor
                                        border.width: rowItem.hasOwnColor ? 0 : 1

                                        MouseArea {
                                            anchors.fill: parent
                                            anchors.margins: -4
                                            acceptedButtons: Qt.LeftButton | Qt.RightButton
                                            onClicked: function(mouse) {
                                                if (mouse.button === Qt.RightButton) {
                                                    editorModel.resetColor(rowItem.index)
                                                    return
                                                }
                                                colorPickerModel.row = rowItem.index
                                                colorPickerModel.selectColor(rowItem.articulationColor, false)
                                            }
                                        }
                                    }
                                }

                                Item {
                                    width: prv.nameColumnWidth
                                    height: parent.height

                                    Row {
                                        anchors.fill: parent
                                        anchors.leftMargin: rowItem.depth * prv.indentWidth
                                        spacing: 4

                                        StyledIconLabel {
                                            visible: rowItem.isFolder
                                            anchors.verticalCenter: parent.verticalCenter
                                            width: 16
                                            iconCode: rowItem.isExpanded ? IconCode.SMALL_ARROW_DOWN : IconCode.SMALL_ARROW_RIGHT

                                            MouseArea {
                                                anchors.fill: parent
                                                anchors.margins: -4
                                                onClicked: editorModel.toggleExpanded(rowItem.index)
                                            }
                                        }

                                        StyledTextLabel {
                                            visible: prv.editingRow !== rowItem.index
                                            anchors.verticalCenter: parent.verticalCenter
                                            horizontalAlignment: Text.AlignLeft
                                            text: rowItem.name + (rowItem.isDefault ? "  ★" : "")
                                            font: rowItem.isFolder ? ui.theme.bodyBoldFont : ui.theme.bodyFont
                                        }

                                        Loader {
                                            active: prv.editingRow === rowItem.index
                                            visible: active
                                            anchors.verticalCenter: parent.verticalCenter
                                            width: prv.nameColumnWidth - rowItem.depth * prv.indentWidth - 24

                                            sourceComponent: TextInputField {
                                                currentText: rowItem.name
                                                Component.onCompleted: {
                                                    ensureActiveFocus()
                                                    selectAll()
                                                }
                                                onTextEditingFinished: function(newTextValue) {
                                                    editorModel.rename(rowItem.index, newTextValue)
                                                    prv.editingRow = -1
                                                }
                                                onEscaped: prv.editingRow = -1
                                            }
                                        }
                                    }
                                }

                                StyledTextLabel {
                                    height: parent.height
                                    width: parent.width - prv.numberColumnWidth - prv.colorColumnWidth - prv.nameColumnWidth
                                    horizontalAlignment: Text.AlignLeft
                                    elide: Text.ElideRight
                                    text: rowItem.sequence
                                }
                            }
                        }
                    }
                }

                // wraps onto a second line when the list is narrow
                Flow {
                    Layout.fillWidth: true
                    Layout.topMargin: 8
                    spacing: 8

                    FlatButton {
                        minWidth: 0
                        text: qsTrc("notation", "New articulation")
                        onClicked: editorModel.addArticulation()
                    }

                    FlatButton {
                        minWidth: 0
                        text: qsTrc("notation", "New folder")
                        onClicked: editorModel.addFolder()
                    }

                    FlatButton {
                        minWidth: 0
                        text: qsTrc("notation", "Rename")
                        enabled: editorModel.hasSelection
                        onClicked: prv.editingRow = editorModel.selectedRow
                    }

                    FlatButton {
                        minWidth: 0
                        text: qsTrc("global", "Copy")
                        enabled: editorModel.hasSelection && !editorModel.selectedIsFolder
                        onClicked: editorModel.copySelectedArticulation()
                    }

                    FlatButton {
                        minWidth: 0
                        text: qsTrc("global", "Remove")
                        enabled: editorModel.hasSelection
                        onClicked: editorModel.removeSelected()
                    }
                }
            }

            // ---- selected articulation
            Rectangle {
                Layout.preferredWidth: prv.sidePanelWidth
                Layout.fillHeight: true
                color: ui.theme.backgroundSecondaryColor
                radius: 4

                StyledTextLabel {
                    anchors.centerIn: parent
                    visible: !editorModel.hasSelection || editorModel.selectedIsFolder
                    width: parent.width - 32
                    wrapMode: Text.WordWrap
                    text: editorModel.selectedIsFolder
                          ? qsTrc("notation", "“%1” is a submenu of the articulation picker. Drag articulations into it.").arg(editorModel.selectedName)
                          : qsTrc("notation", "Select an articulation")
                }

                ColumnLayout {
                    anchors.fill: parent
                    anchors.margins: 12
                    spacing: 10
                    visible: editorModel.hasSelection && !editorModel.selectedIsFolder

                    StyledTextLabel {
                        Layout.fillWidth: true
                        horizontalAlignment: Text.AlignLeft
                        elide: Text.ElideRight
                        font: ui.theme.largeBodyBoldFont
                        text: editorModel.selectedNumber + "   " + editorModel.selectedName
                    }

                    CheckBox {
                        text: qsTrc("notation", "Default articulation")
                        enabled: !editorModel.selectedIsDisabled
                        checked: editorModel.selectedIsDefault
                        onClicked: editorModel.setSelectedIsDefault(!checked)
                    }

                    CheckBox {
                        text: qsTrc("notation", "Disable articulation")
                        checked: editorModel.selectedIsDisabled
                        onClicked: editorModel.setSelectedIsDisabled(!checked)
                    }

                    RowLayout {
                        Layout.fillWidth: true

                        CheckBox {
                            Layout.fillWidth: true
                            text: qsTrc("notation", "Articulation delay")
                            checked: editorModel.selectedHasKeyswitchOffset
                            onClicked: editorModel.setSelectedHasKeyswitchOffset(!checked)
                        }

                        IncrementalPropertyControl {
                            Layout.preferredWidth: 90
                            enabled: editorModel.selectedHasKeyswitchOffset
                            currentValue: editorModel.selectedKeyswitchOffsetMs
                            decimals: 0
                            step: 1
                            minValue: -1000
                            maxValue: 1000
                            measureUnitsSymbol: qsTrc("global", "ms")
                            onValueEditingFinished: function(newValue) {
                                editorModel.setSelectedKeyswitchOffsetMs(newValue)
                            }
                        }
                    }

                    RowLayout {
                        Layout.fillWidth: true

                        StyledTextLabel {
                            Layout.fillWidth: true
                            horizontalAlignment: Text.AlignLeft
                            text: qsTrc("notation", "Note delay")
                        }

                        IncrementalPropertyControl {
                            Layout.preferredWidth: 90
                            currentValue: editorModel.selectedNotesOffsetMs
                            decimals: 0
                            step: 1
                            minValue: -1000
                            maxValue: 1000
                            measureUnitsSymbol: qsTrc("global", "ms")
                            onValueEditingFinished: function(newValue) {
                                editorModel.setSelectedNotesOffsetMs(newValue)
                            }
                        }
                    }

                    StyledTextLabel {
                        Layout.topMargin: 8
                        horizontalAlignment: Text.AlignLeft
                        font: ui.theme.bodyBoldFont
                        text: qsTrc("notation", "Activation sequence")
                    }

                    RowLayout {
                        Layout.fillWidth: true
                        spacing: 6

                        StyledTextLabel {
                            Layout.preferredWidth: 120
                            horizontalAlignment: Text.AlignLeft
                            text: qsTrc("notation", "Type")
                        }
                        StyledTextLabel {
                            Layout.preferredWidth: 56
                            horizontalAlignment: Text.AlignLeft
                            text: qsTrc("notation", "Data")
                        }
                        StyledTextLabel {
                            Layout.fillWidth: true
                            horizontalAlignment: Text.AlignLeft
                            text: qsTrc("notation", "Data 2")
                        }
                    }

                    StyledFlickable {
                        id: messagesFlickable

                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        contentHeight: messagesColumn.height
                        clip: true

                        Column {
                            id: messagesColumn

                            width: messagesFlickable.width - 12 // room for the scroll bar
                            spacing: 6

                            Repeater {
                                model: editorModel.selectedMessages

                                delegate: RowLayout {
                                    id: messageRow

                                    required property int index
                                    required property var modelData

                                    width: messagesColumn.width
                                    spacing: 6

                                    StyledDropdown {
                                        Layout.preferredWidth: 120
                                        model: [
                                            { text: qsTrc("notation", "Note On/Off"), value: ArticulationMapEditorModel.Note },
                                            { text: qsTrc("notation", "MIDI CC"), value: ArticulationMapEditorModel.ControlChange },
                                            { text: qsTrc("notation", "Program Change"), value: ArticulationMapEditorModel.ProgramChange }
                                        ]
                                        currentIndex: messageRow.modelData.type
                                        onActivated: function(index, value) {
                                            editorModel.setMessageType(messageRow.index, value)
                                        }
                                    }

                                    TextInputField {
                                        Layout.preferredWidth: 56
                                        currentText: messageRow.modelData.data
                                        onTextEditingFinished: function(newTextValue) {
                                            editorModel.setMessageData(messageRow.index, newTextValue)
                                        }
                                    }

                                    IncrementalPropertyControl {
                                        Layout.preferredWidth: 72 // 3 digits clear of the arrows
                                        visible: messageRow.modelData.hasData2
                                        currentValue: messageRow.modelData.data2
                                        decimals: 0
                                        step: 1
                                        minValue: 0
                                        maxValue: 127
                                        onValueEditingFinished: function(newValue) {
                                            editorModel.setMessageData2(messageRow.index, newValue)
                                        }
                                    }

                                    Item {
                                        Layout.preferredWidth: 72
                                        visible: !messageRow.modelData.hasData2
                                    }

                                    Item { Layout.fillWidth: true }

                                    FlatButton {
                                        icon: IconCode.DELETE_TANK
                                        transparent: true
                                        toolTipTitle: qsTrc("global", "Remove")
                                        onClicked: editorModel.removeMessage(messageRow.index)
                                    }
                                }
                            }
                        }
                    }

                    FlatButton {
                        minWidth: 0
                        text: qsTrc("notation", "+ Add")
                        onClicked: editorModel.addMessage()
                    }
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
                visible: editorModel.canReloadIntoTrack
                icon: IconCode.UPDATE
                toolTipTitle: qsTrc("notation", "Reload into the track")
                toolTipDescription: qsTrc("notation", "Saves the changes, then plays the track with this map")
                onClicked: editorModel.reloadIntoTrack()
            }

            FlatButton {
                text: qsTrc("global", "Close")
                onClicked: root.close()
            }
        }
    }
}
