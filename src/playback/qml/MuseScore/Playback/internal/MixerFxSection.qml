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

MixerPanelSection {
    id: root

    property bool resourcePickingActive: false

    //: FX is an abbreviation of "effects".
    headerTitle: qsTrc("playback", "Audio FX")
    headerHeight: 24

    Item {
        id: content

        required property MixerChannelItem channelItem

        // NOTE: slots are positioned manually (via each delegate's own `y`
        // binding below) rather than via Column, so that a dragged slot's
        // position can be driven separately (`dragOffsetY`) without a plain
        // Column fighting over who owns `y` each frame -- see the Repeater
        // delegate's own comment for the reasoning.
        readonly property int itemHeight: 24
        readonly property int itemSpacing: 4
        readonly property int rowHeight: itemHeight + itemSpacing

        y: 0

        height: childrenRect.height
        width: root.channelItemWidth

        property string accessibleName: (Boolean(root.needReadChannelName) ? channelItem.title + " " : "") + root.headerTitle

        // NOTE: drag state lives here (not on the dragged delegate itself)
        // because the dragged delegate's Repeater instance is destroyed and
        // recreated once the reorder is actually committed to C++ (see
        // onReleased below) -- this QtObject is a stable sibling that
        // survives that rebuild.
        QtObject {
            id: prv

            property int draggedIndex: -1
            property int dropIndex: -1
        }

        Repeater {
            id: repeater

            model: content.channelItem.outputResourceItemList

            // NOTE: the slot count can change out from under an in-progress drag
            // for reasons that have nothing to do with it (e.g. adding/removing
            // an effect elsewhere in the score re-syncs the shared blank-slot
            // count across every channel, see MixerPanelModel::updateOutputResourceItemCount) --
            // when that happens mid-drag, the dragged delegate is destroyed
            // without onReorderReleased ever firing, which would otherwise leave
            // draggedIndex/dropIndex stuck and displayIndex permanently
            // misapplying a stale shift to every remaining slot.
            onCountChanged: {
                prv.draggedIndex = -1
                prv.dropIndex = -1
            }

            delegate: Item {
                id: slotRoot

                required property OutputResourceItem modelData
                required property int index

                // NOTE: exposed so a reorder can restore navigation focus onto
                // whichever slot it lands on after the Repeater rebuild --
                // see onReorderReleased below.
                property alias resourceControl: resourceControl

                readonly property bool dragging: prv.draggedIndex === index

                // NOTE: where this slot should currently be drawn, in terms
                // of row position -- normally just its own index, but while
                // another slot is being dragged past it, it shifts by one row
                // to visually make room, exactly mirroring what
                // MixerChannelItem::moveOutputResourceItem() will do to the
                // real model once the drag actually ends (see the C++ side).
                readonly property int displayIndex: {
                    if (prv.draggedIndex < 0 || index === prv.draggedIndex) {
                        return index
                    }

                    if (prv.draggedIndex < prv.dropIndex) {
                        return (index > prv.draggedIndex && index <= prv.dropIndex) ? index - 1 : index
                    } else {
                        return (index >= prv.dropIndex && index < prv.draggedIndex) ? index + 1 : index
                    }
                }

                property real dragOffsetY: 0
                property real pressOffsetInItem: 0

                width: content.width
                height: content.itemHeight
                z: dragging ? 10 : 0

                // NOTE: deliberately NOT using MouseArea.drag.target here --
                // that writes to `y` imperatively, which permanently breaks a
                // declarative binding on it the first time a drag happens.
                // Driving a separate `dragOffsetY` property instead (which
                // has no binding of its own to break) keeps this ternary
                // reactive for the rest of this delegate's lifetime, e.g. if
                // a drag is released back onto its own starting slot (a
                // no-op move, so this exact delegate instance survives).
                y: dragging ? dragOffsetY : displayIndex * content.rowHeight

                Behavior on y {
                    enabled: !slotRoot.dragging
                    NumberAnimation { duration: 120; easing.type: Easing.OutCubic }
                }

                AudioResourceControl {
                    id: resourceControl

                    anchors.horizontalCenter: parent.horizontalCenter

                    resourceItemModel: slotRoot.modelData

                    //! NOTE Reordering is driven from a press-and-drag on this
                    //! control's own title button rather than a separate handle --
                    //! see AudioResourceControl.qml's `reorderable` for why (and
                    //! why a plain click there still opens the native editor).
                    reorderable: repeater.count > 1

                    navigationPanel: content.channelItem.panel
                    navigationRowStart: root.navigationRowStart + slotRoot.index * 3 // NOTE: 3 - because AudioResourceControl have 3 controls
                    navigationName: slotRoot.modelData.id
                    accessibleName: content.accessibleName

                    onTurnedOn: {
                        slotRoot.modelData.isActive = true
                    }

                    onTurnedOff: {
                        slotRoot.modelData.isActive = false
                    }

                    onTitleClicked: {
                        slotRoot.modelData.requestToLaunchNativeEditorView()
                    }

                    onNavigateControlIndexChanged: function(index) {
                        root.navigateControlIndexChanged(index)
                    }

                    onResourcePickingActiveChanged: {
                        root.resourcePickingActive = resourceControl.resourcePickingActive
                    }

                    onReorderPressed: function(globalX, globalY) {
                        var posInContent = content.mapFromItem(null, globalX, globalY)

                        slotRoot.pressOffsetInItem = posInContent.y - slotRoot.y
                        slotRoot.dragOffsetY = slotRoot.y
                        prv.draggedIndex = slotRoot.index
                        prv.dropIndex = slotRoot.index
                    }

                    onReorderPositionChanged: function(globalX, globalY) {
                        if (prv.draggedIndex !== slotRoot.index) {
                            return
                        }

                        var maxY = (repeater.count - 1) * content.rowHeight
                        var posInContent = content.mapFromItem(null, globalX, globalY)
                        var newY = Math.max(0, Math.min(posInContent.y - slotRoot.pressOffsetInItem, maxY))

                        slotRoot.dragOffsetY = newY

                        var newIndex = Math.max(0, Math.min(Math.round(newY / content.rowHeight), repeater.count - 1))
                        prv.dropIndex = newIndex
                    }

                    onReorderReleased: {
                        if (prv.draggedIndex < 0) {
                            return
                        }

                        if (prv.draggedIndex !== prv.dropIndex) {
                            var from = prv.draggedIndex
                            var to = prv.dropIndex

                            // NOTE: deferred so this event handler (on an item
                            // that's about to be destroyed by the reorder it
                            // triggers -- outputResourceItemList is a plain
                            // QList property, so any change to it makes the
                            // Repeater discard and recreate every delegate,
                            // this one included) finishes running first.
                            Qt.callLater(function() {
                                // NOTE: cleared BEFORE moveOutputResourceItem, not
                                // after -- the Repeater rebuild it triggers is
                                // synchronous, so displayIndex for the freshly
                                // created delegates must already see draggedIndex/
                                // dropIndex as cleared. Otherwise whichever new
                                // delegate happens to land at the old draggedIndex
                                // briefly computes a phantom shift, then visibly
                                // slides to its real position once these two lines
                                // ran (a spurious extra animation on the wrong slot).
                                prv.draggedIndex = -1
                                prv.dropIndex = -1
                                content.channelItem.moveOutputResourceItem(from, to)

                                // NOTE: deferred a second time -- the Repeater
                                // rebuild triggered by the call above needs to
                                // have actually happened before there's a
                                // delegate at `to` to focus.
                                Qt.callLater(function() {
                                    var droppedDelegate = repeater.itemAt(to)
                                    if (droppedDelegate) {
                                        droppedDelegate.resourceControl.requestTitleFocus()
                                    }
                                })
                            })
                        } else {
                            prv.draggedIndex = -1
                            prv.dropIndex = -1
                        }
                    }
                }
            }
        }
    }
}
