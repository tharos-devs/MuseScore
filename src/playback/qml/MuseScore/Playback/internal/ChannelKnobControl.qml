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

//! NOTE: a channel's Pan, Gain or track delay (Mixer, Track list): a knob and its field. Compact: no field, the value
//! shows while the knob is dragged
Row {
    id: root

    required property MixerChannelItem channelItem
    property bool isGain: false
    //! NOTE: the track delay is applied once set (a release of the knob, a value typed), not along a drag: each change
    //! rebuilds what the instrument plays
    property bool isDelay: false
    property bool compact: false
    //! NOTE: the knob's size (KnobControl's default)
    property real knobRadius: 16

    property NavigationPanel navigationPanel: channelItem.panel
    property int navigationRowStart: 0
    property string accessibleName: ""

    signal navigateControlIndexChanged(var index)

    readonly property int value: root.isDelay ? (prv.delayDragging ? prv.draggedDelay : root.channelItem.delayMs)
                                              : root.isGain ? root.channelItem.gain : root.channelItem.balance
    readonly property int minValue: root.isDelay ? root.channelItem.delayMsMin
                                                 : root.isGain ? root.channelItem.gainMin : root.channelItem.balanceMin
    readonly property int maxValue: root.isDelay ? root.channelItem.delayMsMax
                                                 : root.isGain ? root.channelItem.gainMax : root.channelItem.balanceMax
    //! NOTE: the pan curve drawn on the score sets it then
    readonly property bool editable: root.isDelay ? root.channelItem.delayAvailable
                                                  : root.isGain || !root.channelItem.hasBalanceAutomation
    readonly property string unit: root.isDelay ? qsTrc("global", "ms") : root.isGain ? "dB" : ""

    QtObject {
        id: prv

        property bool delayDragging: false
        property int draggedDelay: 0
    }

    function setValue(newValue) {
        if (root.isDelay) {
            if (prv.delayDragging) {
                prv.draggedDelay = newValue
            } else {
                // not a drag (keyboard, wheel): applied at once
                root.channelItem.commitDelayMs(newValue)
            }
        } else if (root.isGain) {
            root.channelItem.gain = newValue
        } else {
            root.channelItem.balance = newValue
        }
    }

    function beginChange() {
        if (root.isDelay) {
            prv.draggedDelay = root.channelItem.delayMs
            prv.delayDragging = true
        } else if (root.isGain) {
            root.channelItem.beginGainChange()
        } else {
            root.channelItem.beginBalanceChange()
        }
    }

    function endChange() {
        if (root.isDelay) {
            prv.delayDragging = false
            root.channelItem.commitDelayMs(prv.draggedDelay)
        } else if (root.isGain) {
            root.channelItem.endGainChange()
        } else {
            root.channelItem.endBalanceChange()
        }
    }

    spacing: 8

    KnobControl {
        id: knob

        radius: root.knobRadius
        from: root.minValue
        to: root.maxValue
        value: root.value
        stepSize: 1
        isBalanceKnob: true
        accentColor: root.channelItem.hasCustomColor ? root.channelItem.color : ui.theme.accentColor
        enabled: root.editable
        opacity: enabled ? 1.0 : ui.theme.itemOpacityDisabled

        //! NOTE: compact hides the field below, so this is the only way left to read the exact value -- shown only
        //! while actively dragging, same "reveal on interaction" precedent as AuxSendControl's isDragging title swap
        KnobDragValueTooltip {
            visible: root.compact && knob.mouseArea.pressed
            text: Math.round(root.value) + root.unit
        }

        navigation.panel: root.navigationPanel
        navigation.row: root.navigationRowStart
        navigation.accessible.name: root.accessibleName
        navigation.onActiveChanged: {
            if (navigation.active) {
                root.navigateControlIndexChanged({row: navigation.row, column: navigation.column})
            }
        }

        onNewValueRequested: function(newValue) {
            root.setValue(newValue)
        }

        Connections {
            target: knob.mouseArea
            function onPressedChanged() {
                if (knob.mouseArea.pressed) {
                    root.beginChange()
                } else {
                    root.endChange()
                }
            }
        }
    }

    TextInputField {
        visible: !root.compact

        anchors.verticalCenter: knob.verticalCenter

        height: 24
        width: 36

        enabled: root.editable

        textHorizontalAlignment: Qt.AlignHCenter
        textSidePadding: 0
        background.radius: 2

        navigation.panel: root.navigationPanel
        navigation.row: root.navigationRowStart + 1
        navigation.accessible.name: root.accessibleName + " " + currentText
        navigation.onActiveChanged: {
            if (navigation.active) {
                root.navigateControlIndexChanged({row: navigation.row, column: navigation.column})
            }
        }

        validator: IntInputValidator {
            top: root.maxValue
            bottom: root.minValue
        }

        currentText: root.value

        onTextEdited: function(newTextValue) {
            if (root.isDelay) {
                return
            }

            if (root.value !== Number(newTextValue)) {
                root.beginChange()
                root.setValue(Number(newTextValue))
                root.endChange()
            }
        }

        onTextEditingFinished: function(newTextValue) {
            const newValue = parseInt(newTextValue)
            if (root.isDelay && !isNaN(newValue)) {
                root.channelItem.commitDelayMs(newValue)
            }
        }
    }
}
