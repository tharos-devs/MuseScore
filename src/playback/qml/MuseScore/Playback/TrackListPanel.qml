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

    //! NOTE: zoomed like the Mixer (see MixerPanel.qml's zoomContainer): the list is scaled as one block,
    //! sized so that once scaled it exactly fills this container
    Item {
        id: zoomContainer

        anchors.fill: parent
        clip: true

        StyledListView {
            id: listView

            width: zoomContainer.width / scale
            height: zoomContainer.height / scale

            scale: contextMenuModel.zoom > 0 ? contextMenuModel.zoom : 1
            transformOrigin: Item.TopLeft

            model: trackListModel

            //! NOTE: stays at the top; its global Mute/Solo buttons are in the same columns as the rows' ones
            headerPositioning: ListView.OverlayHeader
            header: Rectangle {
                width: ListView.view.width
                height: 32
                z: 2

                color: ui.theme.backgroundSecondaryColor

                Row {
                    anchors.right: parent.right
                    anchors.rightMargin: 4
                    anchors.verticalCenter: parent.verticalCenter

                    spacing: 4

                    GlobalMuteSoloButtons {
                        spacing: 4

                        model: trackListModel
                        toolTipsEnabled: false

                        navigationPanel: navPanel
                        navigationRow: 0
                        navigationColumnStart: 0
                    }

                    //! NOTE: the rows' visibility, sound, editor and articulation map columns
                    Repeater {
                        model: 4

                        Item {
                            width: 20
                            height: 20
                        }
                    }
                }

                SeparatorLine {
                    anchors.bottom: parent.bottom
                }
            }

            delegate: Item {
                id: rowItem

                required property int index
                required property MixerChannelItem channelItem
                required property bool partVisible
                required property bool hasArticulationMap

                readonly property InputResourceItem soundItem: channelItem ? channelItem.inputResourceItem : null
                readonly property color trackColor: channelItem && channelItem.hasCustomColor ? channelItem.color : ui.theme.accentColor

                width: ListView.view.width
                height: 28

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
                            { id: "resetColor", title: qsTrc("playback", "Reset color"), enabled: rowItem.channelItem.hasCustomColor }
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

                    anchors.left: colorStrip.right
                    anchors.leftMargin: 6
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
                    anchors.left: meter.right
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

                    anchors.right: parent.right
                    anchors.rightMargin: 4
                    anchors.verticalCenter: parent.verticalCenter

                    spacing: 4

                    ChannelMuteSoloButtons {
                        spacing: 4

                        channelItem: rowItem.channelItem

                        navigationPanel: navPanel
                        navigationName: "Row" + rowItem.index
                        muteNavigationRow: rowItem.index + 1
                        muteNavigationColumn: 0
                        soloNavigationRow: rowItem.index + 1
                        soloNavigationColumn: 1
                        accessibleName: rowItem.channelItem.title

                        onMuteToggled: function(muted) {
                            rowItem.channelItem.muted = muted
                        }

                        onSoloToggled: function(solo) {
                            rowItem.channelItem.solo = solo
                        }
                    }

                    FlatButton {
                        width: 20
                        height: 20

                        transparent: true
                        icon: rowItem.partVisible ? IconCode.EYE_OPEN : IconCode.EYE_CLOSED

                        navigation.panel: navPanel
                        navigation.name: "VisibilityButton" + rowItem.index
                        navigation.row: rowItem.index + 1
                        navigation.column: 2
                        navigation.accessible.name: rowItem.channelItem.title + " " + (rowItem.partVisible ? qsTrc("playback", "Hide instrument") : qsTrc("playback", "Show instrument"))

                        onClicked: {
                            trackListModel.togglePartVisible(rowItem.index)
                        }
                    }

                    //! NOTE: the Mixer's Sound menu
                    FlatButton {
                        id: soundButton

                        width: 20
                        height: 20

                        transparent: !soundMenu.isMenuOpened
                        icon: IconCode.AUDIO

                        navigation.panel: navPanel
                        navigation.name: "SoundButton" + rowItem.index
                        navigation.row: rowItem.index + 1
                        navigation.column: 3
                        navigation.accessible.name: rowItem.channelItem.title + " " + qsTrc("playback", "Sound")

                        onClicked: {
                            soundMenu.requestOpen()
                        }

                        AudioResourceMenuLoader {
                            id: soundMenu

                            resourceItemModel: rowItem.soundItem
                        }
                    }

                    //! NOTE: fixed slots, so the buttons stay in columns whatever the track has
                    Item {
                        width: 20
                        height: 20

                        FlatButton {
                            width: 20
                            height: 20

                            visible: rowItem.hasArticulationMap

                            transparent: true
                            icon: IconCode.ARTICULATION

                            navigation.panel: navPanel
                            navigation.name: "ArticulationMapButton" + rowItem.index
                            navigation.row: rowItem.index + 1
                            navigation.column: 4
                            navigation.accessible.name: rowItem.channelItem.title + " " + qsTrc("playback", "Edit articulation map")

                            onClicked: {
                                rowItem.channelItem.handleArticulationMapMenuItem("editArticulationMap")
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

                        FlatButton {
                            width: 20
                            height: 20

                            visible: rowItem.soundItem ? rowItem.soundItem.hasNativeEditorSupport : false

                            transparent: true
                            icon: IconCode.PLUGIN

                            navigation.panel: navPanel
                            navigation.name: "EditorButton" + rowItem.index
                            navigation.row: rowItem.index + 1
                            navigation.column: 5
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
    }
}
