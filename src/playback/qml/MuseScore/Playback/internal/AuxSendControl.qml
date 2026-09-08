/*
 * SPDX-License-Identifier: GPL-3.0-only
 * MuseScore-Studio-CLA-applies
 *
 * MuseScore Studio
 * Music Composition & Notation
 *
 * Copyright (C) 2023 MuseScore Limited and others
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
import QtQuick.Layouts

import Muse.Ui
import Muse.UiComponents

Item {
    id: root

    property AuxSendItem auxSendItemModel: null

    property color accentColor: ui.theme.accentColor

    readonly property string title: root.auxSendItemModel ? root.auxSendItemModel.title : ""

    property NavigationPanel navigationPanel: null
    property int navigationRowStart: 0
    //! NOTE: knob is at +0; AudioResourceControl starts at +1 and its own activity/title/
    //! selector controls sit at a further +1/+2/+3 relative to THAT, i.e. +2/+3/+4 here
    readonly property int navigationRowEnd: navigationRowStart + 4
    property string navigationName: ""
    property string accessibleName: ""

    signal navigateControlIndexChanged(var index)

    height: 24
    width: 96

    RowLayout {
        id: content

        anchors.fill: parent

        spacing: 8

        KnobControl {
            id: audioSignalAmountKnob

            radius: root.height / 2 + 1.5

            enabled: root.auxSendItemModel ? !root.auxSendItemModel.isBlank : false

            from: 0
            to: 100
            stepSize: 1
            value: root.auxSendItemModel ? root.auxSendItemModel.audioSignalPercentage : 0

            accentControl: root.auxSendItemModel ? root.auxSendItemModel.isActive : false
            accentColor: root.accentColor

            navigation.panel: root.navigationPanel
            navigation.row: root.navigationRowStart
            navigation.accessible.name: root.accessibleName
            navigation.onActiveChanged: {
                if (navigation.active) {
                    root.navigateControlIndexChanged({row: navigation.row, column: navigation.column})
                }
            }

            onNewValueRequested: function(newValue) {
                root.auxSendItemModel.audioSignalPercentage = newValue
            }

            //! NOTE: while dragging, the slot button next to the knob (resourceControl,
            //! below) shows the live percentage in place of the target bus name - restores
            //! this control's pre-redesign behavior, driven from the model's own title()
            //! (see AuxSendItem::isDragging) rather than a separate floating readout
            Connections {
                target: audioSignalAmountKnob.mouseArea
                function onPressedChanged() {
                    if (root.auxSendItemModel) {
                        root.auxSendItemModel.isDragging = audioSignalAmountKnob.mouseArea.pressed
                    }
                }
            }
        }

        AudioResourceControl {
            id: resourceControl

            Layout.fillWidth: true
            Layout.fillHeight: true

            resourceItemModel: root.auxSendItemModel

            navigationPanel: root.navigationPanel
            navigationRowStart: root.navigationRowStart + 1 // NOTE: 3 controls (activity/title/selector)
            navigationName: root.navigationName
            accessibleName: root.accessibleName

            onTurnedOn: {
                root.auxSendItemModel.isActive = true
            }

            onTurnedOff: {
                root.auxSendItemModel.isActive = false
            }

            accentColor: root.accentColor

            onNavigateControlIndexChanged: function(index) {
                root.navigateControlIndexChanged(index)
            }
        }
    }
}
