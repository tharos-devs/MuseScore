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

    contentWidth: 1000 // 880 + the Score markings column
    contentHeight: 580

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

        property int row: -1 // -1: the selected articulations

        onColorSelected: function(color) {
            if (row < 0) {
                editorModel.setSelectedColor(color)
            } else {
                editorModel.setColor(row, color)
            }
        }
    }

    QtObject {
        id: prv

        readonly property int rowHeight: 26
        readonly property int indentWidth: 16
        readonly property int numberColumnWidth: 32
        readonly property int colorColumnWidth: 24
        readonly property int nameColumnWidth: 175
        readonly property int scoreMarkingsColumnWidth: 205
        readonly property int sidePanelWidth: 340

        property int editingRow: -1

        // several articulations selected: the side panel sets the properties of all of them
        readonly property bool multipleArticulations: editorModel.selectedArticulationCount > 1

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

            //! NOTE: the target track's VST instrument window, colored while it's open
            FlatButton {
                visible: editorModel.hasInstrumentEditor

                transparent: true
                icon: IconCode.PLUGIN
                iconColor: editorModel.instrumentEditorOpened ? ui.theme.accentColor : ui.theme.fontPrimaryColor
                toolTipTitle: qsTrc("playback", "Open instrument window")

                onClicked: {
                    editorModel.openInstrumentEditor()
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
                            width: prv.scoreMarkingsColumnWidth
                            height: parent.height
                            horizontalAlignment: Text.AlignLeft
                            elide: Text.ElideRight
                            text: qsTrc("notation", "Score markings")
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

                        function isListKey(event) {
                            return event.matches(StandardKey.SelectAll) || event.key === Qt.Key_Delete || event.key === Qt.Key_Backspace
                        }

                        // not the application's shortcuts (the score's Select all, Delete...)
                        Keys.onShortcutOverride: function(event) {
                            event.accepted = isListKey(event)
                        }

                        Keys.onPressed: function(event) {
                            if (event.matches(StandardKey.SelectAll)) {
                                editorModel.selectAll()
                                event.accepted = true
                            } else if (event.key === Qt.Key_Delete || event.key === Qt.Key_Backspace) {
                                editorModel.removeSelected()
                                event.accepted = true
                            }
                        }

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
                            required property bool isSelected
                            required property var scoreMarkings

                            width: listView.width
                            height: prv.rowHeight

                            Rectangle {
                                anchors.fill: parent
                                color: rowItem.isSelected ? ui.theme.accentColor
                                       : (rowMouseArea.containsMouse ? ui.theme.buttonColor : "transparent")
                                opacity: rowItem.isSelected ? 0.5 : 0.6
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
                                // pressed in a selection of several rows: they're dragged together, a click selects it alone
                                property bool selectAloneOnRelease: false

                                function selectAlone(button) {
                                    editorModel.selectedRow = rowItem.index

                                    //! NOTE: clicking an articulation lets you hear it on the track's instrument
                                    if (button === Qt.LeftButton) {
                                        editorModel.sendArticulation(rowItem.index)
                                    }
                                }

                                onPressed: function(mouse) {
                                    listView.forceActiveFocus()
                                    pressPoint = Qt.point(mouse.x, mouse.y)
                                    selectAloneOnRelease = false

                                    const toggle = mouse.modifiers & Qt.ControlModifier
                                    const extend = mouse.modifiers & Qt.ShiftModifier
                                    if (mouse.button === Qt.LeftButton && (toggle || extend)) {
                                        editorModel.selectRow(rowItem.index, toggle, extend)
                                        return
                                    }

                                    if (rowItem.isSelected && editorModel.selectionCount > 1) {
                                        selectAloneOnRelease = mouse.button === Qt.LeftButton
                                        return
                                    }

                                    selectAlone(mouse.button)
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

                                onReleased: function(mouse) {
                                    if (selectAloneOnRelease && !prv.isDragging) {
                                        selectAloneOnRelease = false
                                        selectAlone(mouse.button)
                                        return
                                    }

                                    selectAloneOnRelease = false
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
                                            // on a row of a selection of several articulations: all of them
                                            onClicked: function(mouse) {
                                                const forSelection = rowItem.isSelected && prv.multipleArticulations
                                                if (mouse.button === Qt.RightButton) {
                                                    if (forSelection) {
                                                        editorModel.resetSelectedColor()
                                                    } else {
                                                        editorModel.resetColor(rowItem.index)
                                                    }
                                                    return
                                                }
                                                colorPickerModel.row = forSelection ? -1 : rowItem.index
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

                                //! NOTE: an overview, the details are in the side panel; a marking an articulation
                                //! higher in the map has too is dimmed: that one is played for it
                                StyledTextLabel {
                                    height: parent.height
                                    width: prv.scoreMarkingsColumnWidth
                                    rightPadding: 8
                                    horizontalAlignment: Text.AlignLeft
                                    elide: Text.ElideRight
                                    textFormat: Text.StyledText
                                    text: {
                                        const dimmed = Qt.rgba(ui.theme.fontPrimaryColor.r, ui.theme.fontPrimaryColor.g,
                                                               ui.theme.fontPrimaryColor.b, ui.theme.itemOpacityDisabled)
                                        return rowItem.scoreMarkings.map(marking => marking.shadowed
                                                                         ? "<font color=\"" + dimmed + "\">" + marking.name + "</font>"
                                                                         : marking.name).join(", ")
                                    }
                                }

                                StyledTextLabel {
                                    height: parent.height
                                    width: parent.width - prv.numberColumnWidth - prv.colorColumnWidth - prv.nameColumnWidth
                                           - prv.scoreMarkingsColumnWidth
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

                    //! NOTE: recolors the selected articulations (all of them without several selected) with a gradient
                    FlatButton {
                        minWidth: 0
                        text: qsTrc("notation", "Colors")
                        toolTipTitle: qsTrc("notation", "Color gradient")
                        toolTipDescription: qsTrc("notation", "Recolors the selected articulations, or all of them, from the first one to the last one")
                        onClicked: colorGradientsPopup.toggleOpened()

                        StyledPopupView {
                            id: colorGradientsPopup

                            contentWidth: colorGradientsColumn.implicitWidth
                            contentHeight: colorGradientsColumn.implicitHeight

                            Column {
                                id: colorGradientsColumn

                                spacing: 4

                                Repeater {
                                    model: editorModel.colorGradientPreviews

                                    delegate: Rectangle {
                                        id: gradientRow

                                        required property int index
                                        required property var modelData

                                        width: samplesRow.width + 12
                                        height: samplesRow.height + 12
                                        radius: 3
                                        color: gradientMouseArea.containsMouse ? ui.theme.buttonColor : "transparent"

                                        Row {
                                            id: samplesRow

                                            anchors.centerIn: parent
                                            spacing: 2

                                            Repeater {
                                                model: gradientRow.modelData

                                                delegate: Rectangle {
                                                    required property color modelData

                                                    width: 16
                                                    height: 16
                                                    radius: 2
                                                    color: modelData
                                                }
                                            }
                                        }

                                        MouseArea {
                                            id: gradientMouseArea

                                            anchors.fill: parent
                                            hoverEnabled: true

                                            onClicked: {
                                                editorModel.applyColorGradient(gradientRow.index)
                                                colorGradientsPopup.close()
                                            }
                                        }
                                    }
                                }
                            }
                        }
                    }

                    FlatButton {
                        minWidth: 0
                        text: qsTrc("notation", "Rename")
                        enabled: editorModel.hasSelection && editorModel.selectionCount === 1
                        onClicked: prv.editingRow = editorModel.selectedRow
                    }

                    FlatButton {
                        minWidth: 0
                        text: qsTrc("global", "Copy")
                        enabled: editorModel.selectedArticulationCount > 0
                        onClicked: editorModel.copySelectedArticulation()
                    }

                    FlatButton {
                        minWidth: 0
                        text: qsTrc("global", "Remove")
                        enabled: editorModel.selectionCount > 0
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
                    visible: !prv.multipleArticulations && (!editorModel.hasSelection || editorModel.selectedIsFolder)
                    width: parent.width - 32
                    wrapMode: Text.WordWrap
                    text: editorModel.selectedIsFolder
                          ? qsTrc("notation", "“%1” is a submenu of the articulation picker. Drag articulations into it.").arg(editorModel.selectedName)
                          : qsTrc("notation", "Select an articulation")
                }

                ColumnLayout {
                    anchors.fill: parent
                    anchors.margins: 12
                    spacing: 6
                    visible: prv.multipleArticulations || (editorModel.hasSelection && !editorModel.selectedIsFolder)

                    StyledTextLabel {
                        Layout.fillWidth: true
                        horizontalAlignment: Text.AlignLeft
                        elide: Text.ElideRight
                        font: ui.theme.largeBodyBoldFont
                        text: prv.multipleArticulations
                              ? qsTrc("notation", "%n articulation(s) selected", "", editorModel.selectedArticulationCount)
                              : editorModel.selectedNumber + "   " + editorModel.selectedName
                    }

                    // side by side: room for the activation sequence below
                    RowLayout {
                        Layout.fillWidth: true
                        spacing: 8

                        CheckBox {
                            Layout.fillWidth: true
                            Layout.preferredWidth: 1
                            visible: !prv.multipleArticulations
                            text: qsTrc("notation", "Default articulation")
                            enabled: !editorModel.selectedIsDisabled
                            checked: editorModel.selectedIsDefault
                            onClicked: editorModel.setSelectedIsDefault(!checked)
                        }

                        // mixed: a click disables all of them
                        CheckBox {
                            Layout.fillWidth: true
                            Layout.preferredWidth: 1
                            text: qsTrc("notation", "Disable articulation")
                            checked: editorModel.selectedIsDisabled && !editorModel.selectedIsDisabledMixed
                            isIndeterminate: editorModel.selectedIsDisabledMixed
                            onClicked: editorModel.setSelectedIsDisabled(isIndeterminate || !checked)
                        }
                    }

                    RowLayout {
                        Layout.fillWidth: true

                        CheckBox {
                            Layout.fillWidth: true
                            text: qsTrc("notation", "Articulation delay")
                            checked: editorModel.selectedHasKeyswitchOffset && !editorModel.selectedHasKeyswitchOffsetMixed
                            isIndeterminate: editorModel.selectedHasKeyswitchOffsetMixed
                            onClicked: editorModel.setSelectedHasKeyswitchOffset(isIndeterminate || !checked)
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

                    RowLayout {
                        Layout.fillWidth: true

                        StyledTextLabel {
                            Layout.fillWidth: true
                            horizontalAlignment: Text.AlignLeft
                            text: qsTrc("notation", "MIDI channel")
                        }

                        StyledDropdown {
                            Layout.preferredWidth: 90
                            model: {
                                var items = [ { text: qsTrc("notation", "Track"), value: 0 } ]
                                for (var channel = 1; channel <= 16; ++channel) {
                                    items.push({ text: String(channel), value: channel })
                                }
                                return items
                            }
                            currentIndex: editorModel.selectedChannel
                            onActivated: function(index, value) {
                                editorModel.setSelectedChannel(value)
                            }
                        }
                    }

                    //! NOTE: what selects the articulation in the lane, without placing it there by hand
                    RowLayout {
                        Layout.fillWidth: true

                        StyledTextLabel {
                            horizontalAlignment: Text.AlignLeft
                            text: qsTrc("notation", "Score markings")
                        }

                        FlatButton {
                            id: scoreArticulationsButton

                            Layout.fillWidth: true
                            Layout.leftMargin: 8
                            minWidth: 0

                            text: editorModel.selectedScoreArticulationsText
                            toolTipTitle: qsTrc("notation", "Score markings")
                            toolTipDescription: qsTrc("notation", "The notation symbols and playing technique texts that select this articulation in the articulation lane")

                            onClicked: scoreArticulationsPopup.toggleOpened()

                            StyledPopupView {
                                id: scoreArticulationsPopup

                                // case and spaces ignored: "snap pizz" finds Snap Pizzicato
                                property string filterText: ""

                                function matches(name) {
                                    const filter = filterText.replace(/\s/g, "").toLowerCase()
                                    return filter === "" || name.toLowerCase().indexOf(filter) !== -1
                                }

                                //! NOTE: everything smaller than the editor's own controls, as one zoom
                                readonly property real zoom: 0.9

                                contentWidth: 520 * zoom
                                contentHeight: 420 * zoom

                                onOpened: {
                                    scoreMarkingsSearchField.clear()
                                    scoreMarkingsSearchField.ensureActiveFocus()
                                }

                                ZoomContainer {
                                    anchors.fill: parent
                                    zoom: scoreArticulationsPopup.zoom

                                    SearchField {
                                        id: scoreMarkingsSearchField

                                        anchors.top: parent.top
                                        anchors.left: parent.left
                                        anchors.right: parent.right

                                        hint: qsTrc("notation", "Search score markings")

                                        onSearchTextChanged: {
                                            scoreArticulationsPopup.filterText = searchText
                                        }
                                    }

                                    StyledFlickable {
                                        id: scoreArticulationsFlickable

                                        anchors.top: scoreMarkingsSearchField.bottom
                                        anchors.topMargin: 12
                                        anchors.left: parent.left
                                        anchors.right: parent.right
                                        anchors.bottom: parent.bottom
                                        contentHeight: scoreArticulationsColumn.implicitHeight
                                        clip: true

                                        Column {
                                            id: scoreArticulationsColumn

                                            width: scoreArticulationsFlickable.width - 12 // room for the scroll bar
                                            spacing: 12

                                            StyledTextLabel {
                                                width: parent.width
                                                horizontalAlignment: Text.AlignLeft
                                                wrapMode: Text.WordWrap
                                                text: qsTrc("notation", "By default, an articulation is selected by the score marking it's named like.")
                                            }

                                            Repeater {
                                                model: editorModel.scoreArticulationGroups

                                                delegate: Column {
                                                    id: group

                                                    required property var modelData

                                                    visible: group.modelData.items.some(item => scoreArticulationsPopup.matches(item.name))
                                                    width: scoreArticulationsColumn.width
                                                    spacing: 6

                                                    StyledTextLabel {
                                                        horizontalAlignment: Text.AlignLeft
                                                        font: ui.theme.bodyBoldFont
                                                        text: group.modelData.title
                                                    }

                                                    Grid {
                                                        columns: 2
                                                        columnSpacing: 12
                                                        rowSpacing: 6

                                                        Repeater {
                                                            model: group.modelData.items

                                                            delegate: Row {
                                                                id: scoreArticulationItem

                                                                required property var modelData

                                                                readonly property int selectionState: editorModel.selectedScoreArticulationStates[modelData.name] || 0

                                                                visible: scoreArticulationsPopup.matches(modelData.name)

                                                                width: (group.width - 12) / 2
                                                                spacing: 4

                                                                function toggle() {
                                                                    editorModel.setSelectedScoreArticulation(modelData.name, selectionState !== 1)
                                                                }

                                                                CheckBox {
                                                                    anchors.verticalCenter: parent.verticalCenter
                                                                    checked: scoreArticulationItem.selectionState === 1
                                                                    isIndeterminate: scoreArticulationItem.selectionState === 2
                                                                    onClicked: scoreArticulationItem.toggle()
                                                                }

                                                                //! NOTE: its symbol in the score, centered on its own shape (the music font's glyphs
                                                                //! sit anywhere around the baseline) and shrunk to fit
                                                                Item {
                                                                    id: glyphIcon

                                                                    anchors.verticalCenter: parent.verticalCenter
                                                                    width: 28
                                                                    height: 22

                                                                    MouseArea {
                                                                        anchors.fill: parent
                                                                        onClicked: scoreArticulationItem.toggle()
                                                                    }

                                                                    TextMetrics {
                                                                        id: glyphMetrics

                                                                        font.family: scoreArticulationItem.modelData.glyphFont
                                                                        font.pixelSize: 20
                                                                        text: scoreArticulationItem.modelData.glyph
                                                                    }

                                                                    Text {
                                                                        id: glyphText

                                                                        readonly property rect bounds: glyphMetrics.tightBoundingRect
                                                                        readonly property real fitScale: bounds.width > 0 && bounds.height > 0
                                                                                                         ? Math.min(1, (glyphIcon.width - 2) / bounds.width,
                                                                                                                    (glyphIcon.height - 2) / bounds.height)
                                                                                                         : 1

                                                                        visible: scoreArticulationItem.modelData.glyph !== ""
                                                                        font: glyphMetrics.font
                                                                        text: scoreArticulationItem.modelData.glyph
                                                                        color: ui.theme.fontPrimaryColor

                                                                        x: glyphIcon.width / 2 - (bounds.x + bounds.width / 2)
                                                                        y: glyphIcon.height / 2 - (baselineOffset + bounds.y + bounds.height / 2)

                                                                        // a Transform has no parent: the Text by its id
                                                                        transform: Scale {
                                                                            origin.x: glyphText.bounds.x + glyphText.bounds.width / 2
                                                                            origin.y: glyphText.baselineOffset + glyphText.bounds.y + glyphText.bounds.height / 2
                                                                            xScale: glyphText.fitScale
                                                                            yScale: glyphText.fitScale
                                                                        }
                                                                    }
                                                                }

                                                                StyledTextLabel {
                                                                    anchors.verticalCenter: parent.verticalCenter
                                                                    width: parent.width - x
                                                                    horizontalAlignment: Text.AlignLeft
                                                                    elide: Text.ElideRight
                                                                    // "SnapPizzicato" -> "Snap Pizzicato"
                                                                    text: scoreArticulationItem.modelData.name.replace(/([a-z])([A-Z0-9])/g, "$1 $2")

                                                                    MouseArea {
                                                                        anchors.fill: parent
                                                                        onClicked: scoreArticulationItem.toggle()
                                                                    }
                                                                }
                                                            }
                                                        }
                                                    }
                                                }
                                            }
                                        }
                                    }
                                }
                            }
                        }
                    }

                    // in place of the activation sequence, shown for one articulation only
                    Item {
                        Layout.fillHeight: true
                        visible: prv.multipleArticulations
                    }

                    RowLayout {
                        Layout.fillWidth: true
                        Layout.topMargin: 6
                        visible: !prv.multipleArticulations

                        StyledTextLabel {
                            Layout.fillWidth: true
                            horizontalAlignment: Text.AlignLeft
                            font: ui.theme.bodyBoldFont
                            text: qsTrc("notation", "Activation sequence")
                        }

                        FlatButton {
                            minWidth: 0
                            text: qsTrc("notation", "+ Add")
                            onClicked: editorModel.addMessage()
                        }
                    }

                    RowLayout {
                        Layout.fillWidth: true
                        visible: !prv.multipleArticulations
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

                        visible: !prv.multipleArticulations
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
