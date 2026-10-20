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
import MuseScore.Playback

//! NOTE The Audio sync window: a reference recording's waveform under the score's measures and beats,
//! placed by the score's current tempo
Item {
    id: root

    property NavigationSection navigationSection: null
    property int contentNavigationPanelOrderStart: 0

    readonly property int toolbarHeight: 50

    NavigationPanel {
        id: navigationPanel
        name: "AudioSyncPanel"
        section: root.navigationSection
        order: root.contentNavigationPanelOrderStart
        direction: NavigationPanel.Horizontal
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        // Toolbar: file and sync on the left, transport in the middle (under the playback time), view on the right
        Item {
            Layout.fillWidth: true
            Layout.preferredHeight: root.toolbarHeight

            RowLayout {
                id: leftGroup

                anchors.left: parent.left
                anchors.leftMargin: 8
                anchors.verticalCenter: parent.verticalCenter
                //! NOTE Same line as the transport buttons (the clock is above them)
                anchors.verticalCenterOffset: 4
                spacing: 4

                RecentFilesLoadButton {
                    path: syncView.audioPath
                    dialogTitle: qsTrc("playback", "Choose reference audio")
                    filter: qsTrc("playback", "Audio files (*.wav *.wave *.aif *.aiff *.mp3 *.flac *.ogg *.oga *.opus *.m4a *.aac *.wma *.mp4 *.mov *.m4v *.mkv *.webm *.avi);;All files (*)")
                    recentFilesTitle: qsTrc("playback", "Recently opened audio files")
                    recentFiles: function() { return syncView.recentAudioFiles() }
                    navigationPanel: navigationPanel
                    navigationOrderStart: 1

                    onPathChosen: function(newPath) {
                        syncView.setAudioFile(newPath)
                    }

                    onClearRecentFilesRequested: {
                        syncView.clearRecentAudioFiles()
                    }
                }

                FlatButton {
                    visible: syncView.hasAudio
                    icon: IconCode.DELETE_TANK
                    buttonType: FlatButton.IconOnly
                    transparent: true
                    toolTipTitle: qsTrc("playback", "Remove reference audio")
                    navigation.panel: navigationPanel
                    navigation.order: 5

                    onClicked: syncView.removeAudio()
                }

                SeparatorLine { orientation: Qt.Vertical; Layout.preferredHeight: 20; Layout.leftMargin: 4; Layout.rightMargin: 4 }

                FlatButton {
                    minWidth: 28
                    text: qsTrc("playback", "Sync", "Use audio sync: the score's tempo follows the audio sync points")
                    accentButton: syncView.audioSyncEnabled
                    toolTipTitle: qsTrc("playback", "Use audio sync")
                    toolTipDescription: qsTrc("playback", "Play the score at the tempo given by the audio sync points. Double-click: add a point on the nearest beat (Shift: anywhere). Drag a point to sync it to the audio. Delete: remove the selected point. Left/Right arrows: move it by 1 ms (Shift: 10 ms).")
                    navigation.panel: navigationPanel
                    navigation.order: 6

                    onClicked: syncView.audioSyncEnabled = !syncView.audioSyncEnabled
                }

                PopupButton {
                    minWidth: 28
                    enabled: syncView.audioSyncEnabled && syncView.waveformReady && !syncView.detecting
                    text: qsTrc("playback", "Detect…", "Detect audio sync points automatically")
                    toolTipTitle: qsTrc("playback", "Detect sync points")
                    toolTipDescription: qsTrc("playback", "Detect the beats of the audio and propose sync points")
                    navigation.panel: navigationPanel
                    navigation.order: 7

                    popupComponent: AudioSyncDetectionPopup {
                        onDetectRequested: function(params) {
                            syncView.detectPoints(params)
                        }
                    }
                }

                ProgressBar {
                    Layout.preferredWidth: 120
                    Layout.preferredHeight: 14
                    visible: syncView.detecting
                    from: 0
                    to: 100
                    value: Math.round(syncView.detectionProgress * 100)
                }

                FlatButton {
                    visible: syncView.detecting
                    icon: IconCode.CLOSE_X_ROUNDED
                    buttonType: FlatButton.IconOnly
                    transparent: true
                    toolTipTitle: qsTrc("playback", "Cancel detection")
                    navigation.panel: navigationPanel
                    navigation.order: 8

                    onClicked: syncView.cancelDetection()
                }
            }

            //! NOTE Compact, like a transport bar: small grey buttons under the playback clock
            //! NOTE The buttons on the same line as the other groups' controls, the clock above them
            Item {
                id: transportGroup

                anchors.fill: parent

                StyledTextLabel {
                    anchors.horizontalCenter: transportButtons.horizontalCenter
                    anchors.bottom: transportButtons.top
                    anchors.bottomMargin: 1
                    text: syncView.timeText
                    font.pixelSize: 10
                    //! NOTE Fixed-width digits: the clock doesn't wobble while it runs
                    font.features: { "tnum": 1 }
                }

                Row {
                    id: transportButtons

                    anchors.horizontalCenter: parent.horizontalCenter
                    anchors.verticalCenter: parent.verticalCenter
                    anchors.verticalCenterOffset: 4
                    spacing: 4

                    FlatButton {
                        width: 26
                        height: 26
                        icon: IconCode.METRONOME
                        accentButton: syncView.metronomeEnabled
                        toolTipTitle: qsTrc("playback", "Metronome")
                        navigation.panel: navigationPanel
                        navigation.order: 20

                        onClicked: syncView.toggleMetronome()
                    }

                    FlatButton {
                        width: 26
                        height: 26
                        minWidth: 26
                        enabled: syncView.hasAudio
                        text: qsTrc("playback", "M", "Mute the reference audio")
                        accentButton: syncView.muted
                        toolTipTitle: qsTrc("playback", "Mute")
                        navigation.panel: navigationPanel
                        navigation.order: 21

                        onClicked: syncView.muted = !syncView.muted
                    }

                    FlatButton {
                        width: 26
                        height: 26
                        minWidth: 26
                        enabled: syncView.hasAudio
                        text: qsTrc("playback", "S", "Solo the reference audio")
                        accentButton: syncView.solo
                        toolTipTitle: qsTrc("playback", "Solo")
                        navigation.panel: navigationPanel
                        navigation.order: 22

                        onClicked: syncView.solo = !syncView.solo
                    }

                    FlatButton {
                        width: 26
                        height: 26
                        icon: IconCode.REWIND_START_FILL
                        toolTipTitle: qsTrc("playback", "Go to start")
                        navigation.panel: navigationPanel
                        navigation.order: 23

                        onClicked: syncView.rewindToStart()
                    }

                    FlatButton {
                        width: 26
                        height: 26
                        icon: syncView.isPlaying ? IconCode.PAUSE_FILL : IconCode.PLAY_FILL
                        toolTipTitle: syncView.isPlaying ? qsTrc("playback", "Pause") : qsTrc("playback", "Play")
                        navigation.panel: navigationPanel
                        navigation.order: 24

                        onClicked: syncView.togglePlay()
                    }

                    FlatButton {
                        width: 26
                        height: 26
                        icon: IconCode.STOP_FILL
                        toolTipTitle: qsTrc("playback", "Stop")
                        navigation.panel: navigationPanel
                        navigation.order: 25

                        onClicked: syncView.stopPlayback()
                    }

                    FlatButton {
                        width: 26
                        height: 26
                        minWidth: 26
                        enabled: syncView.hasAudio
                        text: "A"
                        accentButton: syncView.hasLoopFrom
                        toolTipTitle: qsTrc("playback", "Set loop start (A)")
                        toolTipDescription: qsTrc("playback", "At the playback position; click again to clear it")
                        navigation.panel: navigationPanel
                        navigation.order: 26

                        onClicked: syncView.toggleLoopFrom()
                    }

                    FlatButton {
                        width: 26
                        height: 26
                        minWidth: 26
                        enabled: syncView.hasAudio
                        text: "B"
                        accentButton: syncView.hasLoopTo
                        toolTipTitle: qsTrc("playback", "Set loop end (B)")
                        toolTipDescription: qsTrc("playback", "At the playback position; click again to clear it")
                        navigation.panel: navigationPanel
                        navigation.order: 27

                        onClicked: syncView.toggleLoopTo()
                    }

                    FlatButton {
                        width: 26
                        height: 26
                        icon: IconCode.LOOP
                        enabled: syncView.hasLoopFrom && syncView.hasLoopTo
                        accentButton: syncView.loopEnabled
                        toolTipTitle: qsTrc("playback", "Loop A-B")
                        navigation.panel: navigationPanel
                        navigation.order: 28

                        onClicked: syncView.loopEnabled = !syncView.loopEnabled
                    }
                }
            }

            RowLayout {
                id: rightGroup

                anchors.right: parent.right
                anchors.rightMargin: 8
                anchors.verticalCenter: parent.verticalCenter
                //! NOTE Same line as the transport buttons (the clock is above them)
                anchors.verticalCenterOffset: 4
                spacing: 4

                IncrementalPropertyControl {
                    Layout.preferredWidth: 90
                    enabled: syncView.hasAudio
                    currentValue: syncView.latencyMs
                    minValue: -1000
                    maxValue: 1000
                    step: 5
                    decimals: 0
                    measureUnitsSymbol: qsTrc("global", "ms")
                    navigation.panel: navigationPanel
                    navigation.order: 40
                    navigation.accessible.name: qsTrc("playback", "Reference audio latency compensation")

                    onValueEdited: function(newValue) {
                        syncView.latencyMs = newValue
                    }
                }

                SeparatorLine { orientation: Qt.Vertical; Layout.preferredHeight: 20; Layout.leftMargin: 4; Layout.rightMargin: 4 }

                FlatButton {
                    icon: IconCode.ZOOM_OUT
                    buttonType: FlatButton.IconOnly
                    transparent: true
                    toolTipTitle: qsTrc("playback", "Zoom out")
                    navigation.panel: navigationPanel
                    navigation.order: 41

                    onClicked: syncView.zoomOut()
                }

                FlatButton {
                    icon: IconCode.ZOOM_IN
                    buttonType: FlatButton.IconOnly
                    transparent: true
                    toolTipTitle: qsTrc("playback", "Zoom in")
                    navigation.panel: navigationPanel
                    navigation.order: 42

                    onClicked: syncView.zoomIn()
                }

                FlatButton {
                    icon: IconCode.FIT_PROJECT
                    buttonType: FlatButton.IconOnly
                    transparent: true
                    toolTipTitle: qsTrc("playback", "Zoom to fit")
                    navigation.panel: navigationPanel
                    navigation.order: 43

                    onClicked: syncView.zoomToFit()
                }

                SeparatorLine { orientation: Qt.Vertical; Layout.preferredHeight: 20; Layout.leftMargin: 4; Layout.rightMargin: 4 }

                FlatButton {
                    minWidth: 28
                    text: qsTrc("playback", "Stereo", "Show the left and right channels separately")
                    accentButton: syncView.showStereo && syncView.isStereoAudio
                    enabled: syncView.isStereoAudio
                    toolTipTitle: qsTrc("playback", "Show left and right channels separately")
                    navigation.panel: navigationPanel
                    navigation.order: 44

                    onClicked: syncView.showStereo = !syncView.showStereo
                }

                FlatButton {
                    minWidth: 28
                    text: qsTrc("playback", "dB", "Logarithmic amplitude scale")
                    accentButton: syncView.logarithmic
                    toolTipTitle: qsTrc("playback", "Logarithmic amplitude scale")
                    toolTipDescription: qsTrc("playback", "Alt + mouse wheel: amplitude zoom")
                    navigation.panel: navigationPanel
                    navigation.order: 45

                    onClicked: syncView.logarithmic = !syncView.logarithmic
                }

                FlatButton {
                    minWidth: 28
                    text: qsTrc("playback", "Follow", "Scroll the waveform along with the playback cursor")
                    accentButton: syncView.followPlayback
                    toolTipTitle: qsTrc("playback", "Follow playback")
                    navigation.panel: navigationPanel
                    navigation.order: 46

                    onClicked: syncView.followPlayback = !syncView.followPlayback
                }
            }
        }

        SeparatorLine {}

        Item {
            Layout.fillWidth: true
            Layout.fillHeight: true

            AudioSyncView {
                id: syncView
                anchors.fill: parent
                anchors.leftMargin: 8
                anchors.rightMargin: 8 + amplitudeSlider.width + 4
                anchors.bottomMargin: 8

                backgroundColor: ui.theme.backgroundSecondaryColor
                rulerColor: ui.theme.backgroundPrimaryColor
                waveformColor: ui.theme.accentColor
                gridColor: ui.theme.strokeColor
                textColor: ui.theme.fontPrimaryColor
                accentColor: ui.theme.accentColor
                cursorColor: "#FF6A3D"
            }

            //! NOTE The waveform's vertical zoom, along its right edge (the Alt + wheel's too)
            StyledSlider {
                id: amplitudeSlider

                x: syncView.x + syncView.width + 4
                y: syncView.y + syncView.waveformTop
                width: 14
                height: syncView.waveformHeight

                orientation: Qt.Vertical
                from: -1 // log2 of the zoom: 1/2 .. 8
                to: 3
                stepSize: 0.01
                value: Math.log2(syncView.amplitudeZoom)
                navigation.panel: navigationPanel
                navigation.order: 50
                navigation.accessible.name: qsTrc("playback", "Waveform zoom")

                onMoved: syncView.amplitudeZoom = Math.pow(2, value)

                ToolTip.visible: hovered || pressed
                ToolTip.delay: 500
                ToolTip.text: qsTrc("playback", "Waveform zoom: ×%1").arg(syncView.amplitudeZoom.toFixed(2))

                MouseArea {
                    //! NOTE Double-click: back to 1:1
                    anchors.fill: parent
                    acceptedButtons: Qt.LeftButton
                    propagateComposedEvents: true
                    onPressed: function(mouse) { mouse.accepted = false }
                    onDoubleClicked: syncView.amplitudeZoom = 1
                }
            }

            StyledTextLabel {
                anchors.right: syncView.right
                anchors.rightMargin: 8
                anchors.top: syncView.top
                anchors.topMargin: 60
                visible: syncView.speedWarning
                text: qsTrc("playback", "Playback speed isn't 100%: the reference audio doesn't follow it")
                color: "#E8A33D"
            }

            // Status: loading, errors, empty state
            Rectangle {
                anchors.centerIn: parent
                width: statusColumn.implicitWidth + 32
                height: statusColumn.implicitHeight + 24
                radius: 4
                color: ui.theme.backgroundPrimaryColor
                border.color: ui.theme.strokeColor
                border.width: ui.theme.borderWidth
                visible: statusColumn.visible

                ColumnLayout {
                    id: statusColumn
                    anchors.centerIn: parent
                    spacing: 8

                    visible: !syncView.hasAudio || syncView.audioFileMissing || syncView.decodeFailed || syncView.decoding
                             || !syncView.waveformReady

                    StyledTextLabel {
                        Layout.alignment: Qt.AlignHCenter
                        text: {
                            if (!syncView.hasAudio) {
                                return qsTrc("playback", "Drop an audio file here, or load one, to sync the score's tempo to it")
                            }
                            if (syncView.audioFileMissing) {
                                return qsTrc("playback", "The audio file can't be found: %1").arg(syncView.audioPath)
                            }
                            if (syncView.decodeFailed) {
                                return qsTrc("playback", "The audio file can't be read (unsupported or damaged file)")
                            }
                            if (syncView.decoding) {
                                return qsTrc("playback", "Decoding the audio…")
                            }
                            return qsTrc("playback", "Computing the waveform…")
                        }
                    }

                    ProgressBar {
                        Layout.alignment: Qt.AlignHCenter
                        Layout.preferredWidth: 200
                        Layout.preferredHeight: 14
                        visible: syncView.hasAudio && syncView.decoding
                        from: 0
                        to: 100
                        value: Math.round(syncView.decodeProgress * 100)
                    }

                    FlatButton {
                        Layout.alignment: Qt.AlignHCenter
                        visible: syncView.audioFileMissing || syncView.decodeFailed || !syncView.hasAudio
                        text: syncView.hasAudio ? qsTrc("playback", "Locate the file…") : qsTrc("playback", "Load reference audio…")
                        navigation.panel: navigationPanel
                        navigation.order: 11

                        onClicked: syncView.chooseAudioFile()
                    }
                }
            }

            DropArea {
                id: dropArea
                anchors.fill: parent

                onEntered: function(drag) {
                    drag.accepted = syncView.canDropUrls(drag.urls)
                }

                onDropped: function(drop) {
                    if (syncView.canDropUrls(drop.urls)) {
                        syncView.dropUrls(drop.urls)
                        drop.acceptProposedAction()
                    }
                }
            }

            Rectangle {
                anchors.fill: parent
                visible: dropArea.containsDrag
                color: "transparent"
                border.color: ui.theme.accentColor
                border.width: 2
            }
        }
    }
}
