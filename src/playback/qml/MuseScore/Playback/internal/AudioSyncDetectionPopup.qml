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
    signal togglePlayRequested()

    //! NOTE Played from here, to tap the tempo: the popup closes when clicking outside it
    property bool isPlaying: false

    property real flexibility: 0.0
    //! NOTE 0: automatic
    property int approxBpm: 0
    property bool everyMeasure: true
    property bool ignoreLeadingSilence: true
    property int toleranceMs: 20

    //! NOTE Tap tempo: the median interval of the last taps; a pause starts over
    property var tapTimes: []

    function tap() {
        const now = Date.now()
        let times = tapTimes.length > 0 && now - tapTimes[tapTimes.length - 1] > 2000 ? [] : tapTimes.slice(-7)
        times.push(now)
        tapTimes = times

        if (times.length < 3) {
            return
        }

        let intervals = []
        for (let i = 1; i < times.length; ++i) {
            intervals.push(times[i] - times[i - 1])
        }
        intervals.sort(function(a, b) { return a - b })
        approxBpm = Math.round(60000 / intervals[Math.floor(intervals.length / 2)])
    }

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

        //! NOTE Space taps the tempo here, instead of playing or pausing
        Keys.onShortcutOverride: function(event) {
            event.accepted = event.key === Qt.Key_Space
        }
        Keys.onSpacePressed: root.tap()

        CheckBox {
            checked: root.ignoreLeadingSilence
            text: qsTrc("playback", "Ignore silence at the beginning")
            navigation.panel: navPanel
            navigation.order: 0

            onClicked: root.ignoreLeadingSilence = !root.ignoreLeadingSilence
        }

        StyledTextLabel {
            text: qsTrc("playback", "Tempo changes")
            horizontalAlignment: Text.AlignLeft
        }

        //! NOTE Constant: music played to a click (pop, rock); flexible: rubato (orchestral music)
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
                navigation.accessible.name: qsTrc("playback", "Tempo changes: constant or variable")

                onMoved: root.flexibility = value
            }

            RowLayout {
                Layout.fillWidth: true

                StyledTextLabel {
                    Layout.fillWidth: true
                    text: qsTrc("playback", "Constant", "tempo")
                    horizontalAlignment: Text.AlignLeft
                }

                StyledTextLabel {
                    Layout.fillWidth: true
                    text: qsTrc("playback", "Variable", "tempo")
                    horizontalAlignment: Text.AlignRight
                }
            }
        }

        RowLayout {
            Layout.fillWidth: true
            spacing: 4

            StyledTextLabel {
                Layout.fillWidth: true
                text: qsTrc("playback", "Tempo")
                horizontalAlignment: Text.AlignLeft
                wrapMode: Text.WordWrap

                ToolTip.visible: tempoHover.hovered
                ToolTip.delay: 500
                ToolTip.text: qsTrc("playback", "Approximate is enough: type it or tap it. Empty: automatic")

                HoverHandler {
                    id: tempoHover
                }
            }

            //! NOTE "--": automatic
            IncrementalPropertyControl {
                Layout.preferredWidth: 84
                isIndeterminate: root.approxBpm <= 0
                currentValue: root.approxBpm > 0 ? root.approxBpm : 120
                minValue: 30
                maxValue: 300
                step: 1
                decimals: 0
                measureUnitsSymbol: qsTrc("playback", "BPM")
                navigation.panel: navPanel
                navigation.order: 2

                onValueEdited: function(newValue) { root.approxBpm = newValue }
            }

            FlatButton {
                Layout.preferredWidth: 26
                Layout.preferredHeight: 26
                icon: IconCode.UNDO
                transparent: true
                enabled: root.approxBpm > 0
                toolTipTitle: qsTrc("playback", "Automatic tempo")
                navigation.panel: navPanel
                navigation.order: 3

                onClicked: root.approxBpm = 0
            }

            FlatButton {
                Layout.preferredWidth: 26
                Layout.preferredHeight: 26
                icon: root.isPlaying ? IconCode.PAUSE_FILL : IconCode.PLAY_FILL
                toolTipTitle: root.isPlaying ? qsTrc("playback", "Pause") : qsTrc("playback", "Play")
                navigation.panel: navPanel
                navigation.order: 4

                onClicked: root.togglePlayRequested()
            }

            FlatButton {
                minWidth: 40
                Layout.preferredHeight: 26
                text: qsTrc("playback", "Tap", "tap tempo")
                toolTipTitle: qsTrc("playback", "Tap tempo")
                toolTipDescription: qsTrc("playback", "Click on the beats, or press Space, while the music plays")
                navigation.panel: navPanel
                navigation.order: 5

                onClicked: root.tap()
            }
        }

        CheckBox {
            checked: root.everyMeasure
            text: qsTrc("playback", "Add a point on every measure")
            navigation.panel: navPanel
            navigation.order: 6

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
                navigation.order: 7

                onValueEdited: function(newValue) { root.toleranceMs = newValue }
            }
        }

        RowLayout {
            Layout.alignment: Qt.AlignRight
            spacing: 8

            FlatButton {
                text: qsTrc("global", "Cancel")
                navigation.panel: navPanel
                navigation.order: 8

                onClicked: root.close()
            }

            FlatButton {
                text: qsTrc("playback", "Detect")
                accentButton: true
                navigation.panel: navPanel
                navigation.order: 9

                onClicked: {
                    root.detectRequested({
                        flexibility: root.flexibility,
                        approxBpm: root.approxBpm,
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
