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

import Muse.Ui
import Muse.UiComponents
import MuseScore.NotationScene

//! NOTE: shown over the score, centered on the mouse (see AbstractNotationPaintView::toggleCircularToolbar()):
//! the panels in the middle, the automation curves on the upper ring, the note editors on the lower one.
//! A click on a button runs its command and closes the toolbar, a click elsewhere only closes it
Item {
    id: root

    signal closeRequested()

    readonly property real outerRadius: 80
    readonly property real ringInnerRadius: 40
    readonly property real innerRadius: 34
    readonly property real frameWidth: 4
    readonly property real gap: 3
    readonly property real iconSize: 14
    // for the icons drawn smaller than the others by the icon font
    readonly property real largeIconSize: 18

    // Angles in degrees, clockwise from 3 o'clock
    readonly property var outerSectors: [
        { id: "cc1", label: "CC01", from: 180, to: 216 },
        { id: "cc11", label: "CC11", from: 216, to: 252 },
        { id: "tempo", label: "", from: 252, to: 288, icon: IconCode.NOTE_QUARTER },
        { id: "last-midi-cc", label: "", titleLabel: true, from: 288, to: 324 },
        { id: "more-midi-cc", label: "…", from: 324, to: 360, large: true },
        { id: "note-offsets", label: "", from: 0, to: 48, icon: IconCode.MULTIMEASURE_REST },
        { id: "instrument-editor", label: "", from: 48, to: 72, icon: IconCode.PLUGIN },
        { id: "articulations", label: "", from: 72, to: 108, icon: IconCode.ARTICULATION },
        { id: "articulation-map-editor", label: "", from: 108, to: 132, icon: IconCode.EDIT },
        { id: "velocities", label: "", from: 132, to: 180, icon: IconCode.WAVEFORM_HALFWAVE, largeIcon: true }
    ]

    // dx: moves the icon horizontally (right if positive), away from the vertical middle line
    readonly property var innerSectors: [
        { id: "mixer", from: 180, to: 270, icon: IconCode.MIXER, dx: -2 },
        { id: "video", from: 270, to: 360, icon: IconCode.VIDEO, dx: 2 },
        { id: "secondary-window", from: 0, to: 90, icon: IconCode.ORIENTATION_LANDSCAPE, largeIcon: true, dx: 2 },
        { id: "timeline", from: 90, to: 180, icon: IconCode.SECTION_BREAK2, rotated: true, dx: -2 }
    ]

    // Neutral light grey buttons, whatever the theme (the dark theme's lightest grey: a color derived from the accent
    // color doesn't suit every accent color), the theme's accent color for the active ones
    readonly property color frameColor: ui.theme.backgroundPrimaryColor
    readonly property color buttonColor: "#BDBDBD"
    readonly property color activeButtonColor: ui.theme.accentColor
    readonly property color contentColor: "#000000"
    readonly property color activeContentColor: "#FFFFFF"

    property point center: Qt.point(0, 0)
    property string hoveredId: ""
    property string pressedId: ""

    visible: false

    function show(viewPos) {
        model.prepareOpening()

        root.hoveredId = ""
        root.pressedId = ""
        placeAt(viewPos)
        root.visible = true
    }

    //! NOTE: fully inside the score view
    function placeAt(viewPos) {
        var r = outerRadius + frameWidth
        root.center = Qt.point(Math.max(r, Math.min(viewPos.x, root.width - r)),
                               Math.max(r, Math.min(viewPos.y, root.height - r)))
        canvas.requestPaint()
    }

    function hide() {
        moreMenuLoader.close()
        root.visible = false
    }

    //! NOTE: reads model.items, so that the bindings using it follow the rebuilt items
    function itemData(id) {
        var items = model.items
        for (var i = 0; i < items.length; ++i) {
            if (items[i].id === id) {
                return items[i]
            }
        }
        return null
    }

    function isEnabled(id) {
        var item = itemData(id)
        return Boolean(item) && item.enabled
    }

    //! NOTE: the articulation map editor button without a map stays clickable (an empty editor)
    function isDimmed(id) {
        return !isEnabled(id) || (id === "articulation-map-editor" && !model.focusedStaffHasArticulationMap)
    }

    function isChecked(id) {
        var item = itemData(id)
        return Boolean(item) && item.checked
    }

    function sectorIdAt(sectors, angle) {
        for (var i = 0; i < sectors.length; ++i) {
            if (angle >= sectors[i].from && angle < sectors[i].to) {
                return sectors[i].id
            }
        }
        return ""
    }

    //! NOTE: the id of the button at this position, "" outside of them
    function hitTest(x, y) {
        var dx = x - center.x
        var dy = y - center.y
        var distance = Math.sqrt(dx * dx + dy * dy)

        var angle = Math.atan2(dy, dx) * 180 / Math.PI
        if (angle < 0) {
            angle += 360
        }

        if (distance <= innerRadius) {
            return sectorIdAt(innerSectors, angle)
        }

        if (distance < ringInnerRadius || distance > outerRadius) {
            return ""
        }

        return sectorIdAt(outerSectors, angle)
    }

    function contentColorOf(id) {
        return isChecked(id) ? activeContentColor : contentColor
    }

    function fillColor(id) {
        var color = isChecked(id) ? activeButtonColor : buttonColor
        if (isEnabled(id) && pressedId === id) {
            return Qt.darker(color, 1.15)
        }
        if (isEnabled(id) && hoveredId === id) {
            return isChecked(id) ? Qt.lighter(color, 1.15) : Qt.lighter(color, 1.08)
        }
        return color
    }

    function activate(id) {
        if (!isEnabled(id)) {
            return
        }

        if (id === "more-midi-cc") {
            var sector = outerSectors.find(function(s) { return s.id === id })
            var mid = (sector.from + sector.to) / 2 * Math.PI / 180
            var r = (ringInnerRadius + outerRadius) / 2
            moreMenuAnchor.x = center.x + r * Math.cos(mid)
            moreMenuAnchor.y = center.y + r * Math.sin(mid)
            // the menu shows below its parent
            moreMenuLoader.parent = moreMenuAnchor
            moreMenuLoader.toggleOpened(itemData(id).subitems)
            return
        }

        model.handleMenuItem(id)
        root.closeRequested()
    }

    onHoveredIdChanged: canvas.requestPaint()
    onPressedIdChanged: canvas.requestPaint()
    // Only moved: rebuilding the buttons would delete those the "…" menu may still show
    onWidthChanged: if (visible) { placeAt(center) }
    onHeightChanged: if (visible) { placeAt(center) }

    CircularToolbarModel {
        id: model
    }

    // Closes the toolbar on a click outside of its buttons, without the click reaching the score
    MouseArea {
        anchors.fill: parent

        acceptedButtons: Qt.AllButtons
        hoverEnabled: true

        onPositionChanged: function(mouse) {
            root.hoveredId = root.hitTest(mouse.x, mouse.y)
        }

        onExited: {
            root.hoveredId = ""
        }

        onPressed: function(mouse) {
            root.pressedId = mouse.button === Qt.LeftButton ? root.hitTest(mouse.x, mouse.y) : ""
            if (root.pressedId === "") {
                root.closeRequested()
            }
        }

        onReleased: function(mouse) {
            var id = root.pressedId
            root.pressedId = ""
            if (id !== "" && id === root.hitTest(mouse.x, mouse.y)) {
                root.activate(id)
            }
        }

        onWheel: function(wheel) {
            wheel.accepted = true
        }
    }

    Canvas {
        id: canvas

        x: root.center.x - width / 2
        y: root.center.y - height / 2
        width: 2 * (root.outerRadius + root.frameWidth)
        height: width

        onPaint: {
            var ctx = getContext("2d")
            ctx.reset()

            var c = width / 2

            // Frame
            ctx.fillStyle = root.frameColor
            ctx.strokeStyle = ui.theme.strokeColor
            ctx.lineWidth = 1
            ctx.beginPath()
            ctx.arc(c, c, root.outerRadius + root.frameWidth - 0.5, 0, 2 * Math.PI)
            ctx.fill()
            ctx.stroke()

            // Outer ring
            for (var i = 0; i < root.outerSectors.length; ++i) {
                var sector = root.outerSectors[i]
                var a0 = sector.from * Math.PI / 180
                var a1 = sector.to * Math.PI / 180

                ctx.fillStyle = root.fillColor(sector.id)
                ctx.beginPath()
                ctx.arc(c, c, root.outerRadius, a0, a1, false)
                ctx.arc(c, c, root.ringInnerRadius, a1, a0, true)
                ctx.closePath()
                ctx.fill()

                // Gaps of the same width all along, between the buttons
                ctx.strokeStyle = root.frameColor
                ctx.lineWidth = root.gap
                ctx.stroke()
            }

            // Middle
            for (var j = 0; j < root.innerSectors.length; ++j) {
                var innerSector = root.innerSectors[j]

                ctx.fillStyle = root.fillColor(innerSector.id)
                ctx.beginPath()
                ctx.moveTo(c, c)
                ctx.arc(c, c, root.innerRadius, innerSector.from * Math.PI / 180, innerSector.to * Math.PI / 180, false)
                ctx.closePath()
                ctx.fill()

                ctx.strokeStyle = root.frameColor
                ctx.lineWidth = root.gap
                ctx.stroke()
            }
        }
    }

    // Outer ring labels
    Repeater {
        model: root.outerSectors

        delegate: Column {
            id: sectorLabel

            required property var modelData

            readonly property real midAngle: (modelData.from + modelData.to) / 2 * Math.PI / 180
            readonly property real labelRadius: (root.ringInnerRadius + root.outerRadius) / 2

            x: root.center.x + labelRadius * Math.cos(midAngle) - width / 2
            y: root.center.y + labelRadius * Math.sin(midAngle) - height / 2

            spacing: 2
            opacity: root.isDimmed(modelData.id) ? ui.theme.itemOpacityDisabled : 1

            StyledIconLabel {
                anchors.horizontalCenter: parent.horizontalCenter
                visible: Boolean(sectorLabel.modelData.icon)
                iconCode: sectorLabel.modelData.icon ?? IconCode.NONE
                color: root.contentColorOf(sectorLabel.modelData.id)
                font.pixelSize: sectorLabel.modelData.largeIcon ? root.largeIconSize : root.iconSize
            }

            StyledTextLabel {
                anchors.horizontalCenter: parent.horizontalCenter
                // the button's own title, e.g. the controller of the last MIDI CC button
                readonly property var itemData: sectorLabel.modelData.titleLabel ? root.itemData(sectorLabel.modelData.id) : null

                visible: text !== ""
                text: sectorLabel.modelData.titleLabel ? (itemData ? itemData.title : "") : sectorLabel.modelData.label
                color: root.contentColorOf(sectorLabel.modelData.id)
                font: Qt.font({ family: ui.theme.bodyFont.family,
                                pixelSize: sectorLabel.modelData.large ? 18 : ui.theme.bodyFont.pixelSize,
                                bold: true })
            }
        }
    }

    // Middle icons
    Repeater {
        model: root.innerSectors

        delegate: StyledIconLabel {
            required property var modelData

            readonly property real midAngle: (modelData.from + modelData.to) / 2 * Math.PI / 180
            readonly property real iconRadius: root.innerRadius * 0.5

            x: root.center.x + iconRadius * Math.cos(midAngle) - width / 2 + (modelData.dx ?? 0)
            y: root.center.y + iconRadius * Math.sin(midAngle) - height / 2

            opacity: root.isDimmed(modelData.id) ? ui.theme.itemOpacityDisabled : 1

            iconCode: modelData.icon
            color: root.contentColorOf(modelData.id)
            font.pixelSize: modelData.largeIcon ? root.largeIconSize : root.iconSize

            rotation: modelData.rotated ? 180 : 0
        }
    }

    Item {
        id: moreMenuAnchor
        width: 1
        height: 1
    }

    StyledMenuLoader {
        id: moreMenuLoader


        onHandleMenuItem: function(itemId) {
            model.handleMenuItem(itemId)
            root.closeRequested()
        }
    }
}
