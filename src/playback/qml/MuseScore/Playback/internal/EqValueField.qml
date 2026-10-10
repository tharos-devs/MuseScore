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

//! NOTE A value shown as a filled bar: dragged sideways (Shift: finely) or scrolled to change it,
//! double-clicked to type it. A gesture is bracketed by gestureStarted()/gestureFinished(), for undo
Rectangle {
    id: root

    property real value: 0
    property real from: 0
    property real to: 1
    property bool logarithmic: false
    property int decimals: 1
    property string unit: ""

    property alias navigation: navCtrl

    signal gestureStarted()
    signal valueRequested(real newValue)
    signal gestureFinished()

    implicitHeight: 22
    radius: 3
    color: ui.theme.textFieldColor
    border.width: (mouseArea.containsMouse || navCtrl.highlight) && root.enabled ? 1 : 0
    border.color: ui.theme.strokeColor
    opacity: root.enabled ? 1 : ui.theme.itemOpacityDisabled

    function ratioFor(value) {
        if (root.to <= root.from) {
            return 0
        }

        const v = Math.min(Math.max(value, root.from), root.to)
        if (root.logarithmic) {
            return Math.log(v / root.from) / Math.log(root.to / root.from)
        }
        return (v - root.from) / (root.to - root.from)
    }

    function valueFor(ratio) {
        const r = Math.min(Math.max(ratio, 0), 1)
        const v = root.logarithmic ? root.from * Math.pow(root.to / root.from, r)
                                   : root.from + r * (root.to - root.from)
        const factor = Math.pow(10, root.decimals)
        return Math.round(v * factor) / factor
    }

    function formatted(value) {
        return value.toFixed(root.decimals) + root.unit
    }

    // the position
    Rectangle {
        anchors.left: parent.left
        anchors.top: parent.top
        anchors.bottom: parent.bottom
        anchors.margins: 1

        width: Math.max(0, (parent.width - 2) * root.ratioFor(root.value))
        radius: root.radius
        color: Utils.colorWithAlpha(ui.theme.accentColor, 0.35)

        Rectangle {
            anchors.right: parent.right
            anchors.top: parent.top
            anchors.bottom: parent.bottom
            width: 2
            color: ui.theme.accentColor
            visible: parent.width > 1
        }
    }

    StyledTextLabel {
        anchors.fill: parent
        anchors.rightMargin: 6
        anchors.leftMargin: 6

        visible: !textInput.visible
        text: root.formatted(root.value)
        horizontalAlignment: Text.AlignRight
    }

    TextInputField {
        id: textInput

        anchors.fill: parent
        visible: false

        textSidePadding: 6
        textHorizontalAlignment: Qt.AlignRight
        background.radius: root.radius

        onTextEditingFinished: function(newTextValue) {
            visible = false

            const typed = parseFloat(String(newTextValue).replace(",", "."))
            if (!isNaN(typed)) {
                root.gestureStarted()
                root.valueRequested(Math.min(Math.max(typed, root.from), root.to))
                root.gestureFinished()
            }
        }

        onEscaped: {
            visible = false
        }
    }

    // a scroll is one gesture
    Timer {
        id: wheelEndTimer
        interval: 500
        onTriggered: root.gestureFinished()
    }

    MouseArea {
        id: mouseArea

        anchors.fill: parent
        visible: !textInput.visible

        hoverEnabled: true
        cursorShape: Qt.SizeHorCursor
        preventStealing: true

        property real pressX: 0
        property real pressRatio: 0
        property bool dragging: false

        onPressed: function(mouse) {
            pressX = mouse.x
            pressRatio = root.ratioFor(root.value)
            dragging = false
        }

        onPositionChanged: function(mouse) {
            if (!pressed) {
                return
            }

            if (!dragging) {
                if (Math.abs(mouse.x - pressX) < 2) {
                    return
                }
                dragging = true
                root.gestureStarted()
            }

            const sensitivity = (mouse.modifiers & Qt.ShiftModifier) ? 0.1 : 1
            const ratio = pressRatio + (mouse.x - pressX) / root.width * sensitivity
            root.valueRequested(root.valueFor(ratio))
        }

        onReleased: {
            if (dragging) {
                dragging = false
                root.gestureFinished()
            }
        }

        onDoubleClicked: {
            textInput.currentText = root.value.toFixed(root.decimals)
            textInput.visible = true
            textInput.ensureActiveFocus()
            textInput.selectAll()
        }

        onWheel: function(wheel) {
            const notches = wheel.angleDelta.y / 120
            if (notches === 0) {
                return
            }

            if (!wheelEndTimer.running) {
                root.gestureStarted()
            }

            const step = (wheel.modifiers & Qt.ShiftModifier) ? 0.002 : 0.02
            root.valueRequested(root.valueFor(root.ratioFor(root.value) + notches * step))
            wheelEndTimer.restart()
        }
    }

    NavigationControl {
        id: navCtrl

        name: "EqValueField"
        enabled: root.enabled && root.visible

        accessible.role: MUAccessible.Range
        accessible.visualItem: root
        accessible.value: root.formatted(root.value)
        accessible.minimumValue: root.from
        accessible.maximumValue: root.to

        onNavigationEvent: function(event) {
            let ratioStep = 0
            switch (event.type) {
            case NavigationEvent.Left:
            case NavigationEvent.Down:
                ratioStep = -0.02
                break
            case NavigationEvent.Right:
            case NavigationEvent.Up:
                ratioStep = 0.02
                break
            default:
                return
            }

            root.gestureStarted()
            root.valueRequested(root.valueFor(root.ratioFor(root.value) + ratioStep))
            root.gestureFinished()
            event.accepted = true
        }

        onTriggered: mouseArea.doubleClicked(null)
    }

    NavigationFocusBorder {
        navigationCtrl: navCtrl
    }
}
