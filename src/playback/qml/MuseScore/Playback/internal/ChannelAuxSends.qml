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

//! NOTE: a channel's aux send slots (Mixer, Track list): a bus and a send level each, reordered by dragging a slot's
//! bus button (only how they're shown: the sends are parallel). Compact: narrower slots, not draggable
Item {
    id: root

    required property MixerChannelItem channelItem
    property bool compact: false

    property NavigationPanel navigationPanel: channelItem.panel
    property int navigationRowStart: 0
    property string accessibleName: ""

    signal navigateControlIndexChanged(var index)

    readonly property int itemHeight: 24
    readonly property int itemSpacing: 4
    readonly property int rowHeight: itemHeight + itemSpacing

    width: 108
    height: Math.max(0, repeater.count * root.rowHeight - root.itemSpacing)

    SlotReorderState {
        id: reorder

        //! NOTE: the empty slots stay last (see MixerChannelItem::moveAuxSendItem()): the dragged one goes among
        //! the sends only
        property int lastSendIndex: 0
    }

    Repeater {
        id: repeater

        model: root.channelItem.auxSendItemModel

        //! NOTE: slots added or removed during a drag (e.g. another panel's change): the drag is dropped
        onCountChanged: reorder.clear()

        delegate: Item {
            id: sendRow

            required property AuxSendItem modelData
            required property int index

            readonly property bool dragging: reorder.draggedIndex === index
            property real dragOffsetY: 0
            property real pressOffsetInItem: 0

            width: root.width
            height: root.itemHeight
            z: dragging ? 10 : 0

            //! NOTE: not MouseArea.drag.target, which would break this binding (see ChannelFxSlots.qml)
            y: dragging ? dragOffsetY : reorder.displayIndex(index) * root.rowHeight

            Behavior on y {
                enabled: !sendRow.dragging
                NumberAnimation { duration: 120; easing.type: Easing.OutCubic }
            }

            AuxSendControl {
                id: auxSendControl

                anchors.horizontalCenter: parent.horizontalCenter
                //! NOTE: stays at the component's own 96 default until the row narrows past 104. Compact narrows this
                //! further still, to knob(27) + spacing(8) + an 18px arrow button
                width: root.compact ? 53 : Math.min(96, sendRow.width - 8)

                compact: root.compact

                auxSendItemModel: sendRow.modelData

                accentColor: root.channelItem.hasCustomColor ? root.channelItem.color : ui.theme.accentColor

                reorderable: repeater.count > 1

                navigationPanel: root.navigationPanel
                navigationRowStart: root.navigationRowStart + sendRow.index * 5 // NOTE: 5 - AuxSendControl spans rows +0..+4
                navigationName: sendRow.modelData.id
                accessibleName: root.accessibleName

                onNavigateControlIndexChanged: function(index) {
                    root.navigateControlIndexChanged(index)
                }

                onReorderPressed: function(globalX, globalY) {
                    let lastSendIndex = 0
                    for (let i = 0; i < repeater.count; ++i) {
                        const row = repeater.itemAt(i)
                        if (row && !row.modelData.isBlank) {
                            lastSendIndex = i
                        }
                    }

                    const posInRoot = root.mapFromItem(null, globalX, globalY)
                    sendRow.pressOffsetInItem = posInRoot.y - sendRow.y
                    sendRow.dragOffsetY = sendRow.y
                    reorder.lastSendIndex = lastSendIndex
                    reorder.draggedIndex = sendRow.index
                    reorder.dropIndex = sendRow.index
                }

                onReorderPositionChanged: function(globalX, globalY, modifiers) {
                    if (reorder.draggedIndex !== sendRow.index) {
                        return
                    }

                    const maxY = reorder.lastSendIndex * root.rowHeight
                    const posInRoot = root.mapFromItem(null, globalX, globalY)
                    const newY = Math.max(0, Math.min(posInRoot.y - sendRow.pressOffsetInItem, maxY))

                    sendRow.dragOffsetY = newY
                    reorder.dropIndex = Math.max(0, Math.min(Math.round(newY / root.rowHeight), reorder.lastSendIndex))
                }

                onReorderCanceled: reorder.clear()

                onReorderReleased: {
                    if (reorder.draggedIndex < 0) {
                        return
                    }

                    const from = reorder.draggedIndex
                    const to = reorder.dropIndex

                    if (from === to) {
                        reorder.clear()
                        return
                    }

                    //! NOTE: deferred, and cleared first: the move rebuilds the delegates, this one included (see
                    //! ChannelFxSlots.qml)
                    Qt.callLater(function() {
                        reorder.clear()
                        root.channelItem.moveAuxSendItem(from, to)
                    })
                }
            }
        }
    }
}
