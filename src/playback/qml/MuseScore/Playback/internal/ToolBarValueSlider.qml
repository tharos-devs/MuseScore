/*
 * SPDX-License-Identifier: GPL-3.0-only
 * MuseScore-CLA-applies
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
import QtQuick.Controls

import Muse.Ui
import Muse.UiComponents

//! NOTE A small toolbar slider with its name above it and its value at its right, a double-click (two presses close
//! together) back to its default value; a new value is sent once released (while dragged too when live): the Audio
//! sync window's Pitch, Speed and Zoom. Without a title or a value text, just the slider
Row {
    id: root

    property string title: ""
    property real from: 0
    property real to: 1
    property real stepSize: 1
    property real value: 0
    property real defaultValue: 0
    property string valueText: ""
    //! NOTE The value being computed (e.g. a transformed copy of the audio): its text dimmed
    property bool busy: false
    property bool live: false
    property int sliderWidth: 80

    property NavigationPanel navigationPanel: null
    property int navigationOrder: 0

    //! NOTE The slider's position, while dragged too
    readonly property real currentValue: slider.value

    signal valueEdited(real newValue)

    spacing: 4

    Column {
        anchors.verticalCenter: parent.verticalCenter
        spacing: 0

        StyledTextLabel {
            anchors.horizontalCenter: parent.horizontalCenter
            visible: root.title.length > 0
            text: root.title
            font.pixelSize: 9
            opacity: 0.75
        }

        StyledSlider {
            id: slider

            width: root.sliderWidth
            from: root.from
            to: root.to
            stepSize: root.stepSize
            snapMode: Slider.SnapAlways
            fillBackground: false
            value: root.value
            navigation.panel: root.navigationPanel
            navigation.order: root.navigationOrder
            navigation.accessible.name: root.title

            //! NOTE Two presses close together: a MouseArea over it would have to let the presses through to the
            //! slider, and then never gets the double-click
            property double lastPressTime: 0
            property bool resetOnRelease: false

            onPressedChanged: {
                if (pressed) {
                    const now = Date.now()
                    resetOnRelease = now - lastPressTime < 400
                    lastPressTime = resetOnRelease ? 0 : now
                    return
                }

                //! NOTE Applied once released: the slider sets its value from the mouse until then
                if (resetOnRelease) {
                    resetOnRelease = false
                    root.valueEdited(root.defaultValue)
                    value = Qt.binding(function() { return root.value })
                    return
                }

                root.valueEdited(value)
            }

            onMoved: {
                if (!pressed || root.live) {
                    root.valueEdited(value)
                }
            }
        }
    }

    StyledTextLabel {
        anchors.verticalCenter: parent.verticalCenter
        width: 34
        visible: root.valueText.length > 0
        horizontalAlignment: Text.AlignLeft
        font.features: { "tnum": 1 }
        opacity: root.busy ? 0.5 : 1.0
        text: root.valueText
    }
}
