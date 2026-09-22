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

    //! NOTE: shared state for the aux drag-and-drop reorder gesture below - lives here
    //! (not on any one delegate) since every delegate of this row needs to read it for
    //! its own drop-indicator/dragged-opacity styling, not just the one that was
    //! actually pressed to start the drag.
    //!
    //! auxDropBeforeIndex: -2 while no drag is active; -1 means "drop at the end of
    //! this type's section"; >= 0 is the aux bus index the drag is currently hovering
    //! just before.
    property int auxDropBeforeIndex: -2
    property int auxLastOfTypeIndex: -1
    property var auxDraggedIndices: []

    Rectangle {
        id: content

        required property MixerChannelItem channelItem

        //! NOTE: only Aux channels are renamable/add-FX-or-Group/deletable for now - see
        //! buildContextMenuItems() below for where these per-type item lists live
        readonly property bool isAux: channelItem.type === MixerChannelItem.Aux

        property bool editingName: false

        width: root.rowWidthFor(channelItem)
        height: 22

        readonly property bool isInstrument: channelItem.type === MixerChannelItem.PrimaryInstrument
                                              || channelItem.type === MixerChannelItem.SecondaryInstrument

        //! NOTE: channels whose title color/selection can be customized
        readonly property bool isColorable: content.isInstrument || channelItem.type === MixerChannelItem.Aux

        function resolveLabelColor() {
            if (content.isColorable && channelItem.hasCustomColor) {
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
            let items = []

            if (content.isColorable) {
                items.push({ id: "editColor", title: qsTrc("playback", "Edit color…") })
                //! NOTE Not scoped to content.channelItem.hasCustomColor: this may apply to a whole
                //! multi-selection where other selected channels have a custom color even if the
                //! right-clicked one doesn't (right-clicking an already-selected channel keeps the
                //! multi-selection, see onClicked above).
                items.push({ id: "resetColor", title: qsTrc("playback", "Reset color") })
            }

            if (content.isAux) {
                if (items.length > 0) {
                    items.push({})
                }
                items.push({ id: "renameAux", title: qsTrc("playback", "Rename channel") })
                items.push({})
                items.push({ id: "addFxChannel", title: qsTrc("playback", "Add FX channel"), enabled: root.model.canAddAuxBus() })
                items.push({ id: "addGroupChannel", title: qsTrc("playback", "Add Group channel"), enabled: root.model.canAddAuxBus() })
                items.push({})
                items.push({ id: "deleteChannel", title: qsTrc("playback", "Delete channel"), enabled: !content.channelItem.isReverbBus })
            }

            if (content.isInstrument) {
                if (items.length > 0) {
                    items.push({})
                }
                items.push({ id: "addFxChannel", title: qsTrc("playback", "Add FX channel"), enabled: root.model.canAddAuxBus() })
                items.push({ id: "addGroupChannel", title: qsTrc("playback", "Add Group channel"), enabled: root.model.canAddAuxBus() })
                items.push({})
                items.push({
                    id: "addFxChannelForSelectedTracks",
                    title: qsTrc("playback", "Add FX channel for selected tracks"),
                    enabled: root.model.canAddAuxBus()
                })
                items.push({
                    id: "addGroupChannelForSelectedTracks",
                    title: qsTrc("playback", "Add Group channel for selected tracks"),
                    enabled: root.model.canAddAuxBus()
                })
            }

            return items
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

        //! NOTE: this channel is currently part of an in-progress aux drag - see the
        //! MouseArea's onPositionChanged below for how root.auxDraggedIndices is filled.
        //! The drop-position indicator itself is drawn in MixerPanel.qml instead (see
        //! its own NOTE), spanning the full channel column rather than just this row.
        //! Requires isAux first: a non-Aux channel's auxBusIndex defaults to 0, which
        //! is also the Reverb bus's real index (REVERB_CHANNEL_IDX) - without this
        //! guard, dragging Reverb itself would match that default on every
        //! instrument/master/video/metronome channel too, dimming all of them.
        readonly property bool isDragged: content.isAux && root.auxDropBeforeIndex !== -2
                                           && root.auxDraggedIndices.indexOf(channelItem.auxBusIndex) !== -1

        color: Utils.colorWithAlpha(labelColor, resolveLabelColorOpacity())
        border.color: channelItem.selected ? ui.theme.fontPrimaryColor : labelColor
        border.width: channelItem.selected ? 2 : 1
        opacity: content.isDragged ? 0.5 : 1.0

        //! NOTE: safety net for a channel being destroyed while it's part of an active
        //! drag (e.g. a full model reload - see MixerPanelModel::reload() - firing
        //! mid-gesture) - onReleased/onCanceled below never get a chance to run in
        //! that case (the MouseArea they're on is destroyed right along with this
        //! delegate), which would otherwise leave the OTHER, surviving delegates
        //! permanently reading a stale root.auxDraggedIndices/auxDropBeforeIndex (the
        //! drop indicator stuck visible, this kind of channel stuck dimmed) until
        //! some unrelated later drag happens to overwrite it.
        Component.onDestruction: {
            if (content.isDragged) {
                root.auxDropBeforeIndex = -2
                root.auxDraggedIndices = []
            }
        }

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
            //! NOTE: the Reverb bus is a fixed, non-removable default (see
            //! MixerChannelItem::isReverbBus()'s doc comment - "deleteChannel" in
            //! buildContextMenuItems() above excludes it for that reason) but it's an
            //! ordinary FX bus otherwise, just as reorderable as any other.
            readonly property bool isDraggable: content.isAux

            //! NOTE: without this, the enclosing Flickable (MixerPanel.qml) steals the
            //! mouse grab as soon as it sees horizontal movement, which both turns the
            //! drag into a horizontal scroll instead of a reorder, AND means onReleased
            //! below never fires (onCanceled does instead) - leaving the drag state
            //! (and the drop indicator) stuck until some unrelated click happens to
            //! reset it. Only set while an aux drag could actually be starting, so a
            //! non-draggable channel's plain click still lets an accidental drag-through
            //! from there scroll the mixer as before.
            preventStealing: mouseArea.isDraggable

            property point pressScenePos
            property bool dragThresholdExceeded: false
            //! NOTE: every aux bus of the pressed channel's own type (FX or Group), in
            //! current display order - captured once, at drag start, so the live
            //! drop-target math below has a stable reference to measure movement
            //! against for the whole gesture, regardless of anything else that may
            //! change concurrently.
            property var auxOrderSnapshot: []
            property int pressedPositionInOrder: -1

            onContainsMouseChanged: {
                if (mouseArea.containsMouse && nameLoader.item && nameLoader.item.truncated) {
                    ui.tooltip.show(mouseArea, content.channelItem.title)
                } else {
                    ui.tooltip.hide(mouseArea)
                }
            }

            //! NOTE: onReleased below fires (and resets dragThresholdExceeded via
            //! resetAuxDragState()) BEFORE onClicked does for the same gesture - a
            //! guard in onClicked checking dragThresholdExceeded directly would always
            //! see it already false and never actually suppress anything. This
            //! separate one-shot flag is what onClicked actually checks instead: set
            //! from dragThresholdExceeded's value right before it gets reset, cleared
            //! again on the next press so it can't leak into a later, unrelated click.
            property bool suppressNextClick: false

            onPressed: function(mouse) {
                mouseArea.pressScenePos = mouseArea.mapToItem(null, mouse.x, mouse.y)
                mouseArea.dragThresholdExceeded = false
                mouseArea.suppressNextClick = false
            }

            onPositionChanged: function(mouse) {
                //! NOTE: hoverEnabled above (needed for the truncated-title tooltip)
                //! makes this handler fire on every mouse movement over the label, not
                //! just while a button is held - without this guard, plain hovering
                //! (mouse never actually pressed) would measure distance against
                //! pressScenePos' stale value from some earlier gesture (or its default
                //! Qt.point(0,0) if there hasn't been one yet this session) and
                //! immediately "exceed the drag threshold", showing the drop indicator
                //! and computing a meaningless target for a drag that was never started.
                if (!mouseArea.pressed || !mouseArea.isDraggable) {
                    return
                }

                const scenePos = mouseArea.mapToItem(null, mouse.x, mouse.y)

                if (!mouseArea.dragThresholdExceeded) {
                    const dx = scenePos.x - mouseArea.pressScenePos.x
                    const dy = scenePos.y - mouseArea.pressScenePos.y
                    //! NOTE: 4px, same threshold AudioResourceControl.qml's own
                    //! press-vs-drag disambiguation uses for the FX-slot reorder
                    if (Math.sqrt(dx * dx + dy * dy) < 4) {
                        return
                    }

                    mouseArea.dragThresholdExceeded = true

                    const isGroupBus = content.channelItem.isGroupBus
                    mouseArea.auxOrderSnapshot = root.model.auxBusIndicesOfType(isGroupBus)
                    mouseArea.pressedPositionInOrder = mouseArea.auxOrderSnapshot.indexOf(content.channelItem.auxBusIndex)

                    //! NOTE: dragging an already-selected channel drags the whole
                    //! current selection (of this same type) along with it; dragging
                    //! one that isn't part of the current selection drags just itself
                    const selected = root.model.selectedAuxBusIndices(isGroupBus)
                    root.auxDraggedIndices = selected.indexOf(content.channelItem.auxBusIndex) !== -1
                                              ? selected : [content.channelItem.auxBusIndex]
                    root.auxLastOfTypeIndex = mouseArea.auxOrderSnapshot.length > 0
                                              ? mouseArea.auxOrderSnapshot[mouseArea.auxOrderSnapshot.length - 1] : -1
                }

                if (mouseArea.pressedPositionInOrder < 0) {
                    return
                }

                //! NOTE: driven by movement relative to the press point rather than an
                //! absolute position mapped into the row's own layout, since this
                //! delegate has no direct reference to sectionContentList's coordinate
                //! space (it's created via MixerPanelSection.qml's rowWrapper, not a
                //! plain ListView delegate itself - see its own NOTE on excluding
                //! master from the live list).
                const deltaX = scenePos.x - mouseArea.pressScenePos.x
                const slotDelta = Math.round(deltaX / (root.channelItemWidth + 1))
                const rawTargetPos = mouseArea.pressedPositionInOrder + slotDelta
                const maxPos = mouseArea.auxOrderSnapshot.length - 1
                const clampedPos = Math.max(0, Math.min(maxPos, rawTargetPos))

                //! NOTE: dragging past the last slot in either direction still clamps
                //! clampedPos to maxPos (the confinement the user asked for - this can
                //! never resolve to a position outside the pressed channel's own aux
                //! type), but rawTargetPos itself is left unclamped so overshooting
                //! past the end is still distinguishable from stopping exactly on the
                //! last slot, which is what picks "drop at the end" (-1) over "drop
                //! before the last slot".
                root.auxDropBeforeIndex = rawTargetPos > maxPos ? -1 : mouseArea.auxOrderSnapshot[clampedPos]
            }

            //! NOTE: resets all shared drag state BEFORE calling reorderAuxChannels()
            //! below (rather than after), so the drop indicator is already guaranteed
            //! gone by the time that call's beginRemoveRows()/beginInsertRows() destroy
            //! and recreate delegates - not just eventually, once this function returns.
            function resetAuxDragState() {
                root.auxDropBeforeIndex = -2
                root.auxDraggedIndices = []
                mouseArea.dragThresholdExceeded = false
                mouseArea.auxOrderSnapshot = []
                mouseArea.pressedPositionInOrder = -1
            }

            onReleased: function() {
                const shouldCommit = mouseArea.dragThresholdExceeded && root.auxDraggedIndices.length > 0
                const draggedIndices = root.auxDraggedIndices
                const dropBeforeIndex = root.auxDropBeforeIndex

                mouseArea.suppressNextClick = mouseArea.dragThresholdExceeded
                mouseArea.resetAuxDragState()

                if (shouldCommit) {
                    root.model.reorderAuxChannels(draggedIndices, dropBeforeIndex)
                }
            }

            //! NOTE: fires instead of onReleased whenever this MouseArea loses the mouse
            //! grab mid-gesture (e.g. preventStealing above failing to prevent an
            //! ancestor from stealing it after all, a popup opening, or the window
            //! losing focus) - without this, such a cancellation would leave the drag
            //! state (and the drop indicator) stuck indefinitely, since onReleased would
            //! never run to reset it.
            onCanceled: mouseArea.resetAuxDragState()

            onClicked: function(mouse) {
                //! NOTE: suppresses the click-to-select this MouseArea would otherwise
                //! also fire once a press-drag-release completes as a reorder instead -
                //! see suppressNextClick's own declaration for why this can't just
                //! check dragThresholdExceeded directly here.
                if (mouseArea.suppressNextClick) {
                    mouseArea.suppressNextClick = false
                    return
                }

                //! NOTE: isColorable already covers Aux channels (see its definition above),
                //! so this single guard also gates the FX/Group/rename/delete context menu -
                //! no separate isAux-only branch is needed here
                if (!content.isColorable) {
                    return
                }

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
                } else if (itemId === "addFxChannel") {
                    root.model.addFxChannel()
                } else if (itemId === "addGroupChannel") {
                    root.model.addGroupChannel()
                } else if (itemId === "addFxChannelForSelectedTracks") {
                    root.model.addFxChannelForSelectedTracks()
                } else if (itemId === "addGroupChannelForSelectedTracks") {
                    root.model.addGroupChannelForSelectedTracks()
                } else if (itemId === "deleteChannel") {
                    root.model.deleteAuxChannel(content.channelItem)
                }
            }
        }
    }
}
