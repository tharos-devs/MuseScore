/*
 * SPDX-License-Identifier: GPL-3.0-only
 * MuseScore-Studio-CLA-applies
 *
 * MuseScore Studio
 * Music Composition & Notation
 *
 * Copyright (C) 2021 MuseScore Limited and others
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

MixerPanelSection {
    id: root

    headerTitle: qsTrc("playback", "Name")

    Rectangle {
        id: content

        required property MixerChannelItem channelItem

        //! NOTE: only Aux channels are renamable for now
        readonly property bool isAux: channelItem.type === MixerChannelItem.Aux

        property bool editingName: false

        width: root.channelItemWidth
        height: 22

        readonly property bool isInstrument: channelItem.type === MixerChannelItem.PrimaryInstrument
                                              || channelItem.type === MixerChannelItem.SecondaryInstrument

        function resolveLabelColor() {
            if (content.isInstrument && channelItem.hasCustomColor) {
                return channelItem.color
            }

            switch(channelItem.type) {
            case MixerChannelItem.PrimaryInstrument:
            case MixerChannelItem.SecondaryInstrument:
            case MixerChannelItem.Video:
                return ui.theme.accentColor
            case MixerChannelItem.Aux:
                return "#63D47B"
            case MixerChannelItem.Master:
                return "#F87BDC"
            }

            return ui.theme.accentColor
        }

        function resolveLabelColorOpacity() {
            if (channelItem.type === MixerChannelItem.SecondaryInstrument) {
                return 0.25
            }

            return 0.5
        }

        //! NOTE: per-channel-type context menu items
        function buildContextMenuItems() {
            if (content.isInstrument) {
                return [
                    { id: "editColor", title: qsTrc("playback", "Edit color…") },
                    //! NOTE Not scoped to content.channelItem.hasCustomColor: this may apply to a whole
                    //! multi-selection where other selected channels have a custom color even if the
                    //! right-clicked one doesn't (right-clicking an already-selected channel keeps the
                    //! multi-selection, see onClicked above).
                    { id: "resetColor", title: qsTrc("playback", "Reset color") }
                ]
            }

            if (content.isAux) {
                return [
                    { id: "renameAux", title: qsTrc("playback", "Rename channel") }
                ]
            }

            return []
        }

        function startEditingName() {
            if (!content.isAux || content.editingName) {
                return
            }

            ui.tooltip.hide(mouseArea)
            content.editingName = true
        }

        //! NOTE: newName undefined means the edit was cancelled (Escape) - end editing
        //! without renaming
        function commitEditingName(newName) {
            if (!content.editingName) {
                return
            }

            content.editingName = false

            if (newName !== undefined) {
                root.model.renameAuxChannel(content.channelItem, newName)
            }
        }

        readonly property color labelColor: resolveLabelColor()

        color: Utils.colorWithAlpha(labelColor, resolveLabelColorOpacity())
        border.color: channelItem.selected ? ui.theme.fontPrimaryColor : labelColor
        border.width: channelItem.selected ? 2 : 1

        Loader {
            id: nameLoader
            anchors.fill: parent

            sourceComponent: content.editingName ? editNameField : nameLabel

            Component {
                id: nameLabel

                StyledTextLabel {
                    id: textLabel
                    anchors.centerIn: parent

                    font: ui.theme.bodyBoldFont

                    readonly property int margin: -8
                    width: margin + content.width + margin

                    text: content.channelItem.title
                }
            }

            Component {
                id: editNameField

                TextInputField {
                    anchors.fill: parent
                    anchors.margins: 2

                    currentText: content.channelItem.title

                    property bool cancelled: false

                    Component.onCompleted: {
                        forceActiveFocus()
                        selectAll()
                    }

                    onEscaped: {
                        cancelled = true
                    }

                    onTextEditingFinished: function(newTextValue) {
                        Qt.callLater(content.commitEditingName, cancelled ? undefined : newTextValue)
                    }
                }
            }
        }

        MouseArea {
            id: mouseArea
            anchors.fill: parent

            enabled: parent.enabled && !content.editingName
            hoverEnabled: true
            acceptedButtons: Qt.LeftButton | Qt.RightButton

            onContainsMouseChanged: {
                if (mouseArea.containsMouse && nameLoader.item && nameLoader.item.truncated) {
                    ui.tooltip.show(mouseArea, content.channelItem.title)
                } else {
                    ui.tooltip.hide(mouseArea)
                }
            }

            onClicked: function(mouse) {
                if (content.isInstrument) {
                    // Qt.ControlModifier is Cmd on macOS and Ctrl on Windows/Linux.
                    const extendSelection = (mouse.modifiers & Qt.ControlModifier) !== 0
                    const rangeSelection = (mouse.modifiers & Qt.ShiftModifier) !== 0

                    if (mouse.button === Qt.RightButton) {
                        if (!content.channelItem.selected) {
                            root.model.selectChannel(content.channelItem, false, false)
                        }
                        contextMenuLoader.show(Qt.point(mouse.x, mouse.y))
                    } else if (mouse.button === Qt.LeftButton) {
                        root.model.selectChannel(content.channelItem, extendSelection, rangeSelection)
                    }
                    return
                }

                if (mouse.button === Qt.RightButton && content.isAux) {
                    contextMenuLoader.show(Qt.point(mouse.x, mouse.y))
                }
            }

            onDoubleClicked: function(mouse) {
                if (mouse.button === Qt.LeftButton) {
                    content.startEditingName()
                }
            }
        }

        ColorPickerModel {
            id: colorPickerModel

            onColorSelected: function(color) {
                root.model.setColorForSelectedChannels(color)
                root.model.clearSelection()
            }
        }

        ContextMenuLoader {
            id: contextMenuLoader

            items: content.buildContextMenuItems()

            onHandleMenuItem: function(itemId) {
                if (itemId === "editColor") {
                    colorPickerModel.selectColor(content.labelColor, false)
                } else if (itemId === "resetColor") {
                    root.model.resetColorForSelectedChannels()
                    root.model.clearSelection()
                } else if (itemId === "renameAux") {
                    content.startEditingName()
                }
            }
        }
    }
}
