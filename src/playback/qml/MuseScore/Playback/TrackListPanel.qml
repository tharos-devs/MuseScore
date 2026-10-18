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

    //! NOTE: the channel strip's controls, after the list's
    NavigationPanel {
        id: stripNavPanel

        name: "TrackListChannelStrip"
        section: root.navigationSection
        order: root.contentNavigationPanelOrderStart + 1
        direction: NavigationPanel.Vertical
        enabled: root.visible && stripLoader.active
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

            //! NOTE: room for the scroll bar on the right: it no longer covers the rows' meters and the header's buttons
            //! (a click there went to the scroll bar). Always kept, so the content doesn't move when the bar appears
            readonly property real scrollBarRoom: visualScrollBarInset

            model: trackListModel

            //! NOTE: a track selected in the score: its row comes into view, only when it's out of view and as little as
            //! possible (not under the header staying at the top either)
            Connections {
                target: trackListModel

                function onRowShowRequested(row) {
                    listView.positionViewAtIndex(row, ListView.Contain)

                    const item = listView.itemAtIndex(row)
                    const header = listView.headerItem
                    if (item && header) {
                        const hidden = header.height - item.mapToItem(listView, 0, 0).y
                        if (hidden > 0) {
                            listView.contentY -= hidden
                        }
                    }
                }
            }

            //! NOTE: the list keeps its scroll position when it's rebuilt (e.g. a folder added, a track moved into one)
            Connections {
                target: trackListModel

                property real savedContentY: 0

                function onModelAboutToBeReset() {
                    savedContentY = listView.contentY - listView.originY
                }

                function onModelReset() {
                    const contentY = savedContentY
                    Qt.callLater(function() {
                        const maxY = Math.max(0, listView.contentHeight - listView.height)
                        listView.contentY = listView.originY + Math.min(contentY, maxY)
                    })
                }
            }

            //! NOTE: a row dragged by its name: a track or bus into a folder (or out of its folder), a folder elsewhere
            //! among the rows outside folders (see TrackListModel::moveRowToFolder()/moveFolder())
            QtObject {
                id: dragPrv

                property int draggedRow: -1
                property bool draggingFolder: false
                //! NOTE: a folder's row the dragged track goes into; the row the dragged folder goes before; -1: none
                property int targetRow: -1
                //! NOTE: a dragged track out of its folder
                property bool outOfFolder: false

                readonly property bool active: draggedRow >= 0

                function update(sceneY) {
                    const y = listView.contentItem.mapFromItem(null, 0, sceneY).y
                    let row = listView.indexAt(10, y)

                    if (draggingFolder) {
                        if (row < 0) {
                            row = y > 0 ? listView.count : -1
                        } else {
                            const item = listView.itemAtIndex(row)
                            if (item && y > item.y + item.height / 2) {
                                row += 1
                            }
                        }
                        targetRow = trackListModel.canMoveFolderBefore(draggedRow, row) ? row : -1
                        return
                    }

                    const item = row >= 0 ? listView.itemAtIndex(row) : null
                    targetRow = item && item.isFolder ? row : -1
                    outOfFolder = targetRow < 0 && listView.itemAtIndex(draggedRow)?.inFolder === true
                                  && !(item && item.inFolder)
                }

                function drop() {
                    if (draggingFolder) {
                        if (targetRow >= 0) {
                            trackListModel.moveFolder(draggedRow, targetRow)
                        }
                    } else if (targetRow >= 0) {
                        trackListModel.moveRowToFolder(draggedRow, targetRow)
                    } else if (outOfFolder) {
                        trackListModel.moveRowToFolder(draggedRow, -1)
                    }
                    cancel()
                }

                function cancel() {
                    draggedRow = -1
                    targetRow = -1
                    outOfFolder = false
                }
            }

            //! NOTE: stays at the top; its global Mute/Solo buttons are in the same columns as the rows' ones
            headerPositioning: ListView.OverlayHeader
            header: Rectangle {
                width: ListView.view.width - ListView.view.scrollBarRoom
                height: 32 + metronomeRow.height + videoRow.height
                z: 2

                color: ui.theme.backgroundSecondaryColor

                //! NOTE: a new folder, above the selected track or first (see TrackListModel::addFolder())
                FlatButton {
                    anchors.left: parent.left
                    anchors.leftMargin: 4
                    anchors.verticalCenter: globalButtonsRow.verticalCenter

                    width: 24
                    height: 20

                    transparent: true
                    icon: IconCode.OPEN_FILE

                    navigation.panel: navPanel
                    navigation.name: "NewFolderButton"
                    navigation.row: 0
                    navigation.column: 0
                    navigation.accessible.name: qsTrc("playback", "New folder")

                    onClicked: {
                        trackListModel.addFolder()
                    }

                    StyledIconLabel {
                        anchors.right: parent.right
                        anchors.bottom: parent.bottom
                        iconCode: IconCode.PLUS
                        font.pixelSize: 9
                    }
                }

                //! NOTE: shows/hides the channel strip, like the "…" menu's Channel strip item
                FlatButton {
                    anchors.right: parent.right
                    anchors.rightMargin: 4
                    anchors.verticalCenter: globalButtonsRow.verticalCenter

                    width: 20
                    height: 20

                    transparent: !contextMenuModel.channelStripVisible
                    accentButton: contextMenuModel.channelStripVisible

                    navigation.panel: navPanel
                    navigation.name: "ChannelStripButton"
                    navigation.row: 0
                    navigation.column: 10
                    navigation.accessible.name: qsTrc("playback", "Channel strip")

                    onClicked: {
                        contextMenuModel.channelStripVisible = !contextMenuModel.channelStripVisible
                    }

                    StyledIconLabel {
                        anchors.centerIn: parent
                        iconCode: IconCode.SECTION_BREAK2
                        rotation: 90
                    }
                }

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
                        anchors.verticalCenter: parent.verticalCenter
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

                    //! NOTE: the rows' bus label room (see the rows' own)
                    Item {
                        width: 20
                        height: 20
                        visible: !trackListModel.hasArticulationMapColumn && !trackListModel.hasEditorColumn
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

                    ChannelColorStrip {
                        id: metronomeColorStrip

                        anchors.left: parent.left
                        anchors.leftMargin: 4
                        anchors.verticalCenter: parent.verticalCenter

                        trackColor: metronomeRow.channelItem && metronomeRow.channelItem.hasCustomColor ? metronomeRow.channelItem.color
                                                                                                         : ui.theme.accentColor
                        resetEnabled: metronomeRow.channelItem ? metronomeRow.channelItem.hasCustomColor : false

                        onColorSelected: function(color) {
                            trackListModel.setMetronomeColor(color)
                        }

                        onResetRequested: {
                            trackListModel.resetMetronomeColor()
                        }
                    }

                    ChannelMeters {
                        id: metronomeMeter

                        anchors.right: parent.right
                        anchors.rightMargin: 4
                        anchors.verticalCenter: parent.verticalCenter

                        meterLength: 22
                        meterThickness: 3
                        overloadLength: 2
                        gap: 1

                        leftChannelPressure: metronomeRow.channelItem ? metronomeRow.channelItem.leftChannelPressure : -60
                        rightChannelPressure: metronomeRow.channelItem ? metronomeRow.channelItem.rightChannelPressure : -60
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

                        //! NOTE: the rows' bus label room (see the rows' own)
                        Item {
                            width: 20
                            height: 20
                            visible: !trackListModel.hasArticulationMapColumn && !trackListModel.hasEditorColumn
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

                    ChannelColorStrip {
                        id: videoColorStrip

                        anchors.left: parent.left
                        anchors.leftMargin: 4
                        anchors.verticalCenter: parent.verticalCenter

                        trackColor: videoRow.trackColor
                        resetEnabled: videoRow.channelItem ? videoRow.channelItem.hasCustomColor : false

                        onColorSelected: function(color) {
                            trackListModel.setVideoColor(color)
                        }

                        onResetRequested: {
                            trackListModel.resetVideoColor()
                        }
                    }

                    ChannelMeters {
                        id: videoMeter

                        anchors.right: parent.right
                        anchors.rightMargin: 4
                        anchors.verticalCenter: parent.verticalCenter

                        meterLength: 22
                        meterThickness: 3
                        overloadLength: 2
                        gap: 1

                        leftChannelPressure: videoRow.channelItem ? videoRow.channelItem.leftChannelPressure : -60
                        rightChannelPressure: videoRow.channelItem ? videoRow.channelItem.rightChannelPressure : -60
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

                        //! NOTE: the rows' bus label room (see the rows' own)
                        Item {
                            width: 20
                            height: 20
                            visible: !trackListModel.hasArticulationMapColumn && !trackListModel.hasEditorColumn
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
                required property bool isFolder
                required property string folderName
                required property bool folderCollapsed
                required property bool folderMuted
                required property bool folderSoloed
                required property bool inFolder

                readonly property InputResourceItem soundItem: channelItem ? channelItem.inputResourceItem : null
                //! NOTE: an Aux FX/Aux Group bus's row: color, Mute and Solo only (Solo disabled for an FX bus)
                readonly property bool isAuxBus: channelItem ? channelItem.type === MixerChannelItem.Aux : false
                readonly property color trackColor: channelItem && channelItem.hasCustomColor ? channelItem.color : ui.theme.accentColor

                width: ListView.view.width - ListView.view.scrollBarRoom
                height: 28

                //! NOTE: a dragged track's target folder, or where a dragged folder goes (a line above the row, or below
                //! the last one)
                Rectangle {
                    anchors.fill: parent
                    z: 2
                    visible: dragPrv.active && !dragPrv.draggingFolder && dragPrv.targetRow === rowItem.index
                    color: "transparent"
                    border.color: ui.theme.accentColor
                    border.width: 2
                }

                Rectangle {
                    z: 2
                    visible: dragPrv.active && dragPrv.draggingFolder
                             && (dragPrv.targetRow === rowItem.index
                                 || (dragPrv.targetRow === listView.count && rowItem.index === listView.count - 1))
                    y: dragPrv.targetRow === rowItem.index ? 0 : parent.height - height
                    width: parent.width
                    height: 2
                    color: ui.theme.accentColor
                }

                //! NOTE: drags the row (a press on its name, see dragPrv)
                function dragPositionChanged(mouseArea, mouse, pressY) {
                    if (!dragPrv.active) {
                        if (Math.abs(mouse.y - pressY) < 6) {
                            return
                        }
                        dragPrv.draggedRow = rowItem.index
                        dragPrv.draggingFolder = rowItem.isFolder
                    }
                    dragPrv.update(mouseArea.mapToItem(null, mouse.x, mouse.y).y)
                }

                Loader {
                    anchors.fill: parent
                    active: rowItem.isFolder
                    sourceComponent: folderRowComponent
                }

                Loader {
                    anchors.fill: parent
                    active: !rowItem.isFolder
                    sourceComponent: channelRowComponent
                }

                //! NOTE: a folder's row: collapse, its name (double click: rename); dragged elsewhere
                Component {
                    id: folderRowComponent

                    Rectangle {
                        color: ui.theme.backgroundSecondaryColor

                        MouseArea {
                            id: folderMouseArea

                            property real pressY: 0

                            anchors.fill: parent
                            acceptedButtons: Qt.LeftButton | Qt.RightButton
                            preventStealing: true

                            onPressed: function(mouse) {
                                pressY = mouse.y
                            }

                            onPositionChanged: function(mouse) {
                                if (pressed && (mouse.buttons & Qt.LeftButton)) {
                                    rowItem.dragPositionChanged(folderMouseArea, mouse, pressY)
                                }
                            }

                            onReleased: {
                                if (dragPrv.active) {
                                    dragPrv.drop()
                                }
                            }

                            onCanceled: dragPrv.cancel()

                            onClicked: function(mouse) {
                                if (mouse.button === Qt.RightButton) {
                                    folderMenu.show(Qt.point(mouse.x, mouse.y))
                                }
                            }

                            onDoubleClicked: function(mouse) {
                                if (mouse.button === Qt.LeftButton) {
                                    nameEditor.active = true
                                }
                            }

                            ContextMenuLoader {
                                id: folderMenu

                                items: [
                                    { id: "rename", title: qsTrc("playback", "Rename") },
                                    { id: "remove", title: qsTrc("playback", "Remove folder") }
                                ]

                                onHandleMenuItem: function(itemId) {
                                    if (itemId === "rename") {
                                        nameEditor.active = true
                                    } else if (itemId === "remove") {
                                        trackListModel.removeFolder(rowItem.index)
                                    }
                                }
                            }
                        }

                        StyledIconLabel {
                            id: collapseArrow

                            anchors.left: parent.left
                            anchors.leftMargin: 2
                            anchors.verticalCenter: parent.verticalCenter
                            width: 16

                            iconCode: rowItem.folderCollapsed ? IconCode.SMALL_ARROW_RIGHT : IconCode.SMALL_ARROW_DOWN

                            MouseArea {
                                anchors.fill: parent
                                anchors.margins: -4
                                onClicked: trackListModel.toggleFolderCollapsed(rowItem.index)
                            }
                        }

                        //! NOTE: in the rows' Mute/Solo columns (left of their meters: 2 × 3 px + 1, then 6)
                        Row {
                            id: folderButtonsRow

                            anchors.right: parent.right
                            anchors.rightMargin: 4 + 7 + 6
                            anchors.verticalCenter: parent.verticalCenter

                            spacing: 4

                            //! NOTE: the rows' sound column
                            Item {
                                width: 20
                                height: 20
                            }

                            FlatToggleButton {
                                width: 20
                                height: 20

                                icon: IconCode.MUTE
                                checked: rowItem.folderMuted

                                navigation.panel: navPanel
                                navigation.name: "FolderMuteButton" + rowItem.index
                                navigation.row: rowItem.index + 3
                                navigation.column: 1
                                navigation.accessible.name: rowItem.folderName + " " + qsTrc("playback", "Mute")

                                onToggled: trackListModel.toggleFolderMute(rowItem.index)
                            }

                            FlatToggleButton {
                                width: 20
                                height: 20

                                icon: IconCode.SOLO
                                checked: rowItem.folderSoloed

                                navigation.panel: navPanel
                                navigation.name: "FolderSoloButton" + rowItem.index
                                navigation.row: rowItem.index + 3
                                navigation.column: 2
                                navigation.accessible.name: rowItem.folderName + " " + qsTrc("playback", "Solo")

                                onToggled: trackListModel.toggleFolderSolo(rowItem.index)
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

                            //! NOTE: the rows' bus label room (see the rows' own)
                            Item {
                                width: 20
                                height: 20
                                visible: !trackListModel.hasArticulationMapColumn && !trackListModel.hasEditorColumn
                            }
                        }

                        StyledTextLabel {
                            visible: !nameEditor.active
                            anchors.left: collapseArrow.right
                            anchors.leftMargin: 6
                            anchors.right: folderButtonsRow.left
                            anchors.rightMargin: 6
                            anchors.verticalCenter: parent.verticalCenter

                            horizontalAlignment: Text.AlignLeft
                            font: ui.theme.bodyBoldFont
                            text: rowItem.folderName
                        }

                        Loader {
                            id: nameEditor

                            active: false
                            anchors.left: collapseArrow.right
                            anchors.leftMargin: 6
                            anchors.right: parent.right
                            anchors.rightMargin: 6
                            anchors.verticalCenter: parent.verticalCenter

                            sourceComponent: TextInputField {
                                currentText: rowItem.folderName
                                Component.onCompleted: {
                                    ensureActiveFocus()
                                    selectAll()
                                }
                                onTextEditingFinished: function(newTextValue) {
                                    trackListModel.renameFolder(rowItem.index, newTextValue)
                                    nameEditor.active = false
                                }
                                onEscaped: nameEditor.active = false
                            }
                        }

                        SeparatorLine {
                            anchors.bottom: parent.bottom
                        }
                    }
                }

                Component {
                    id: channelRowComponent

                    Item {

                        //! NOTE: selection, like the Mixer's (click, Cmd/Ctrl+click, Shift+click): the color, Mute and Solo of
                        //! a selected row apply to the whole selection
                        Rectangle {
                            anchors.fill: parent
                            color: ui.theme.accentColor
                            opacity: 0.3
                            visible: rowItem.channelItem ? rowItem.channelItem.selected : false
                        }

                        MouseArea {
                            id: channelMouseArea

                            property real pressY: 0
                            //! NOTE: the release of a drag isn't a click (no selection change)
                            property bool dragged: false

                            anchors.fill: parent
                            acceptedButtons: Qt.LeftButton | Qt.RightButton
                            preventStealing: true

                            onPressed: function(mouse) {
                                pressY = mouse.y
                                dragged = false
                            }

                            onPositionChanged: function(mouse) {
                                if (pressed && (mouse.buttons & Qt.LeftButton)) {
                                    rowItem.dragPositionChanged(channelMouseArea, mouse, pressY)
                                }
                            }

                            onReleased: {
                                if (dragPrv.active) {
                                    dragged = true
                                    dragPrv.drop()
                                }
                            }

                            onCanceled: dragPrv.cancel()

                            onClicked: function(mouse) {
                                if (dragged) {
                                    return
                                }

                                //! NOTE: right click: "Move to folder", the Mixer's "Add FX/Group channel" ones, then
                                //! the track presets menu, like in the Mixer
                                if (mouse.button === Qt.RightButton) {
                                    let items = trackListModel.folderMenuItems(rowItem.index)
                                    const groups = [
                                        trackListModel.auxBusMenuItems(rowItem.index),
                                        rowItem.isAuxBus ? [] : rowItem.channelItem.trackPresetMenuItems()
                                    ]
                                    for (const group of groups) {
                                        if (group.length > 0) {
                                            items.push({ id: "", title: "" }) // separator
                                            items = items.concat(group)
                                        }
                                    }
                                    rowMenu.show(Qt.point(mouse.x, mouse.y), items)
                                    return
                                }

                                // Qt.ControlModifier is Cmd on macOS and Ctrl on Windows/Linux
                                trackListModel.selectRow(rowItem.index, (mouse.modifiers & Qt.ControlModifier) !== 0,
                                                         (mouse.modifiers & Qt.ShiftModifier) !== 0)
                            }

                            ContextMenuLoader {
                                id: rowMenu

                                onHandleMenuItem: function(itemId) {
                                    if (!trackListModel.handleMoveToFolderMenuItem(rowItem.index, itemId)
                                            && !trackListModel.handleAuxBusMenuItem(rowItem.index, itemId)) {
                                        rowItem.channelItem.handleTrackPresetMenuItem(itemId)
                                    }
                                }
                            }
                        }

                        ChannelColorStrip {
                            id: colorStrip

                            anchors.left: parent.left
                            //! NOTE: a folder's tracks are indented
                            anchors.leftMargin: rowItem.inFolder ? 18 : 4
                            anchors.verticalCenter: parent.verticalCenter

                            trackColor: rowItem.trackColor
                            //! NOTE: on a selected row, for the whole selection, whose other rows may have a color
                            resetEnabled: rowItem.channelItem.hasCustomColor || rowItem.channelItem.selected

                            onColorSelected: function(color) {
                                trackListModel.setTrackColor(rowItem.index, color)
                            }

                            onResetRequested: {
                                trackListModel.resetTrackColor(rowItem.index)
                            }
                        }

                        //! NOTE: the track's level, the Mixer's meters in small
                        ChannelMeters {
                            id: meter

                            anchors.right: parent.right
                            anchors.rightMargin: 4
                            anchors.verticalCenter: parent.verticalCenter

                            meterLength: 22
                            meterThickness: 3
                            overloadLength: 2
                            gap: 1

                            leftChannelPressure: rowItem.channelItem.leftChannelPressure
                            rightChannelPressure: rowItem.channelItem.rightChannelPressure
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
                                id: rowMuteSolo

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

                                    //! NOTE: without a map (dimmed), opens the editor on a new one for the track; its editor
                                    //! open: closes it
                                    onClicked: {
                                        if (rowItem.articulationMapEditorOpened) {
                                            rowItem.channelItem.handleArticulationMapMenuItem("closeArticulationMapEditor")
                                            return
                                        }
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

                                    //! NOTE: its window open (from here or elsewhere): closes it
                                    onClicked: {
                                        if (rowItem.instrumentEditorOpened) {
                                            rowItem.channelItem.closeInstrumentEditor()
                                            return
                                        }
                                        rowItem.soundItem.requestToLaunchNativeEditorView()
                                    }
                                }
                            }

                            //! NOTE: without the articulation map and plugin columns, room for a bus's "FX"/"GRP" label
                            Item {
                                width: 20
                                height: 20
                                visible: !trackListModel.hasArticulationMapColumn && !trackListModel.hasEditorColumn
                            }
                        }

                        //! NOTE: a bus's kind, like in the Mixer: centered between its Solo button and the meter
                        StyledTextLabel {
                            readonly property real start: buttonsRow.x + rowMuteSolo.x + rowMuteSolo.width

                            visible: rowItem.isAuxBus

                            x: start + (meter.x - start - width) / 2
                            anchors.verticalCenter: parent.verticalCenter

                            text: rowItem.channelItem && rowItem.channelItem.isGroupBus ? qsTrc("playback", "GRP") : qsTrc("playback", "FX")

                            font.pixelSize: 10
                            font.capitalization: Font.AllUppercase
                            color: ui.theme.fontSecondaryColor
                            opacity: 0.5
                        }

                        SeparatorLine {
                            anchors.bottom: parent.bottom
                        }
                    }
                }
            }
        }

        //! NOTE: the first selected row's channel with the Mixer's controls, below the list (hidden from the "…" menu);
        //! its top edge is dragged to change its height (remembered), up to all of it
        Item {
            id: stripArea

            readonly property real stripZoom: 0.85
            //! NOTE: all of its controls shown: no higher. Without a channel (none selected): their last height, so
            //! the list above doesn't jump
            readonly property int maxHeight: stripLoader.item ? Math.ceil(stripLoader.item.naturalHeight * stripZoom) + 1
                                                              : trackListModel.stripContentHeight
            onMaxHeightChanged: {
                if (stripLoader.item) {
                    trackListModel.stripContentHeight = maxHeight
                }
            }
            readonly property int minHeight: Math.min(120, maxHeight)
            //! NOTE: while its edge is dragged; then remembered (see TrackListModel::stripHeight)
            property int draggedHeight: -1

            anchors.left: parent.left
            anchors.right: parent.right
            anchors.bottom: parent.bottom

            visible: contextMenuModel.channelStripVisible
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
                        trackListModel.stripHeight = stripArea.height >= stripArea.maxHeight ? trackListModel.fullStripHeight
                                                                                                     : stripArea.height
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
                        channelSelected: trackListModel.stripChannelSelected
                        navigationPanel: stripNavPanel

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
