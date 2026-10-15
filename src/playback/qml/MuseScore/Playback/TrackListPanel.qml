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

import "internal"

Item {
    id: root

    property NavigationSection navigationSection: null
    property int contentNavigationPanelOrderStart: 1

    property alias contextMenuModel: contextMenuModel

    TrackListModel {
        id: trackListModel
    }

    TrackListContextMenuModel {
        id: contextMenuModel

        Component.onCompleted: {
            contextMenuModel.load()
        }
    }

    NavigationPanel {
        id: navPanel

        name: "TrackListPanel"
        section: root.navigationSection
        order: root.contentNavigationPanelOrderStart
        direction: NavigationPanel.Both
        enabled: root.visible
    }

    ZoomContainer {
        id: zoomContainer

        anchors.fill: parent

        zoom: contextMenuModel.zoom

        StyledListView {
            id: listView

            anchors.top: parent.top
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.bottom: stripArea.top

            model: trackListModel

            //! NOTE: stays at the top; its global Mute/Solo buttons are in the same columns as the rows' ones
            headerPositioning: ListView.OverlayHeader
            header: Rectangle {
                width: ListView.view.width
                height: 32 + metronomeRow.height + videoRow.height
                z: 2

                color: ui.theme.backgroundSecondaryColor

                Row {
                    id: globalButtonsRow

                    height: 32
                    anchors.right: parent.right
                    //! NOTE: lined up with the rows' buttons, left of their meters (2 × 3 px + 1, then 6)
                    anchors.rightMargin: 4 + 7 + 6
                    anchors.top: parent.top

                    spacing: 4

                    //! NOTE: the rows' sound column
                    Item {
                        width: 20
                        height: 20
                    }

                    GlobalMuteSoloButtons {
                        spacing: 4

                        model: trackListModel
                        toolTipsEnabled: false

                        navigationPanel: navPanel
                        navigationRow: 0
                        navigationColumnStart: 1
                    }

                    //! NOTE: the rows' articulation map and editor columns
                    Item {
                        width: 20
                        height: 20
                        visible: trackListModel.hasArticulationMapColumn
                    }

                    Item {
                        width: 20
                        height: 20
                        visible: trackListModel.hasEditorColumn
                    }
                }

                SeparatorLine {
                    anchors.top: globalButtonsRow.bottom
                }

                //! NOTE: the metronome's row, first: its Mute in the parts' column
                Rectangle {
                    id: metronomeRow

                    readonly property MixerChannelItem channelItem: trackListModel.metronomeChannelItem

                    anchors.top: globalButtonsRow.bottom
                    width: parent.width
                    height: visible ? 28 : 0
                    visible: Boolean(channelItem) && trackListModel.metronomeVisible

                    color: ui.theme.backgroundPrimaryColor

                    Rectangle {
                        id: metronomeColorStrip

                        anchors.left: parent.left
                        anchors.leftMargin: 4
                        anchors.verticalCenter: parent.verticalCenter

                        width: 8
                        height: 20
                        radius: 2

                        color: ui.theme.accentColor
                    }

                    Row {
                        id: metronomeMeter

                        anchors.right: parent.right
                        anchors.rightMargin: 4
                        anchors.verticalCenter: parent.verticalCenter

                        spacing: 1

                        VolumePressureMeter {
                            meterLength: 18
                            meterThickness: 3
                            overloadLength: 2
                            currentVolumePressure: metronomeRow.channelItem ? metronomeRow.channelItem.leftChannelPressure : -60
                        }

                        VolumePressureMeter {
                            meterLength: 18
                            meterThickness: 3
                            overloadLength: 2
                            currentVolumePressure: metronomeRow.channelItem ? metronomeRow.channelItem.rightChannelPressure : -60
                        }
                    }

                    StyledTextLabel {
                        anchors.left: metronomeColorStrip.right
                        anchors.leftMargin: 6
                        anchors.right: metronomeButtonsRow.left
                        anchors.rightMargin: 6
                        anchors.verticalCenter: parent.verticalCenter

                        horizontalAlignment: Text.AlignLeft
                        text: qsTrc("playback", "Metronome")
                    }

                    Row {
                        id: metronomeButtonsRow

                        anchors.right: metronomeMeter.left
                        anchors.rightMargin: 6
                        anchors.verticalCenter: parent.verticalCenter

                        spacing: 4

                        //! NOTE: the parts' sound column
                        Item {
                            width: 20
                            height: 20
                        }

                        //! NOTE: Mute only (no Solo button for the metronome)
                        ChannelMuteSoloButtons {
                            spacing: 4

                            channelItem: metronomeRow.channelItem

                            navigationPanel: navPanel
                            navigationName: "MetronomeRow"
                            muteNavigationRow: 1
                            muteNavigationColumn: 1
                            accessibleName: qsTrc("playback", "Metronome")

                            onMuteToggled: function(muted) {
                                metronomeRow.channelItem.muted = muted
                            }
                        }

                        //! NOTE: the parts' solo column
                        Item {
                            width: 20
                            height: 20
                        }

                        Item {
                            width: 20
                            height: 20
                            visible: trackListModel.hasArticulationMapColumn
                        }

                        Item {
                            width: 20
                            height: 20
                            visible: trackListModel.hasEditorColumn
                        }
                    }

                    SeparatorLine {
                        anchors.bottom: parent.bottom
                    }
                }

                //! NOTE: the video's row: its buttons in the parts' columns (Load video in the sound one)
                Rectangle {
                    id: videoRow

                    readonly property MixerChannelItem channelItem: trackListModel.videoChannelItem
                    readonly property color trackColor: channelItem && channelItem.hasCustomColor ? channelItem.color : ui.theme.accentColor

                    anchors.top: metronomeRow.bottom
                    width: parent.width
                    height: visible ? 28 : 0
                    visible: Boolean(channelItem) && trackListModel.videoVisible

                    color: ui.theme.backgroundPrimaryColor

                    Rectangle {
                        id: videoColorStrip

                        anchors.left: parent.left
                        anchors.leftMargin: 4
                        anchors.verticalCenter: parent.verticalCenter

                        width: 8
                        height: 20
                        radius: 2

                        color: videoRow.trackColor

                        MouseArea {
                            anchors.fill: parent
                            anchors.margins: -2

                            onClicked: function(mouse) {
                                videoColorMenu.show(Qt.point(mouse.x, mouse.y))
                            }
                        }

                        ContextMenuLoader {
                            id: videoColorMenu

                            items: [
                                { id: "editColor", title: qsTrc("playback", "Edit color…") },
                                { id: "resetColor", title: qsTrc("playback", "Reset color"), enabled: videoRow.channelItem ? videoRow.channelItem.hasCustomColor : false }
                            ]

                            onHandleMenuItem: function(itemId) {
                                if (itemId === "editColor") {
                                    videoColorPickerModel.selectColor(videoRow.trackColor, false)
                                } else if (itemId === "resetColor") {
                                    trackListModel.resetVideoColor()
                                }
                            }
                        }

                        ColorPickerModel {
                            id: videoColorPickerModel

                            onColorSelected: function(color) {
                                trackListModel.setVideoColor(color)
                            }
                        }
                    }

                    Row {
                        id: videoMeter

                        anchors.right: parent.right
                        anchors.rightMargin: 4
                        anchors.verticalCenter: parent.verticalCenter

                        spacing: 1

                        VolumePressureMeter {
                            meterLength: 18
                            meterThickness: 3
                            overloadLength: 2
                            currentVolumePressure: videoRow.channelItem ? videoRow.channelItem.leftChannelPressure : -60
                        }

                        VolumePressureMeter {
                            meterLength: 18
                            meterThickness: 3
                            overloadLength: 2
                            currentVolumePressure: videoRow.channelItem ? videoRow.channelItem.rightChannelPressure : -60
                        }
                    }

                    StyledTextLabel {
                        anchors.left: videoColorStrip.right
                        anchors.leftMargin: 6
                        anchors.right: videoButtonsRow.left
                        anchors.rightMargin: 6
                        anchors.verticalCenter: parent.verticalCenter

                        horizontalAlignment: Text.AlignLeft
                        text: trackListModel.hasVideo ? qsTrc("playback", "Video") : qsTrc("playback", "No video")
                        opacity: trackListModel.hasVideo ? 1 : 0.5
                    }

                    Row {
                        id: videoButtonsRow

                        anchors.right: videoMeter.left
                        anchors.rightMargin: 6
                        anchors.verticalCenter: parent.verticalCenter

                        spacing: 4

                        FlatButton {
                            width: 20
                            height: 20

                            transparent: true
                            icon: IconCode.OPEN_FILE

                            navigation.panel: navPanel
                            navigation.name: "LoadVideoButton"
                            navigation.row: 2
                            navigation.column: 0
                            navigation.accessible.name: qsTrc("playback", "Load video")

                            onClicked: {
                                trackListModel.chooseVideoFile()
                            }
                        }

                        ChannelMuteSoloButtons {
                            spacing: 4

                            enabled: trackListModel.hasVideo
                            channelItem: videoRow.channelItem

                            navigationPanel: navPanel
                            navigationName: "VideoRow"
                            muteNavigationRow: 2
                            muteNavigationColumn: 1
                            soloNavigationRow: 2
                            soloNavigationColumn: 2
                            accessibleName: qsTrc("playback", "Video")

                            onMuteToggled: function(muted) {
                                videoRow.channelItem.muted = muted
                            }

                            onSoloToggled: function(solo) {
                                videoRow.channelItem.solo = solo
                            }
                        }

                        //! NOTE: the parts' articulation map and editor columns
                        Item {
                            width: 20
                            height: 20
                            visible: trackListModel.hasArticulationMapColumn
                        }

                        Item {
                            width: 20
                            height: 20
                            visible: trackListModel.hasEditorColumn
                        }
                    }

                    SeparatorLine {
                        anchors.bottom: parent.bottom
                    }
                }
            }

            delegate: Item {
                id: rowItem

                required property int index
                required property MixerChannelItem channelItem
                required property bool partVisible
                required property bool hasArticulationMap
                required property bool isVstInstrument
                required property bool articulationMapEditorOpened
                required property bool instrumentEditorOpened

                readonly property InputResourceItem soundItem: channelItem ? channelItem.inputResourceItem : null
                //! NOTE: an Aux FX/Aux Group bus's row: color, Mute and Solo only (Solo disabled for an FX bus)
                readonly property bool isAuxBus: channelItem ? channelItem.type === MixerChannelItem.Aux : false
                readonly property color trackColor: channelItem && channelItem.hasCustomColor ? channelItem.color : ui.theme.accentColor

                width: ListView.view.width
                height: 28

                //! NOTE: selection, like the Mixer's (click, Cmd/Ctrl+click, Shift+click): the color, Mute and Solo of
                //! a selected row apply to the whole selection
                Rectangle {
                    anchors.fill: parent
                    color: ui.theme.accentColor
                    opacity: 0.3
                    visible: rowItem.channelItem ? rowItem.channelItem.selected : false
                }

                MouseArea {
                    anchors.fill: parent
                    acceptedButtons: Qt.LeftButton | Qt.RightButton

                    onClicked: function(mouse) {
                        //! NOTE: right click: the track presets menu, like in the Mixer
                        if (mouse.button === Qt.RightButton) {
                            if (rowItem.isAuxBus) {
                                return
                            }
                            trackPresetMenu.show(Qt.point(mouse.x, mouse.y), rowItem.channelItem.trackPresetMenuItems())
                            return
                        }

                        // Qt.ControlModifier is Cmd on macOS and Ctrl on Windows/Linux
                        trackListModel.selectRow(rowItem.index, (mouse.modifiers & Qt.ControlModifier) !== 0,
                                                 (mouse.modifiers & Qt.ShiftModifier) !== 0)
                    }

                    ContextMenuLoader {
                        id: trackPresetMenu

                        onHandleMenuItem: function(itemId) {
                            rowItem.channelItem.handleTrackPresetMenuItem(itemId)
                        }
                    }
                }

                //! NOTE: the track's color, its menu on a left click (like the Timeline's color strips)
                Rectangle {
                    id: colorStrip

                    anchors.left: parent.left
                    anchors.leftMargin: 4
                    anchors.verticalCenter: parent.verticalCenter

                    width: 8
                    height: 20
                    radius: 2

                    color: rowItem.trackColor

                    MouseArea {
                        anchors.fill: parent
                        anchors.margins: -2

                        onClicked: function(mouse) {
                            colorMenu.show(Qt.point(mouse.x, mouse.y))
                        }
                    }

                    ContextMenuLoader {
                        id: colorMenu

                        items: [
                            { id: "editColor", title: qsTrc("playback", "Edit color…") },
                            //! NOTE: on a selected row, for the whole selection, whose other rows may have a color
                            { id: "resetColor", title: qsTrc("playback", "Reset color"),
                                enabled: rowItem.channelItem.hasCustomColor || rowItem.channelItem.selected }
                        ]

                        onHandleMenuItem: function(itemId) {
                            if (itemId === "editColor") {
                                colorPickerModel.selectColor(rowItem.trackColor, false)
                            } else if (itemId === "resetColor") {
                                trackListModel.resetTrackColor(rowItem.index)
                            }
                        }
                    }

                    ColorPickerModel {
                        id: colorPickerModel

                        onColorSelected: function(color) {
                            trackListModel.setTrackColor(rowItem.index, color)
                        }
                    }
                }

                //! NOTE: the track's level, the Mixer's meters in small
                Row {
                    id: meter

                    anchors.right: parent.right
                    anchors.rightMargin: 4
                    anchors.verticalCenter: parent.verticalCenter

                    spacing: 1

                    VolumePressureMeter {
                        meterLength: 18
                        meterThickness: 3
                        overloadLength: 2
                        currentVolumePressure: rowItem.channelItem.leftChannelPressure
                    }

                    VolumePressureMeter {
                        meterLength: 18
                        meterThickness: 3
                        overloadLength: 2
                        currentVolumePressure: rowItem.channelItem.rightChannelPressure
                    }
                }

                StyledTextLabel {
                    anchors.left: colorStrip.right
                    anchors.leftMargin: 6
                    anchors.right: buttonsRow.left
                    anchors.rightMargin: 6
                    anchors.verticalCenter: parent.verticalCenter

                    horizontalAlignment: Text.AlignLeft
                    text: rowItem.channelItem ? rowItem.channelItem.title : ""
                    opacity: rowItem.partVisible ? 1 : 0.5
                }

                Row {
                    id: buttonsRow

                    anchors.right: meter.left
                    anchors.rightMargin: 6
                    anchors.verticalCenter: parent.verticalCenter

                    spacing: 4

                    //! NOTE: the Mixer's Sound menu (a bus has none: its column stays)
                    FlatButton {
                        id: soundButton

                        width: 20
                        height: 20
                        visible: !rowItem.isAuxBus

                        transparent: !soundMenu.isMenuOpened
                        icon: IconCode.SMALL_ARROW_DOWN

                        navigation.panel: navPanel
                        navigation.name: "SoundButton" + rowItem.index
                        navigation.row: rowItem.index + 3
                        navigation.column: 0
                        navigation.accessible.name: rowItem.channelItem.title + " " + qsTrc("playback", "Sound")

                        onClicked: {
                            soundMenu.requestOpen()
                        }

                        AudioResourceMenuLoader {
                            id: soundMenu

                            resourceItemModel: rowItem.soundItem
                        }
                    }

                    Item {
                        width: 20
                        height: 20
                        visible: rowItem.isAuxBus
                    }

                    ChannelMuteSoloButtons {
                        spacing: 4

                        channelItem: rowItem.channelItem

                        navigationPanel: navPanel
                        navigationName: "Row" + rowItem.index
                        muteNavigationRow: rowItem.index + 3
                        muteNavigationColumn: 1
                        soloNavigationRow: rowItem.index + 3
                        soloNavigationColumn: 2
                        accessibleName: rowItem.channelItem.title

                        onMuteToggled: function(muted) {
                            if (rowItem.channelItem.selected) {
                                trackListModel.setMutedForSelectedRows(muted)
                            } else {
                                rowItem.channelItem.muted = muted
                            }
                        }

                        onSoloToggled: function(solo) {
                            if (rowItem.channelItem.selected) {
                                trackListModel.setSoloForSelectedRows(solo)
                            } else {
                                rowItem.channelItem.setUserSolo(solo)
                            }
                        }
                    }

                    //! NOTE: fixed slots, so the buttons stay in columns whatever the track has; a column no track
                    //! of the list uses takes no room
                    Item {
                        width: 20
                        height: 20
                        visible: trackListModel.hasArticulationMapColumn

                        FlatButton {
                            width: 20
                            height: 20

                            //! NOTE: a VST instrument without a map has it too, looking disabled, for its right click menu
                            visible: rowItem.hasArticulationMap || rowItem.isVstInstrument
                            opacity: rowItem.hasArticulationMap || rowItem.articulationMapEditorOpened ? 1 : ui.theme.itemOpacityDisabled

                            transparent: true
                            icon: IconCode.ARTICULATION
                            //! NOTE: colored while the track's editor is open
                            iconColor: rowItem.articulationMapEditorOpened ? ui.theme.accentColor : ui.theme.fontPrimaryColor

                            navigation.panel: navPanel
                            navigation.name: "ArticulationMapButton" + rowItem.index
                            navigation.row: rowItem.index + 3
                            navigation.column: 3
                            navigation.accessible.name: rowItem.channelItem.title + " " + qsTrc("playback", "Edit articulation map")

                            //! NOTE: without a map (dimmed), opens the editor on a new one for the track
                            onClicked: {
                                rowItem.channelItem.handleArticulationMapMenuItem(rowItem.hasArticulationMap ? "editArticulationMap"
                                                                                                             : "newArticulationMap")
                            }

                            //! NOTE: right click: the Mixer's "Articulation map" menu
                            MouseArea {
                                anchors.fill: parent
                                acceptedButtons: Qt.RightButton

                                onClicked: function(mouse) {
                                    articulationMapMenu.show(Qt.point(mouse.x, mouse.y), rowItem.channelItem.articulationMapMenuItems())
                                }
                            }

                            ContextMenuLoader {
                                id: articulationMapMenu

                                onHandleMenuItem: function(itemId) {
                                    rowItem.channelItem.handleArticulationMapMenuItem(itemId)
                                }
                            }
                        }
                    }

                    Item {
                        width: 20
                        height: 20
                        visible: trackListModel.hasEditorColumn

                        FlatButton {
                            width: 20
                            height: 20

                            visible: rowItem.soundItem ? rowItem.soundItem.hasNativeEditorSupport : false

                            transparent: true
                            icon: IconCode.PLUGIN
                            //! NOTE: colored while the instrument's window is open
                            iconColor: rowItem.instrumentEditorOpened ? ui.theme.accentColor : ui.theme.fontPrimaryColor

                            navigation.panel: navPanel
                            navigation.name: "EditorButton" + rowItem.index
                            navigation.row: rowItem.index + 3
                            navigation.column: 4
                            navigation.accessible.name: rowItem.channelItem.title + " " + qsTrc("playback", "Open instrument window")

                            onClicked: {
                                rowItem.soundItem.requestToLaunchNativeEditorView()
                            }
                        }
                    }
                }

                SeparatorLine {
                    anchors.bottom: parent.bottom
                }
            }
        }

        //! NOTE: the first selected row's channel with the Mixer's controls, below the list (hidden from the "…" menu);
        //! its top edge is dragged to change its height (remembered), up to all of it
        Item {
            id: stripArea

            readonly property real stripZoom: 0.85
            //! NOTE: all of its controls shown: no higher
            readonly property int maxHeight: stripLoader.item ? Math.ceil(stripLoader.item.naturalHeight * stripZoom) + 1 : 0
            readonly property int minHeight: Math.min(120, maxHeight)
            //! NOTE: while its edge is dragged; then remembered (see TrackListModel::stripHeight)
            property int draggedHeight: -1

            anchors.left: parent.left
            anchors.right: parent.right
            anchors.bottom: parent.bottom

            visible: stripLoader.active
            height: !visible ? 0
                             : Math.max(minHeight, Math.min(draggedHeight >= 0 ? draggedHeight : trackListModel.stripHeight,
                                                            maxHeight, zoomContainer.contentItem.height - 80))

            SeparatorLine {
                anchors.top: parent.top
            }

            MouseArea {
                id: resizeArea

                property real pressY: 0
                property int pressHeight: 0

                anchors.top: parent.top
                anchors.left: parent.left
                anchors.right: parent.right
                height: 6
                z: 1

                cursorShape: Qt.SizeVerCursor

                onPressed: function(mouse) {
                    pressY = mapToItem(zoomContainer.contentItem, mouse.x, mouse.y).y
                    pressHeight = stripArea.height
                }

                onPositionChanged: function(mouse) {
                    if (pressed) {
                        const y = mapToItem(zoomContainer.contentItem, mouse.x, mouse.y).y
                        stripArea.draggedHeight = Math.round(pressHeight - (y - pressY))
                    }
                }

                //! NOTE: let go at its highest: stays at its highest, even once its controls take more room
                onReleased: {
                    if (stripArea.draggedHeight >= 0) {
                        trackListModel.stripHeight = stripArea.height >= stripArea.maxHeight ? 100000 : stripArea.height
                        stripArea.draggedHeight = -1
                    }
                }
            }

            ZoomContainer {
                anchors.fill: parent
                anchors.topMargin: 1

                zoom: stripArea.stripZoom

                Loader {
                    id: stripLoader

                    //! NOTE: only ever a channel: the controls never see none while the strip goes away
                    property MixerChannelItem channelItem: null

                    anchors.fill: parent

                    active: contextMenuModel.channelStripVisible && Boolean(trackListModel.stripChannelItem)
                            && Boolean(stripLoader.channelItem)

                    Component.onCompleted: {
                        stripLoader.channelItem = trackListModel.stripChannelItem
                    }

                    Connections {
                        target: trackListModel

                        function onStripChannelItemChanged() {
                            if (trackListModel.stripChannelItem) {
                                stripLoader.channelItem = trackListModel.stripChannelItem
                            }
                        }
                    }

                    sourceComponent: TrackListChannelStrip {
                        channelItem: stripLoader.channelItem

                        //! NOTE: like the rows' buttons: the first selected row's, so for the whole selection
                        onMuteToggled: function(muted) {
                            trackListModel.setMutedForSelectedRows(muted)
                        }

                        onSoloToggled: function(solo) {
                            trackListModel.setSoloForSelectedRows(solo)
                        }
                    }
                }
            }
        }
    }
}
