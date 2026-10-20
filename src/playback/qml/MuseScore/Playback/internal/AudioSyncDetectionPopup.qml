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
import QtQuick.Layouts

import Muse.Ui
import Muse.UiComponents

//! NOTE The Audio sync window's beat detection settings
StyledPopupView {
    id: root

    signal detectRequested(var params)

    property real sensitivity: 0.5
    property int minBpm: 50
    property int maxBpm: 200
    property int octave: 0
    property bool everyMeasure: false
    property int toleranceMs: 20

    contentWidth: contentColumn.implicitWidth
    contentHeight: contentColumn.implicitHeight

    NavigationPanel {
        id: navPanel
        name: "AudioSyncDetectionPopup"
        section: root.navigationSection
        direction: NavigationPanel.Vertical
        accessible.name: qsTrc("playback", "Beat detection")
    }

    ColumnLayout {
        id: contentColumn
        width: 320
        spacing: 10

        StyledTextLabel {
            Layout.fillWidth: true
            text: qsTrc("playback", "Detects the beats from the selected sync point on (the first one if none is selected), which must be on a downbeat.")
            wrapMode: Text.WordWrap
            horizontalAlignment: Text.AlignLeft
        }

        StyledTextLabel {
            text: qsTrc("playback", "Sensitivity")
            horizontalAlignment: Text.AlignLeft
        }

        StyledSlider {
            Layout.fillWidth: true
            Layout.preferredHeight: 30
            from: 0
            to: 1
            stepSize: 0.05
            value: root.sensitivity
            fillBackground: false

            onMoved: root.sensitivity = value
        }

        RowLayout {
            spacing: 8

            StyledTextLabel {
                text: qsTrc("playback", "Expected tempo")
                horizontalAlignment: Text.AlignLeft
            }

            IncrementalPropertyControl {
                Layout.preferredWidth: 90
                currentValue: root.minBpm
                minValue: 20
                maxValue: root.maxBpm - 10
                step: 5
                decimals: 0
                measureUnitsSymbol: qsTrc("playback", "BPM")
                navigation.panel: navPanel
                navigation.order: 1

                onValueEdited: function(newValue) { root.minBpm = newValue }
            }

            StyledTextLabel {
                text: "–"
            }

            IncrementalPropertyControl {
                Layout.preferredWidth: 90
                currentValue: root.maxBpm
                minValue: root.minBpm + 10
                maxValue: 400
                step: 5
                decimals: 0
                measureUnitsSymbol: qsTrc("playback", "BPM")
                navigation.panel: navPanel
                navigation.order: 2

                onValueEdited: function(newValue) { root.maxBpm = newValue }
            }
        }

        RowLayout {
            spacing: 8

            StyledTextLabel {
                text: qsTrc("playback", "Beats")
                horizontalAlignment: Text.AlignLeft
            }

            StyledDropdown {
                Layout.preferredWidth: 220
                model: [
                    { text: qsTrc("playback", "Automatic"), value: 0 },
                    { text: qsTrc("playback", "As detected"), value: 1 },
                    { text: qsTrc("playback", "Twice as many (×2)"), value: 2 },
                    { text: qsTrc("playback", "Half as many (÷2)"), value: 3 }
                ]
                currentIndex: indexOfValue(root.octave)
                navigation.panel: navPanel
                navigation.order: 3

                onActivated: function(index, value) { root.octave = value }
            }
        }

        CheckBox {
            checked: root.everyMeasure
            text: qsTrc("playback", "A point on every measure")
            navigation.panel: navPanel
            navigation.order: 4

            onClicked: root.everyMeasure = !root.everyMeasure
        }

        RowLayout {
            spacing: 8
            enabled: !root.everyMeasure

            StyledTextLabel {
                text: qsTrc("playback", "Otherwise, where the tempo changes by more than")
                horizontalAlignment: Text.AlignLeft
                wrapMode: Text.WordWrap
                Layout.fillWidth: true
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
                navigation.order: 5

                onValueEdited: function(newValue) { root.toleranceMs = newValue }
            }
        }

        FlatButton {
            Layout.alignment: Qt.AlignRight
            text: qsTrc("playback", "Detect")
            accentButton: true
            navigation.panel: navPanel
            navigation.order: 6

            onClicked: {
                root.detectRequested({
                    sensitivity: root.sensitivity,
                    minBpm: root.minBpm,
                    maxBpm: root.maxBpm,
                    octave: root.octave,
                    everyMeasure: root.everyMeasure,
                    toleranceMs: root.toleranceMs
                })
                root.close()
            }
        }
    }
}
