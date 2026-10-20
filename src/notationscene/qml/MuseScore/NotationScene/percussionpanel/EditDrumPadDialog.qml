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

//! NOTE: the Percussion panel's "Edit pad" (see EditDrumPadModel)
StyledDialogView {
    id: root

    title: qsTrc("notation/percussion", "Edit pad") + " – " + padModel.padName

    contentWidth: 800 // the buttons at the bottom
    contentHeight: contentColumn.implicitHeight + 24

    modal: false

    // set when opened
    property int padPitch: -1
    property int newPadRow: -1
    property int newPadColumn: -1
    property bool staffContext: false

    //! NOTE: always on top: the dialog is only of use next to the score and the Percussion panel. A tool window, so that it
    //! is hidden while another application is active (on macOS, a window staying on top otherwise stays above them too)
    onIsOpenedChanged: {
        if (isOpened && !prv.windowFlagsSet && root.window) {
            prv.windowFlagsSet = true
            root.window.flags = root.window.flags | Qt.Tool | Qt.WindowStaysOnTopHint
        }
    }

    Component.onCompleted: {
        padModel.init(root.padPitch, root.newPadRow, root.newPadColumn, root.staffContext)
    }

    EditDrumPadModel {
        id: padModel

        // later: it may be asked while the dialog is being created (init failed), before it's open
        onCloseRequested: {
            Qt.callLater(root.close)
        }
    }

    QtObject {
        id: prv

        property bool windowFlagsSet: false

        readonly property int labelWidth: 110
        // the buttons at the bottom: narrower than the standard ones, so that they fit the dialog
        readonly property int navButtonWidth: 104
        readonly property int dialogButtonWidth: 90

        // ---- keyboard: the 128 MIDI notes, 75 white keys
        readonly property var blackKeys: [1, 3, 6, 8, 10]
        readonly property int whiteKeyCount: 75

        function isBlack(pitch) {
            return prv.blackKeys.indexOf(pitch % 12) >= 0
        }

        // white keys before this pitch
        function whiteIndex(pitch) {
            const octave = Math.floor(pitch / 12)
            const whitesInOctave = [0, 1, 1, 2, 2, 3, 4, 4, 5, 5, 6, 6]
            return octave * 7 + whitesInOctave[pitch % 12]
        }

        // piano keys keep their colors in every theme; the marks use the theme's
        // the same marks whatever triggers the articulation: the pad's note, the other pads of its articulation (its bank),
        // and the key triggering it when it's a keyswitch
        function keyColor(info, black, selected, hovered) {
            if (selected) {
                return ui.theme.accentColor
            }
            if (Boolean(info.padKeyswitch)) {
                return "#E0393E"
            }
            if (Boolean(info.sameArticulationPad)) {
                return Qt.tint(black ? "#262626" : "#F2F2F2", Utils.colorWithAlpha(ui.theme.linkColor, 0.45))
            }
            if (hovered) {
                return black ? "#4A4A4A" : "#DADADA"
            }
            return black ? "#262626" : "#F2F2F2"
        }
        readonly property int leftColumnWidth: 420

        // the noteheads' glyphs, by value and by name (a dropdown's list items only know their text)
        readonly property var glyphByValue: {
            let result = {}
            for (const symbol of padModel.noteheadSymbols) {
                result[symbol.value] = symbol.glyph
            }
            return result
        }

        readonly property var glyphByText: {
            let result = {}
            for (const symbol of padModel.noteheadSymbols) {
                result[symbol.text] = symbol.glyph
            }
            return result
        }

        function glyphOf(value) {
            return prv.glyphByValue[value] ?? ""
        }

        function glyphOfText(text) {
            return prv.glyphByText[text] ?? ""
        }

    }

    // a notehead in the music font, centered: a SMuFL glyph sits on its baseline, one staff space high
    component NoteheadGlyph: Item {
        property string glyph: ""

        Layout.preferredWidth: 24
        Layout.fillHeight: true
        clip: true

        Text {
            anchors.horizontalCenter: parent.horizontalCenter
            anchors.baseline: parent.verticalCenter
            font.family: padModel.musicFontFamily
            font.pixelSize: 40
            color: ui.theme.fontPrimaryColor
            text: parent.glyph
        }
    }

    // the notes of the sound library: the pad's one, the other pads' ones and the keyswitches of the articulation map
    component DrumPadKeyboard: Item {
        id: keyboard

        readonly property var keys: padModel.keyboardKeys // read once per change, not per key
        readonly property real whiteKeyWidth: 16
        readonly property real blackKeyWidth: keyboard.whiteKeyWidth * 0.62

        // x of a key's middle
        function keyCenter(pitch) {
            return prv.isBlack(pitch) ? prv.whiteIndex(pitch) * keyboard.whiteKeyWidth
                                           : (prv.whiteIndex(pitch) + 0.5) * keyboard.whiteKeyWidth
        }

        implicitWidth: prv.whiteKeyCount * keyboard.whiteKeyWidth
        implicitHeight: 88

        Repeater {
            model: 128

            Rectangle {
                id: key

                required property int index

                readonly property int pitch: index
                readonly property bool black: prv.isBlack(key.pitch)
                readonly property var keyInfo: keyboard.keys[key.pitch] ?? ({})
                readonly property bool selected: key.pitch === padModel.soundPitch

                x: key.black ? prv.whiteIndex(key.pitch) * keyboard.whiteKeyWidth - keyboard.blackKeyWidth / 2
                             : prv.whiteIndex(key.pitch) * keyboard.whiteKeyWidth
                z: key.black ? 1 : 0
                width: key.black ? keyboard.blackKeyWidth : keyboard.whiteKeyWidth
                height: key.black ? keyboard.height * 0.6 : keyboard.height

                // piano keys keep their colors in every theme; the marks use the theme's
                color: prv.keyColor(key.keyInfo, key.black, key.selected, keyMouseArea.containsMouse)

                border.color: "#808080"
                border.width: key.black ? 0 : 0.5

                // the octaves
                Text {
                    visible: key.pitch % 12 === 0
                    anchors.horizontalCenter: parent.horizontalCenter
                    anchors.bottom: parent.bottom
                    anchors.bottomMargin: 2
                    font.family: ui.theme.bodyFont.family
                    font.pixelSize: 9
                    color: key.selected ? ui.theme.fontPrimaryColor : "#606060"
                    text: "C" + (Math.floor(key.pitch / 12) - 5 + padModel.middleCOctave)
                }

                MouseArea {
                    id: keyMouseArea

                    anchors.fill: parent
                    hoverEnabled: true

                    onClicked: {
                        padModel.soundPitch = key.pitch
                    }

                    // the note, and below what plays it: the articulation of a keyswitch, the other pad
                    onContainsMouseChanged: {
                        if (!keyMouseArea.containsMouse) {
                            keyTip.hideFor(key)
                            return
                        }

                        const detail = key.keyInfo.detail ?? ""
                        keyTip.showFor(key, key.keyInfo.noteText ?? "", detail)
                    }
                }
            }
        }
    }

    ColumnLayout {
        id: contentColumn

        anchors.fill: parent
        anchors.margins: 12
        spacing: 12

        // ---- the kit's file, the staff's VST instrument window
        RowLayout {
            Layout.fillWidth: true
            spacing: 4

            FlatButton {
                icon: IconCode.NEW_FILE
                toolTipTitle: qsTrc("notation/percussion", "New kit")
                toolTipDescription: qsTrc("notation/percussion", "Removes all the pads, to build a kit from scratch")
                onClicked: padModel.newKit()
            }

            FlatButton {
                icon: IconCode.OPEN_FILE
                toolTipTitle: qsTrc("notation/percussion", "Load…")
                toolTipDescription: qsTrc("notation/percussion", "Replaces the whole kit with a drumset file")
                onClicked: padModel.loadKit()
            }

            FlatButton {
                icon: IconCode.SAVE
                toolTipTitle: qsTrc("notation/percussion", "Save as…")
                toolTipDescription: qsTrc("notation/percussion", "Saves the whole kit, with this pad as edited, to a drumset file")
                onClicked: padModel.saveKit()
            }

            Item { Layout.fillWidth: true }

            //! NOTE: the track's articulation map (a new one if it has none)
            FlatButton {
                visible: padModel.hasArticulationMap || padModel.hasInstrumentEditor
                transparent: true
                icon: IconCode.EDIT
                toolTipTitle: qsTrc("notation", "Articulation map editor")
                onClicked: padModel.openArticulationMapEditor()
            }

            //! NOTE: its sounds can be browsed there too
            FlatButton {
                visible: padModel.hasInstrumentEditor
                transparent: true
                icon: IconCode.PLUGIN
                toolTipTitle: qsTrc("playback", "Open instrument window")
                onClicked: padModel.openInstrumentEditor()
            }
        }

        // in a library with keyswitched banks (hits, rolls...): the bank the pad's note is in
        RowLayout {
            Layout.fillWidth: true
            visible: padModel.hasArticulationMap
            spacing: 8

            StyledTextLabel {
                Layout.preferredWidth: prv.labelWidth
                horizontalAlignment: Text.AlignLeft
                text: qsTrc("notation/percussion", "Articulation:")
            }

            // the articulation and its trigger, as in the menu
            FlatButton {
                id: articulationButton

                Layout.preferredWidth: 320
                Layout.preferredHeight: 30

                toolTipTitle: qsTrc("notation/percussion", "Articulation")
                toolTipDescription: qsTrc("notation/percussion", "The articulation of the map the pad's note is in (e.g. a bank of rolls): "
                                                                 + "its keyswitch is always sent before the pad's notes")

                contentItem: RowLayout {
                    spacing: 8

                    StyledTextLabel {
                        Layout.fillWidth: true
                        Layout.leftMargin: 8
                        horizontalAlignment: Text.AlignLeft
                        text: padModel.articulationName
                    }

                    StyledTextLabel {
                        opacity: 0.6
                        text: padModel.articulationTrigger
                    }

                    StyledIconLabel {
                        Layout.rightMargin: 8
                        iconCode: IconCode.SMALL_ARROW_DOWN
                    }
                }

                onClicked: {
                    articulationMenuLoader.toggleOpened(padModel.articulationMenuItems)
                }

                StyledMenuLoader {
                    id: articulationMenuLoader

                    onHandleMenuItem: function(itemId) {
                        padModel.setArticulation(itemId)
                    }
                }
            }
        }


        // ---- the pad's name, and its note learnt from the MIDI keyboard
        RowLayout {
            Layout.fillWidth: true
            spacing: 8

            StyledTextLabel {
                Layout.preferredWidth: prv.labelWidth
                horizontalAlignment: Text.AlignLeft
                text: qsTrc("notation/percussion", "Name:")
            }

            // at each key: OK, Apply or another pad clicked right after typing don't take the focus (no editing finished)
            TextInputField {
                Layout.preferredWidth: prv.leftColumnWidth - prv.labelWidth - 8
                currentText: padModel.padName
                onTextEdited: function(newTextValue) {
                    padModel.padName = newTextValue
                }
            }

            Item { Layout.fillWidth: true }

            FlatButton {
                text: padModel.learningMidi ? qsTrc("notation/percussion", "Press a key…") : qsTrc("notation/percussion", "Learn MIDI")
                accentButton: padModel.learningMidi
                toolTipTitle: qsTrc("notation/percussion", "Learn MIDI")
                toolTipDescription: qsTrc("notation/percussion", "Press a key of the MIDI keyboard: the pad takes its note")
                onClicked: padModel.learningMidi = !padModel.learningMidi
            }
        }

        // the whole keyboard, small, with the same marks: nothing is hidden; the frame is the part shown below (clicked or
        // dragged: the keyboard below goes there)
        Item {
            id: keyboardOverview

            Layout.fillWidth: true
            Layout.preferredHeight: 18

            readonly property var keys: padModel.keyboardKeys
            readonly property real whiteKeyWidth: keyboardOverview.width / prv.whiteKeyCount
            readonly property real scale: keyboardFlickable.contentWidth > 0 ? keyboardOverview.width / keyboardFlickable.contentWidth : 0

            Repeater {
                model: 128

                Rectangle {
                    id: overviewKey

                    required property int index

                    readonly property bool black: prv.isBlack(overviewKey.index)
                    readonly property var keyInfo: keyboardOverview.keys[overviewKey.index] ?? ({})
                    readonly property bool selected: overviewKey.index === padModel.soundPitch
                    readonly property real blackWidth: keyboardOverview.whiteKeyWidth * 0.62

                    x: overviewKey.black ? prv.whiteIndex(overviewKey.index) * keyboardOverview.whiteKeyWidth - overviewKey.blackWidth / 2
                                         : prv.whiteIndex(overviewKey.index) * keyboardOverview.whiteKeyWidth
                    z: overviewKey.black ? 1 : 0
                    width: overviewKey.black ? overviewKey.blackWidth : keyboardOverview.whiteKeyWidth
                    height: overviewKey.black ? keyboardOverview.height * 0.6 : keyboardOverview.height

                    color: prv.keyColor(overviewKey.keyInfo, overviewKey.black, overviewKey.selected, false)
                    border.color: "#808080"
                    border.width: overviewKey.black ? 0 : 0.5

                }
            }

            Rectangle {
                z: 2
                x: keyboardFlickable.contentX * keyboardOverview.scale
                width: keyboardFlickable.width * keyboardOverview.scale
                height: keyboardOverview.height

                color: Utils.colorWithAlpha(ui.theme.fontPrimaryColor, 0.1)
                border.color: ui.theme.fontPrimaryColor
                border.width: 1.5
                radius: 2
            }

            MouseArea {
                anchors.fill: parent
                z: 3

                function moveTo(x) {
                    if (keyboardOverview.scale <= 0) {
                        return
                    }

                    const maxX = Math.max(0, keyboardFlickable.contentWidth - keyboardFlickable.width)
                    keyboardFlickable.contentX = Math.max(0, Math.min(maxX, x / keyboardOverview.scale - keyboardFlickable.width / 2))
                }

                onPressed: function(mouse) { moveTo(mouse.x) }
                onPositionChanged: function(mouse) {
                    if (pressed) {
                        moveTo(mouse.x)
                    }
                }
            }
        }

        // wider than the dialog: scrolled to the pad's note and its articulation's keyswitch
        StyledFlickable {
            id: keyboardFlickable

            Layout.fillWidth: true
            Layout.preferredHeight: drumPadKeyboard.implicitHeight + 12 // the scroll bar below the keys

            contentWidth: drumPadKeyboard.implicitWidth
            contentHeight: drumPadKeyboard.implicitHeight
            flickableDirection: Flickable.HorizontalFlick
            boundsBehavior: Flickable.StopAtBounds
            clip: true

            // laid out after the pad is known (opening)
            onWidthChanged: {
                if (keyboardFlickable.width > 0) {
                    keyboardFlickable.ensureSoundVisible()
                }
            }

            // the pad's note, and its articulation's keyswitches when they fit with it
            function ensureSoundVisible() {
                if (padModel.soundPitch < 0) {
                    return
                }

                const keys = padModel.keyboardKeys
                let low = drumPadKeyboard.keyCenter(padModel.soundPitch)
                let high = low
                for (let pitch = 0; pitch < keys.length; ++pitch) {
                    if (Boolean(keys[pitch].padKeyswitch)) {
                        low = Math.min(low, drumPadKeyboard.keyCenter(pitch))
                        high = Math.max(high, drumPadKeyboard.keyCenter(pitch))
                    }
                }

                const margin = 2 * drumPadKeyboard.whiteKeyWidth
                if (high - low > keyboardFlickable.width - 2 * margin) {
                    keyboardFlickable.ensurePitchVisible(padModel.soundPitch)
                    return
                }

                if (low >= keyboardFlickable.contentX + margin && high <= keyboardFlickable.contentX + keyboardFlickable.width - margin) {
                    return
                }

                const maxX = Math.max(0, keyboardFlickable.contentWidth - keyboardFlickable.width)
                keyboardFlickable.contentX = Math.max(0, Math.min(maxX, (low + high) / 2 - keyboardFlickable.width / 2))
            }

            function ensurePitchVisible(pitch) {
                const x = drumPadKeyboard.keyCenter(pitch)
                const margin = 2 * drumPadKeyboard.whiteKeyWidth
                if (x >= keyboardFlickable.contentX + margin && x <= keyboardFlickable.contentX + keyboardFlickable.width - margin) {
                    return
                }

                const maxX = Math.max(0, keyboardFlickable.contentWidth - keyboardFlickable.width)
                keyboardFlickable.contentX = Math.max(0, Math.min(maxX, x - keyboardFlickable.width / 2))
            }

            DrumPadKeyboard {
                id: drumPadKeyboard

                width: implicitWidth
                height: implicitHeight
            }

            Connections {
                target: padModel

                function onSoundChanged() {
                    Qt.callLater(keyboardFlickable.ensureSoundVisible)
                }
            }
        }


        SeparatorLine { Layout.fillWidth: true }

        // ---- appearance (as in the Customize kit dialog), in two columns
        RowLayout {
            Layout.fillWidth: true
            spacing: 16

            // a nested layout fills the row by default: kept to the left column, as the noteheads below
            GridLayout {
                id: noteAndNoteheadGrid

                Layout.fillWidth: false
                Layout.preferredWidth: prv.leftColumnWidth
                Layout.maximumWidth: prv.leftColumnWidth
                Layout.alignment: Qt.AlignTop
                columns: 2
                columnSpacing: 8
                rowSpacing: 8

                // the pad's note: chosen on the keyboard above (a key clicked is played) or learnt from the MIDI keyboard
                StyledTextLabel {
                    Layout.preferredWidth: prv.labelWidth
                    horizontalAlignment: Text.AlignLeft
                    text: qsTrc("notation/percussion", "Note:")
                }

                RowLayout {
                    Layout.fillWidth: true
                    spacing: 8

                    StyledTextLabel {
                        horizontalAlignment: Text.AlignLeft
                        font: ui.theme.bodyBoldFont
                        text: padModel.soundPitchText
                    }

                    // e.g. another pad playing the same note in the same articulation: whole on hover when cut
                    StyledTextLabel {
                        id: soundInfoLabel

                        Layout.fillWidth: true
                        horizontalAlignment: Text.AlignLeft
                        elide: Text.ElideRight
                        text: padModel.soundInfo

                        MouseArea {
                            anchors.fill: parent
                            hoverEnabled: soundInfoLabel.truncated
                            acceptedButtons: Qt.NoButton

                            onContainsMouseChanged: {
                                if (containsMouse) {
                                    ui.tooltip.show(soundInfoLabel, soundInfoLabel.text)
                                } else {
                                    ui.tooltip.hide(soundInfoLabel)
                                }
                            }
                        }
                    }
                }

                StyledTextLabel {
                    Layout.preferredWidth: prv.labelWidth
                    horizontalAlignment: Text.AlignLeft
                    text: qsTrc("notation/percussion", "Notehead group:")
                }

                StyledDropdown {
                    Layout.fillWidth: true
                    enabled: !padModel.customNoteheads
                    model: padModel.noteheadGroups
                    currentIndex: indexOfValue(padModel.noteheadGroup)
                    onActivated: function(index, value) {
                        padModel.noteheadGroup = value
                    }
                }
            }

            // the pad on the instrument's staff: as wide as in the Customize kit dialog, centered in its column, as high as
            // the note and notehead lines
            Item {
                Layout.fillWidth: true
                Layout.preferredHeight: Math.max(60, noteAndNoteheadGrid.implicitHeight)

                Rectangle {
                    anchors.centerIn: parent
                    width: 120
                    height: parent.height

                    // as the Customize kit dialog's example: the theme's colors
                    color: ui.theme.textFieldColor
                    border.color: ui.theme.strokeColor
                    border.width: 1
                    radius: 3

                    //! NOTE: PaintedEngravingItem paints into its parent's rect
                    Item {
                        anchors.fill: parent
                        anchors.margins: 1

                        PaintedEngravingItem {
                            anchors.fill: parent

                            engravingItem: padModel.previewItem
                            numStaffLines: padModel.numStaffLines
                            spatium: 6
                            color: ui.theme.fontPrimaryColor
                        }
                    }
                }
            }
        }

        CheckBox {
            text: qsTrc("notation/percussion", "Edit noteheads")
            checked: padModel.customNoteheads
            onClicked: padModel.customNoteheads = !checked
        }

        RowLayout {
            Layout.fillWidth: true
            spacing: 16

            // the noteheads by duration: the group's ones until edited
            Rectangle {
                Layout.preferredWidth: prv.leftColumnWidth
                Layout.preferredHeight: noteheadsColumn.implicitHeight + 16
                Layout.alignment: Qt.AlignTop

                color: "transparent"
                border.color: ui.theme.strokeColor
                border.width: 1
                radius: 3

                ColumnLayout {
                    id: noteheadsColumn

                    anchors.fill: parent
                    anchors.margins: 8
                    spacing: 8

                    enabled: padModel.customNoteheads

                    // by NoteHeadType: whole, half, quarter, double whole
                    Repeater {
                        model: [
                            { type: 2, text: qsTrc("notation/percussion", "Quarter note") },
                            { type: 1, text: qsTrc("notation/percussion", "Half note") },
                            { type: 0, text: qsTrc("notation/percussion", "Whole note") },
                            { type: 3, text: qsTrc("notation/percussion", "Double whole note") }
                        ]

                        delegate: RowLayout {
                            required property var modelData

                            Layout.fillWidth: true
                            spacing: 8

                            StyledTextLabel {
                                Layout.preferredWidth: prv.labelWidth
                                horizontalAlignment: Text.AlignLeft
                                text: modelData.text
                            }

                            StyledDropdown {
                                id: symbolDropdown

                                Layout.fillWidth: true
                                model: padModel.noteheadSymbols
                                currentIndex: indexOfValue(padModel.customNoteheadSymbols[modelData.type])

                                contentItem: RowLayout {
                                    property string text: ""

                                    anchors.fill: parent
                                    anchors.leftMargin: 6
                                    anchors.rightMargin: 8
                                    spacing: 4

                                    NoteheadGlyph {
                                        glyph: prv.glyphOf(symbolDropdown.currentValue)
                                    }

                                    StyledTextLabel {
                                        Layout.fillWidth: true
                                        horizontalAlignment: Text.AlignLeft
                                        text: symbolDropdown.currentText
                                    }

                                    StyledIconLabel {
                                        iconCode: IconCode.SMALL_ARROW_DOWN
                                    }
                                }

                                contentListItem: RowLayout {
                                    property string text: ""
                                    property bool truncated: listLabel.truncated

                                    anchors.fill: parent
                                    anchors.leftMargin: 6
                                    spacing: 4

                                    NoteheadGlyph {
                                        glyph: prv.glyphOfText(parent.text)
                                    }

                                    StyledTextLabel {
                                        id: listLabel
                                        Layout.fillWidth: true
                                        horizontalAlignment: Text.AlignLeft
                                        text: parent.text
                                    }
                                }

                                onActivated: function(index, value) {
                                    padModel.setCustomNoteheadSymbol(modelData.type, value)
                                }
                            }
                        }

                    }
                }
            }

            GridLayout {
                Layout.fillWidth: true
                Layout.alignment: Qt.AlignTop
                Layout.topMargin: 8
                columns: 2
                columnSpacing: 8
                rowSpacing: 8

                StyledTextLabel {
                    horizontalAlignment: Text.AlignLeft
                    text: qsTrc("notation/percussion", "Default voice:")
                }

                StyledDropdown {
                    Layout.fillWidth: true
                    model: [ { text: "1", value: 0 }, { text: "2", value: 1 }, { text: "3", value: 2 }, { text: "4", value: 3 } ]
                    currentIndex: indexOfValue(padModel.voice)
                    onActivated: function(index, value) {
                        padModel.voice = value
                    }
                }

                StyledTextLabel {
                    horizontalAlignment: Text.AlignLeft
                    text: qsTrc("notation/percussion", "Staff line:")
                }

                IncrementalPropertyControl {
                    Layout.fillWidth: true
                    currentValue: padModel.staffLine
                    decimals: 0
                    step: 1
                    minValue: -12
                    maxValue: 24
                    onValueEdited: function(newValue) {
                        padModel.staffLine = newValue
                    }
                }

                StyledTextLabel {
                    horizontalAlignment: Text.AlignLeft
                    text: qsTrc("notation/percussion", "Shortcut:")
                }

                FlatButton {
                    Layout.fillWidth: true
                    text: padModel.shortcut !== "" ? padModel.shortcut : qsTrc("shortcuts", "None")
                    onClicked: padModel.defineShortcut()
                }

                StyledTextLabel {
                    horizontalAlignment: Text.AlignLeft
                    text: qsTrc("notation/percussion", "Stem direction:")
                }

                StyledDropdown {
                    Layout.fillWidth: true
                    model: [
                        { text: qsTrc("notation/percussion", "Auto"), value: 0 },
                        { text: qsTrc("notation/percussion", "Up"), value: 1 },
                        { text: qsTrc("notation/percussion", "Down"), value: 2 }
                    ]
                    currentIndex: indexOfValue(padModel.stemDirection)
                    onActivated: function(index, value) {
                        padModel.stemDirection = value
                    }
                }
            }
        }

        // ---- the dialog's own buttons
        RowLayout {
            Layout.fillWidth: true
            Layout.topMargin: 4

            // in the Percussion panel's order
            FlatButton {
                text: qsTrc("notation/percussion", "Previous pad")
                orientation: Qt.Horizontal
                minWidth: prv.navButtonWidth
                toolTipTitle: qsTrc("notation/percussion", "Previous pad")
                toolTipDescription: qsTrc("notation/percussion", "Edits the previous pad of the Percussion panel")
                onClicked: padModel.editAdjacentPad(-1)
            }

            FlatButton {
                text: qsTrc("notation/percussion", "Next pad")
                orientation: Qt.Horizontal
                minWidth: prv.navButtonWidth
                toolTipTitle: qsTrc("notation/percussion", "Next pad")
                toolTipDescription: qsTrc("notation/percussion", "Edits the next pad of the Percussion panel")
                onClicked: padModel.editAdjacentPad(1)
            }

            Item { Layout.fillWidth: true }

            ButtonBox {

                buttonLayout: ButtonBoxModel.MacLayout

                // applied, the dialog stays open (e.g. to edit another pad)
                FlatButton {
                    text: qsTrc("global", "Apply")
                    enabled: padModel.hasChanges
                    buttonRole: ButtonBoxModel.CustomRole
                    buttonId: ButtonBoxModel.CustomButton + 1 // a standard id would take the Apply role, placed after OK
                    minWidth: prv.dialogButtonWidth
                    onClicked: padModel.apply()
                }

                // like the window's close button and Escape: nothing applied
                FlatButton {
                    text: qsTrc("global", "Cancel")
                    buttonRole: ButtonBoxModel.RejectRole
                    buttonId: ButtonBoxModel.Cancel
                    minWidth: prv.dialogButtonWidth
                    onClicked: root.close()
                }

                FlatButton {
                    text: qsTrc("global", "OK")
                    buttonRole: ButtonBoxModel.AcceptRole
                    buttonId: ButtonBoxModel.Ok
                    accentButton: true
                    minWidth: prv.dialogButtonWidth
                    onClicked: {
                        padModel.apply()
                        root.close()
                    }
                }
            }
        }
    }

    // the keyboard's tooltip: two centered lines (the app's tooltip aligns its text left)
    Rectangle {
        id: keyTip

        property Item target: null

        function showFor(item, title, detail) {
            keyTip.target = item
            keyTipTitle.text = title
            keyTipDetail.text = detail

            // below the key: above, it would hide the keyboard's overview
            const pos = item.mapToItem(keyTip.parent, item.width / 2, item.height)
            keyTip.x = Math.max(4, Math.min(keyTip.parent.width - keyTip.width - 4, pos.x - keyTip.width / 2))
            keyTip.y = pos.y + 4
            keyTip.visible = true
        }

        function hideFor(item) {
            if (keyTip.target === item) {
                keyTip.visible = false
                keyTip.target = null
            }
        }

        visible: false
        z: 100

        width: keyTipColumn.implicitWidth + 16
        height: keyTipColumn.implicitHeight + 8

        color: ui.theme.popupBackgroundColor
        border.color: ui.theme.strokeColor
        border.width: 1
        radius: 3

        Column {
            id: keyTipColumn

            anchors.centerIn: parent
            spacing: 2

            StyledTextLabel {
                id: keyTipTitle
                anchors.horizontalCenter: parent.horizontalCenter
                horizontalAlignment: Text.AlignHCenter
                font: ui.theme.bodyBoldFont
            }

            StyledTextLabel {
                id: keyTipDetail
                anchors.horizontalCenter: parent.horizontalCenter
                horizontalAlignment: Text.AlignHCenter
                visible: text !== ""
            }
        }
    }
}
