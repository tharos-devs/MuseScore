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
import QtQuick.Layouts

import Muse.Ui
import Muse.UiComponents

//! NOTE The Audio sync window's beat detection settings
StyledPopupView {
    id: root

    signal detectRequested(var params)

    property real flexibility: 0.5
    property int minBpm: 50
    property int maxBpm: 200
    property int octave: 0
    property bool everyMeasure: false
    property bool ignoreLeadingSilence: true
    property int toleranceMs: 20

    //! NOTE Fixed: the rows wrap or shrink to it (their implicit widths side by side would make it very wide)
    //! NOTE Takes the keyboard focus: Escape closes it (opened with the mouse, the score kept it)
    onOpened: contentColumn.forceActiveFocus()

    contentWidth: 320
    contentHeight: contentColumn.implicitHeight

    NavigationPanel {
        id: navPanel
        name: "AudioSyncDetectionPopup"
        section: root.navigationSection
        direction: NavigationPanel.Vertical
        accessible.name: qsTrc("playback", "Beat detection")

        onNavigationEvent: function(event) {
            if (event.type === NavigationEvent.Escape) {
                root.close()
            }
        }
    }

    ColumnLayout {
        id: contentColumn
        width: root.contentWidth
        spacing: 10

        //! NOTE Escape closes it, also when opened with the mouse (no keyboard navigation then)
        focus: true
        Keys.onEscapePressed: root.close()

        CheckBox {
            checked: root.ignoreLeadingSilence
            text: qsTrc("playback", "Ignore silence at the beginning")
            navigation.panel: navPanel
            navigation.order: 0

            onClicked: root.ignoreLeadingSilence = !root.ignoreLeadingSilence
        }

        StyledTextLabel {
            text: qsTrc("playback", "Tempo")
            horizontalAlignment: Text.AlignLeft
        }

        //! NOTE Steady: music played to a click (pop, rock); flexible: rubato (orchestral music)
        ColumnLayout {
            Layout.fillWidth: true
            spacing: 0

            StyledSlider {
                Layout.fillWidth: true
                Layout.preferredHeight: 30
                from: 0
                to: 1
                stepSize: 0.05
                value: root.flexibility
                fillBackground: false
                navigation.panel: navPanel
                navigation.order: 1
                navigation.accessible.name: qsTrc("playback", "Tempo: steady or flexible")

                onMoved: root.flexibility = value
            }

            RowLayout {
                Layout.fillWidth: true

                StyledTextLabel {
                    Layout.fillWidth: true
                    text: qsTrc("playback", "Steady", "tempo")
                    horizontalAlignment: Text.AlignLeft
                }

                StyledTextLabel {
                    Layout.fillWidth: true
                    text: qsTrc("playback", "Flexible", "tempo")
                    horizontalAlignment: Text.AlignRight
                }
            }
        }

        RowLayout {
            Layout.fillWidth: true
            spacing: 8

            StyledTextLabel {
                Layout.fillWidth: true
                text: qsTrc("playback", "Expected tempo")
                horizontalAlignment: Text.AlignLeft
                wrapMode: Text.WordWrap
            }

            IncrementalPropertyControl {
                Layout.preferredWidth: 84
                currentValue: root.minBpm
                minValue: 20
                maxValue: root.maxBpm - 10
                step: 5
                decimals: 0
                measureUnitsSymbol: qsTrc("playback", "BPM")
                navigation.panel: navPanel
                navigation.order: 2

                onValueEdited: function(newValue) { root.minBpm = newValue }
            }

            StyledTextLabel {
                text: "–"
            }

            IncrementalPropertyControl {
                Layout.preferredWidth: 84
                currentValue: root.maxBpm
                minValue: root.minBpm + 10
                maxValue: 400
                step: 5
                decimals: 0
                measureUnitsSymbol: qsTrc("playback", "BPM")
                navigation.panel: navPanel
                navigation.order: 3

                onValueEdited: function(newValue) { root.maxBpm = newValue }
            }
        }

        RowLayout {
            Layout.fillWidth: true
            spacing: 8

            StyledTextLabel {
                text: qsTrc("playback", "Beats")
                horizontalAlignment: Text.AlignLeft
            }

            StyledDropdown {
                Layout.fillWidth: true
                model: [
                    { text: qsTrc("playback", "Automatic"), value: 0 },
                    { text: qsTrc("playback", "As detected"), value: 1 },
                    { text: qsTrc("playback", "Twice as many (×2)"), value: 2 },
                    { text: qsTrc("playback", "Half as many (÷2)"), value: 3 }
                ]
                currentIndex: indexOfValue(root.octave)
                navigation.panel: navPanel
                navigation.order: 4

                onActivated: function(index, value) { root.octave = value }
            }
        }

        CheckBox {
            checked: root.everyMeasure
            text: qsTrc("playback", "Add a point on every measure")
            navigation.panel: navPanel
            navigation.order: 5

            onClicked: root.everyMeasure = !root.everyMeasure
        }

        RowLayout {
            Layout.fillWidth: true
            spacing: 8
            enabled: !root.everyMeasure

            //! NOTE Without a point on every measure: the largest gap a measure can have from the audio before it gets one
            StyledTextLabel {
                text: qsTrc("playback", "Precision")
                horizontalAlignment: Text.AlignLeft
                Layout.fillWidth: true

                ToolTip.visible: precisionHover.hovered
                ToolTip.delay: 500
                ToolTip.text: qsTrc("playback", "Maximum gap between the score and the audio: smaller adds more points")

                HoverHandler {
                    id: precisionHover
                }
            }

            IncrementalPropertyControl {
                Layout.preferredWidth: 80
                currentValue: root.toleranceMs
                minValue: 2
                maxValue: 200
                step: 5
                decimals: 0
                measureUnitsSymbol: qsTrc("global", "ms")
                navigation.panel: navPanel
                navigation.order: 6

                onValueEdited: function(newValue) { root.toleranceMs = newValue }
            }
        }

        RowLayout {
            Layout.alignment: Qt.AlignRight
            spacing: 8

            FlatButton {
                text: qsTrc("global", "Cancel")
                navigation.panel: navPanel
                navigation.order: 7

                onClicked: root.close()
            }

            FlatButton {
                text: qsTrc("playback", "Detect")
                accentButton: true
                navigation.panel: navPanel
                navigation.order: 8

                onClicked: {
                    root.detectRequested({
                        flexibility: root.flexibility,
                        minBpm: root.minBpm,
                        maxBpm: root.maxBpm,
                        octave: root.octave,
                        everyMeasure: root.everyMeasure,
                        ignoreLeadingSilence: root.ignoreLeadingSilence,
                        toleranceMs: root.toleranceMs
                    })
                    root.close()
                }
            }
        }
    }
}
