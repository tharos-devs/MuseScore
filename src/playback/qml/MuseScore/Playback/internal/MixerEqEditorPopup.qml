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
import QtQuick.Layouts

import Muse.Ui
import Muse.UiComponents

import MuseScore.Playback

//! NOTE A Mixer channel's EQ: its curve, whose points are dragged (frequency, gain) or scrolled over (Q),
//! and below it each band's on/off, type, gain, frequency and Q
StyledPopupView {
    id: root

    property MixerChannelItem channelItem: null

    readonly property var eq: root.channelItem ? root.channelItem.eq : ({})
    readonly property var bandNames: [ qsTrc("playback", "1 LO"), qsTrc("playback", "2 LMF"),
                                       qsTrc("playback", "3 HMF"), qsTrc("playback", "4 HI") ]

    contentWidth: 600
    contentHeight: contentColumn.implicitHeight

    NavigationPanel {
        id: navPanel
        name: "MixerEqEditor"
        section: root.navigationSection
        direction: NavigationPanel.Both
        accessible.name: qsTrc("playback", "EQ")
    }

    // a scroll gesture over a point is one undoable change
    Timer {
        id: qScrollEndTimer

        interval: 500
        onTriggered: {
            if (root.channelItem) {
                root.channelItem.endEqChange()
            }
        }
    }

    ColumnLayout {
        id: contentColumn

        width: root.contentWidth
        spacing: 8

        RowLayout {
            Layout.fillWidth: true
            spacing: 8

            FlatToggleButton {
                Layout.preferredWidth: 24
                Layout.preferredHeight: 24

                icon: IconCode.BYPASS
                checked: Boolean(root.eq.enabled)

                toolTipTitle: root.eq.enabled ? qsTrc("playback", "Disable EQ") : qsTrc("playback", "Enable EQ")

                navigation.panel: navPanel
                navigation.row: 0
                navigation.column: 0

                onToggled: root.channelItem.setEqEnabled(!root.eq.enabled)
            }

            StyledTextLabel {
                text: qsTrc("playback", "EQ") + (root.channelItem ? " – " + root.channelItem.title : "")
                font: ui.theme.bodyBoldFont
                horizontalAlignment: Text.AlignLeft
            }

            Item {
                Layout.fillWidth: true
            }

            StyledTextLabel {
                text: curveView.hoverText
                horizontalAlignment: Text.AlignRight
            }

            FlatButton {
                text: qsTrc("playback", "Reset")

                navigation.panel: navPanel
                navigation.row: 0
                navigation.column: 1

                onClicked: root.channelItem.resetEq()
            }
        }

        EqCurveView {
            id: curveView

            Layout.fillWidth: true
            Layout.preferredHeight: 260

            eq: root.eq
            editable: true

            backgroundColor: ui.theme.textFieldColor
            curveColor: ui.theme.accentColor
            gridColor: Utils.colorWithAlpha(ui.theme.fontPrimaryColor, 0.15)
            textColor: ui.theme.fontPrimaryColor

            onBandDragStarted: function(band) {
                root.channelItem.beginEqChange()
            }

            onBandDragged: function(band, frequency, gain) {
                root.channelItem.setEqBandFrequency(band, frequency)
                if (!isNaN(gain)) {
                    root.channelItem.setEqBandGain(band, gain)
                }
            }

            onBandDragFinished: function(band) {
                root.channelItem.endEqChange()
            }

            onBandQScrolled: function(band, q) {
                if (!qScrollEndTimer.running) {
                    root.channelItem.beginEqChange()
                }
                root.channelItem.setEqBandQ(band, q)
                qScrollEndTimer.restart()
            }
        }

        RowLayout {
            Layout.fillWidth: true
            Layout.bottomMargin: 4
            spacing: 8

            Repeater {
                model: 4

                delegate: Column {
                    id: bandColumn

                    required property int index

                    readonly property var band: root.eq.bands ? root.eq.bands[index] : ({})

                    Layout.fillWidth: true
                    spacing: 6

                    Rectangle {
                        width: parent.width
                        height: 28
                        radius: 3

                        color: curveView.selectedBand === bandColumn.index ? Utils.colorWithAlpha(ui.theme.accentColor, 0.3)
                                                                           : ui.theme.buttonColor

                        RowLayout {
                            anchors.fill: parent
                            anchors.leftMargin: 2
                            anchors.rightMargin: 2
                            spacing: 4

                            FlatToggleButton {
                                Layout.preferredWidth: 22
                                Layout.preferredHeight: 22

                                icon: IconCode.BYPASS
                                transparent: true
                                checked: Boolean(bandColumn.band.enabled)

                                toolTipTitle: bandColumn.band.enabled ? qsTrc("playback", "Disable band")
                                                                      : qsTrc("playback", "Enable band")

                                navigation.panel: navPanel
                                navigation.row: 1
                                navigation.column: bandColumn.index * 2

                                onToggled: root.channelItem.setEqBandEnabled(bandColumn.index, !bandColumn.band.enabled)
                            }

                            StyledTextLabel {
                                Layout.fillWidth: true
                                text: root.bandNames[bandColumn.index]
                                horizontalAlignment: Text.AlignLeft
                            }

                            FlatButton {
                                id: typeButton

                                Layout.preferredHeight: 22

                                text: bandColumn.band.typeTitle ?? ""
                                transparent: true

                                toolTipTitle: qsTrc("playback", "Band type")

                                navigation.panel: navPanel
                                navigation.row: 1
                                navigation.column: bandColumn.index * 2 + 1

                                onClicked: typeMenuLoader.toggleOpened(bandColumn.typeMenuItems())

                                StyledMenuLoader {
                                    id: typeMenuLoader

                                    onHandleMenuItem: function(itemId) {
                                        root.channelItem.setEqBandType(bandColumn.index, Number(itemId))
                                    }
                                }
                            }
                        }

                        MouseArea {
                            anchors.fill: parent
                            z: -1
                            onClicked: curveView.selectedBand = bandColumn.index
                        }
                    }

                    IncrementalPropertyControl {
                        width: parent.width

                        enabled: Boolean(bandColumn.band.hasGain)
                        currentValue: bandColumn.band.gain ?? 0
                        minValue: -24
                        maxValue: 24
                        step: 0.5
                        decimals: 1
                        measureUnitsSymbol: qsTrc("global", "dB")

                        navigation.panel: navPanel
                        navigation.row: 2
                        navigation.column: bandColumn.index
                        navigation.accessible.name: root.bandNames[bandColumn.index] + " " + qsTrc("playback", "Gain")

                        onValueEdited: function(newValue) {
                            root.channelItem.beginEqChange()
                            root.channelItem.setEqBandGain(bandColumn.index, newValue)
                        }

                        onValueEditingFinished: function(newValue) {
                            root.channelItem.setEqBandGain(bandColumn.index, newValue)
                            root.channelItem.endEqChange()
                        }
                    }

                    IncrementalPropertyControl {
                        width: parent.width

                        currentValue: bandColumn.band.frequency ?? 1000
                        minValue: 20
                        maxValue: 20000
                        step: currentValue >= 1000 ? 100 : (currentValue >= 100 ? 10 : 1)
                        decimals: currentValue >= 1000 ? 0 : 1
                        measureUnitsSymbol: qsTrc("playback", "Hz")

                        navigation.panel: navPanel
                        navigation.row: 3
                        navigation.column: bandColumn.index
                        navigation.accessible.name: root.bandNames[bandColumn.index] + " " + qsTrc("playback", "Frequency")

                        onValueEdited: function(newValue) {
                            root.channelItem.beginEqChange()
                            root.channelItem.setEqBandFrequency(bandColumn.index, newValue)
                        }

                        onValueEditingFinished: function(newValue) {
                            root.channelItem.setEqBandFrequency(bandColumn.index, newValue)
                            root.channelItem.endEqChange()
                        }
                    }

                    IncrementalPropertyControl {
                        width: parent.width

                        enabled: Boolean(bandColumn.band.hasQ)
                        currentValue: bandColumn.band.q ?? 1
                        minValue: 0.1
                        maxValue: 12
                        step: 0.1
                        decimals: 1

                        navigation.panel: navPanel
                        navigation.row: 4
                        navigation.column: bandColumn.index
                        navigation.accessible.name: root.bandNames[bandColumn.index] + " " + qsTrc("playback", "Q")

                        onValueEdited: function(newValue) {
                            root.channelItem.beginEqChange()
                            root.channelItem.setEqBandQ(bandColumn.index, newValue)
                        }

                        onValueEditingFinished: function(newValue) {
                            root.channelItem.setEqBandQ(bandColumn.index, newValue)
                            root.channelItem.endEqChange()
                        }
                    }

                    function typeMenuItems() {
                        const types = root.channelItem.eqBandTypes(bandColumn.index)
                        const items = []
                        for (let i = 0; i < types.length; ++i) {
                            items.push({
                                id: String(types[i].value),
                                title: types[i].title,
                                checkable: true,
                                checked: types[i].value === bandColumn.band.type,
                                enabled: true
                            })
                        }
                        return items
                    }
                }
            }
        }
    }
}
