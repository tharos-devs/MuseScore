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

    //! NOTE The 26 px buttons with 4 px above and below
    readonly property int toolbarHeight: 34

    NavigationPanel {
        id: navigationPanel
        name: "AudioSyncPanel"
        section: root.navigationSection
        order: root.contentNavigationPanelOrderStart
        direction: NavigationPanel.Horizontal
    }

    ColumnLayout {
        anchors.fill: parent
        //! NOTE Up into the dock frame's 12 px margin below its tabs (DockFrame.qml), leaving 4 px: the same space
        //! above the toolbar's buttons as below them. Only this panel's (its toolbar is its first row)
        anchors.topMargin: -8
        spacing: 0

        // Toolbar: file and sync on the left, transport in the middle, view on the right
        Item {
            Layout.fillWidth: true
            //! NOTE Exactly its height: the column would give it some of the extra space otherwise
            Layout.fillHeight: false
            Layout.minimumHeight: root.toolbarHeight
            Layout.maximumHeight: root.toolbarHeight
            Layout.preferredHeight: root.toolbarHeight

            RowLayout {
                id: leftGroup

                anchors.left: parent.left
                anchors.leftMargin: 8
                anchors.verticalCenter: parent.verticalCenter
                spacing: 4

                FlatButton {
                    Layout.preferredWidth: 26
                    Layout.preferredHeight: 26
                    icon: IconCode.NEW_FILE
                    buttonType: FlatButton.IconOnly
                    transparent: true
                    enabled: syncView.hasAudio
                    toolTipTitle: qsTrc("playback", "New")
                    toolTipDescription: qsTrc("playback", "Removes the reference audio")
                    navigation.panel: navigationPanel
                    navigation.order: 0

                    onClicked: syncView.removeAudio()
                }

                RecentFilesLoadButton {
                    path: syncView.audioPath
                    dialogTitle: qsTrc("playback", "Choose reference audio")
                    filter: qsTrc("playback", "Audio files (*.wav *.wave *.aif *.aiff *.mp3 *.flac *.ogg *.oga *.opus *.m4a *.aac *.wma *.mp4 *.mov *.m4v *.mkv *.webm *.avi);;All files (*)")
                    recentFilesTitle: qsTrc("playback", "Recently opened audio files")
                    recentFiles: function() { return syncView.recentAudioFiles() }
                    buttonSize: 26
                    navigationPanel: navigationPanel
                    navigationOrderStart: 1

                    onPathChosen: function(newPath) {
                        syncView.setAudioFile(newPath)
                    }

                    onClearRecentFilesRequested: {
                        syncView.clearRecentAudioFiles()
                    }
                }

                SeparatorLine { orientation: Qt.Vertical; Layout.preferredHeight: 20; Layout.leftMargin: 4; Layout.rightMargin: 4 }

                FlatButton {
                    minWidth: 28
                    Layout.preferredHeight: 26
                    text: qsTrc("playback", "Sync", "Use audio sync: the score's tempo follows the audio sync points")
                    accentButton: syncView.audioSyncEnabled
                    toolTipTitle: qsTrc("playback", "Use audio sync")
                    toolTipDescription: qsTrc("playback", "Play the score at the tempo given by the audio sync points. Double-click: add a point on the nearest beat (Shift: anywhere), or delete a point. Drag a point to sync it to the audio. Delete: remove the selected point. Left/Right arrows: move it by 1 ms (Shift: 10 ms).")
                    navigation.panel: navigationPanel
                    navigation.order: 6

                    onClicked: syncView.audioSyncEnabled = !syncView.audioSyncEnabled
                }

                PopupButton {
                    minWidth: 28
                    Layout.preferredHeight: 26
                    enabled: syncView.audioSyncEnabled && syncView.waveformReady && !syncView.detecting
                    text: qsTrc("playback", "Detect…", "Detect audio sync points automatically")
                    toolTipTitle: qsTrc("playback", "Detect sync points")
                    toolTipDescription: qsTrc("playback", "Detects the beats from the score's start: the first sync point if you moved it, else where the music starts (when ignoring the silence at the beginning).")
                    navigation.panel: navigationPanel
                    navigation.order: 7

                    popupComponent: AudioSyncDetectionPopup {
                        onDetectRequested: function(params) {
                            syncView.detectPoints(params)
                        }
                    }
                }

                FlatButton {
                    Layout.preferredWidth: 26
                    Layout.preferredHeight: 26
                    icon: IconCode.ERASER
                    buttonType: FlatButton.IconOnly
                    transparent: true
                    enabled: syncView.audioSyncEnabled && syncView.hasSyncPoints && !syncView.detecting
                    toolTipTitle: qsTrc("playback", "Clear sync points")
                    toolTipDescription: qsTrc("playback", "Deletes all the sync points but the score's start (and the locked ones)")
                    navigation.panel: navigationPanel
                    navigation.order: 8

                    onClicked: syncView.clearSyncPoints()
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
            //! NOTE Centered between the left and right groups (not in the toolbar: they aren't as wide, so the gaps on
            //! its sides wouldn't be either)
            Item {
                id: transportGroup

                anchors.fill: parent

                Row {
                    id: transportButtons

                    x: {
                        const freeLeft = leftGroup.x + leftGroup.width
                        const freeWidth = rightGroup.x - freeLeft
                        return Math.round(freeLeft + Math.max(0, (freeWidth - width) / 2))
                    }
                    anchors.verticalCenter: parent.verticalCenter
                    spacing: 4

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
                        icon: IconCode.REPEAT_START
                        enabled: syncView.hasLoopFrom
                        toolTipTitle: qsTrc("playback", "Go to loop start (A)")
                        navigation.panel: navigationPanel
                        navigation.order: 23

                        onClicked: syncView.goToLoopStart()
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

                    //! NOTE Playback position and duration on the left, measure.beat and tempo on the right; fixed-width
                    //! digits, so nothing moves while it runs
                    Item {
                        width: clockGrid.implicitWidth + 16
                        height: 26

                        GridLayout {
                            id: clockGrid

                            anchors.centerIn: parent
                            columns: 2
                            columnSpacing: 12
                            rowSpacing: -2

                            StyledTextLabel {
                                Layout.alignment: Qt.AlignHCenter
                                text: syncView.timeText
                                font.pixelSize: 14
                                font.features: { "tnum": 1 }
                            }

                            StyledTextLabel {
                                Layout.alignment: Qt.AlignHCenter
                                Layout.minimumWidth: 36
                                text: syncView.positionText
                                font.pixelSize: 14
                                font.features: { "tnum": 1 }
                            }

                            StyledTextLabel {
                                Layout.alignment: Qt.AlignHCenter
                                text: syncView.durationText
                                font.pixelSize: 9
                                font.features: { "tnum": 1 }
                                opacity: 0.75
                            }

                            StyledTextLabel {
                                Layout.alignment: Qt.AlignHCenter
                                text: syncView.tempoText
                                font.pixelSize: 9
                                font.features: { "tnum": 1 }
                                opacity: 0.75
                            }
                        }
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

                    FlatButton {
                        width: 26
                        height: 26
                        icon: IconCode.PAN_SCORE
                        accentButton: syncView.followPlayback
                        toolTipTitle: qsTrc("playback", "Follow playback")
                        toolTipDescription: qsTrc("playback", "Scroll the waveform along with the playback cursor")
                        navigation.panel: navigationPanel
                        navigation.order: 29

                        onClicked: syncView.followPlayback = !syncView.followPlayback
                    }

                    //! NOTE The reference audio transposed by semitones (its duration kept): applied when released,
                    //! a transposed copy being made first (a few seconds)
                    ToolBarValueSlider {
                        id: pitchSlider

                        anchors.verticalCenter: parent.verticalCenter
                        leftPadding: 8
                        enabled: syncView.hasAudio
                        title: qsTrc("playback", "Pitch")
                        from: -12
                        to: 12
                        value: syncView.pitchSemitones
                        defaultValue: 0
                        busy: syncView.pitchShifting
                        //: Semitones, e.g. "+3 st"
                        valueText: {
                            const st = Math.round(currentValue)
                            return (st > 0 ? "+" : "") + st + " " + qsTrc("playback", "st")
                        }
                        navigationPanel: navigationPanel
                        navigationOrder: 30

                        onValueEdited: function(newValue) {
                            syncView.pitchSemitones = Math.round(newValue)
                        }
                    }

                    //! NOTE The score's playback speed (the playback toolbar's one): the reference audio follows it,
                    //! its pitch kept (a stretched copy being made first), the sync kept
                    ToolBarValueSlider {
                        anchors.verticalCenter: parent.verticalCenter
                        enabled: syncView.hasAudio
                        title: qsTrc("playback", "Speed")
                        from: 50
                        to: 150
                        stepSize: 5
                        value: Math.round(syncView.speed * 100)
                        defaultValue: 100
                        busy: syncView.pitchShifting
                        valueText: Math.round(currentValue) + "%"
                        navigationPanel: navigationPanel
                        navigationOrder: 31

                        onValueEdited: function(newValue) {
                            syncView.speed = Math.round(newValue) / 100
                        }
                    }
                }
            }

            RowLayout {
                id: rightGroup

                anchors.right: parent.right
                anchors.rightMargin: 8
                anchors.verticalCenter: parent.verticalCenter
                spacing: 4

                FlatButton {
                    icon: IconCode.ZOOM_OUT
                    buttonType: FlatButton.IconOnly
                    transparent: true
                    toolTipTitle: qsTrc("playback", "Zoom out")
                    navigation.panel: navigationPanel
                    navigation.order: 41

                    onClicked: syncView.zoomOut()
                }

                //! NOTE In steps of 5% of the current zoom (its range spans several orders of magnitude): step 0 is
                //! 100%, a double-click back to it
                ToolBarValueSlider {
                    readonly property real stepFactor: Math.log(1.05)

                    Layout.alignment: Qt.AlignVCenter
                    sliderWidth: 64
                    live: true
                    from: Math.ceil(Math.log(syncView.minZoomPercent / 100) / stepFactor)
                    to: Math.floor(Math.log(syncView.maxZoomPercent / 100) / stepFactor)
                    value: Math.round(Math.log(syncView.zoomPercent / 100) / stepFactor)
                    defaultValue: 0
                    navigationPanel: navigationPanel
                    navigationOrder: 42

                    onValueEdited: function(newValue) {
                        syncView.zoomPercent = 100 * Math.exp(Math.round(newValue) * stepFactor)
                    }
                }

                FlatButton {
                    icon: IconCode.ZOOM_IN
                    buttonType: FlatButton.IconOnly
                    transparent: true
                    toolTipTitle: qsTrc("playback", "Zoom in")
                    navigation.panel: navigationPanel
                    navigation.order: 43

                    onClicked: syncView.zoomIn()
                }

                FlatButton {
                    icon: IconCode.FIT_PROJECT
                    buttonType: FlatButton.IconOnly
                    transparent: true
                    toolTipTitle: qsTrc("playback", "Zoom to fit")
                    navigation.panel: navigationPanel
                    navigation.order: 44

                    onClicked: syncView.zoomToFit()
                }
            }
        }

        SeparatorLine {}

        Item {
            Layout.fillWidth: true
            Layout.fillHeight: true

            //! NOTE The reference audio's channel, as in the Mixer (shared with it: its changes show in both)
            AudioSyncChannelModel {
                id: channelModel

                Component.onCompleted: channelModel.load()
            }

            Item {
                id: channelSidebar

                x: 8
                width: 72
                anchors.top: parent.top
                anchors.bottom: parent.bottom
                anchors.bottomMargin: 8

                Rectangle {
                    anchors.fill: parent
                    color: ui.theme.backgroundSecondaryColor
                    radius: 3
                }

                //! NOTE Made with the reference audio's channel only (its controls need one)
                Loader {
                    anchors.fill: parent
                    active: Boolean(channelModel.channelItem)

                    sourceComponent: Item {
                        //! NOTE At the bottom, as in a Mixer channel: its name on its color
                        ChannelNameBar {
                            id: channelNameBar

                            anchors.left: parent.left
                            anchors.right: parent.right
                            anchors.bottom: parent.bottom
                            anchors.margins: 4
                            channelItem: channelModel.channelItem
                            mixerStyle: true
                        }

                        StyledFlickable {
                            id: channelFlickable

                            anchors.left: parent.left
                            anchors.right: parent.right
                            anchors.top: parent.top
                            anchors.bottom: channelNameBar.top
                            clip: true
                            contentWidth: width
                            contentHeight: Math.max(height, channelControlsArea.height + 16)

                            //! NOTE At 90%: the controls of a Mixer channel, a bit smaller in this side bar. At the
                            //! bottom, by the channel's name, when there's more room than they need
                            Item {
                                id: channelControlsArea

                                y: Math.max(8, channelFlickable.height - height - 8)
                                width: parent.width
                                height: channelColumn.height * channelColumn.scale

                                Column {
                                    id: channelColumn

                                    anchors.horizontalCenter: parent.horizontalCenter
                                    scale: 0.9
                                    transformOrigin: Item.Top
                                    spacing: 10

                                    ChannelMainControls {
                                        anchors.horizontalCenter: parent.horizontalCenter
                                        channelItem: channelModel.channelItem
                                        controlWidth: channelSidebar.width
                                        knobRadius: 12
                                        faderMetersSpacing: 2
                                        showEq: true
                                        spacing: 10
                                        navigationPanel: navigationPanel
                                        navigationRowStart: 0
                                        navigationName: "AudioSyncChannel"

                                        onMuteToggled: function(muted) {
                                            channelModel.channelItem.muted = muted
                                        }

                                        onSoloToggled: function(solo) {
                                            channelModel.channelItem.setUserSolo(solo)
                                        }
                                    }

                                    Row {
                                        anchors.horizontalCenter: parent.horizontalCenter
                                        spacing: 6

                                        FlatButton {
                                            width: 26
                                            height: 26
                                            icon: syncView.showStereo && syncView.isStereoAudio ? IconCode.TWO_CIRCLES : IconCode.CIRCLE
                                            enabled: syncView.isStereoAudio
                                            toolTipTitle: syncView.showStereo && syncView.isStereoAudio
                                                          ? qsTrc("playback", "Stereo waveform") : qsTrc("playback", "Mono waveform")
                                            toolTipDescription: qsTrc("playback", "Show the left and right channels separately, or mixed")
                                            navigation.panel: navigationPanel
                                            navigation.order: 19

                                            onClicked: syncView.showStereo = !syncView.showStereo
                                        }

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
                                    }
                                }
                            }
                        }
                    }
                }
            }

            AudioSyncView {
                id: syncView
                anchors.fill: parent
                anchors.leftMargin: channelSidebar.x + channelSidebar.width + 8
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

                //! NOTE A double-click (two presses close together) back to 1:1, as the pitch slider
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
                        syncView.amplitudeZoom = 1
                        value = Qt.binding(function() { return Math.log2(syncView.amplitudeZoom) })
                    }
                }
            }

            //! NOTE What the last detection found, shown for a while
            StyledTextLabel {
                id: detectionSummaryLabel

                anchors.left: syncView.left
                anchors.leftMargin: 8
                anchors.top: syncView.top
                anchors.topMargin: syncView.waveformTop + 14
                visible: false
                text: syncView.detectionSummary

                onTextChanged: {
                    visible = text !== ""
                    if (visible) {
                        detectionSummaryTimer.restart()
                    }
                }

                Timer {
                    id: detectionSummaryTimer
                    interval: 10000
                    onTriggered: detectionSummaryLabel.visible = false
                }
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
                //! NOTE On itself, not from its content: a hidden item's children are hidden too, so it could never
                //! show again
                visible: !syncView.hasAudio || syncView.audioFileMissing || syncView.decodeFailed || syncView.decoding
                         || !syncView.waveformReady || syncView.pitchShifting

                ColumnLayout {
                    id: statusColumn
                    anchors.centerIn: parent
                    spacing: 8

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
                            if (!syncView.waveformReady) {
                                return qsTrc("playback", "Computing the waveform…")
                            }
                            return qsTrc("playback", "Preparing the audio (pitch, speed)…")
                        }
                    }

                    ProgressBar {
                        Layout.alignment: Qt.AlignHCenter
                        Layout.preferredWidth: 200
                        Layout.preferredHeight: 14
                        visible: syncView.hasAudio && (syncView.decoding || syncView.pitchShifting)
                        from: 0
                        to: 100
                        value: Math.round((syncView.decoding ? syncView.decodeProgress : syncView.pitchShiftProgress) * 100)
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
