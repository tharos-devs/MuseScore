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

    readonly property int toolbarHeight: 36

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

        // Toolbar
        RowLayout {
            Layout.fillWidth: true
            Layout.fillHeight: false
            Layout.preferredHeight: root.toolbarHeight
            Layout.maximumHeight: root.toolbarHeight
            Layout.leftMargin: 8
            Layout.rightMargin: 8
            spacing: 4

            FlatButton {
                icon: IconCode.OPEN_FILE
                buttonType: FlatButton.IconOnly
                transparent: true
                toolTipTitle: qsTrc("playback", "Load reference audio…")
                navigation.panel: navigationPanel
                navigation.order: 1

                onClicked: syncView.chooseAudioFile()
            }

            StyledTextLabel {
                Layout.maximumWidth: 260
                text: syncView.hasAudio ? syncView.audioFileName : qsTrc("playback", "No reference audio")
                elide: Text.ElideMiddle
                horizontalAlignment: Text.AlignLeft
                opacity: syncView.hasAudio ? 1 : 0.6
            }

            FlatButton {
                visible: syncView.hasAudio
                icon: IconCode.DELETE_TANK
                buttonType: FlatButton.IconOnly
                transparent: true
                toolTipTitle: qsTrc("playback", "Remove reference audio")
                navigation.panel: navigationPanel
                navigation.order: 2

                onClicked: syncView.removeAudio()
            }

            SeparatorLine { orientation: Qt.Vertical; Layout.preferredHeight: 20; Layout.leftMargin: 4; Layout.rightMargin: 4 }

            FlatButton {
                enabled: syncView.hasAudio
                minWidth: 28
                text: qsTrc("playback", "M", "Mute the reference audio")
                accentButton: syncView.muted
                toolTipTitle: qsTrc("playback", "Mute reference audio")
                navigation.panel: navigationPanel
                navigation.order: 3

                onClicked: syncView.muted = !syncView.muted
            }

            FlatButton {
                enabled: syncView.hasAudio
                minWidth: 28
                text: qsTrc("playback", "S", "Solo the reference audio")
                accentButton: syncView.solo
                toolTipTitle: qsTrc("playback", "Solo reference audio")
                navigation.panel: navigationPanel
                navigation.order: 4

                onClicked: syncView.solo = !syncView.solo
            }

            SeparatorLine { orientation: Qt.Vertical; Layout.preferredHeight: 20; Layout.leftMargin: 4; Layout.rightMargin: 4 }

            FlatButton {
                icon: IconCode.ZOOM_OUT
                buttonType: FlatButton.IconOnly
                transparent: true
                toolTipTitle: qsTrc("playback", "Zoom out")
                navigation.panel: navigationPanel
                navigation.order: 5

                onClicked: syncView.zoomOut()
            }

            FlatButton {
                icon: IconCode.ZOOM_IN
                buttonType: FlatButton.IconOnly
                transparent: true
                toolTipTitle: qsTrc("playback", "Zoom in")
                navigation.panel: navigationPanel
                navigation.order: 6

                onClicked: syncView.zoomIn()
            }

            FlatButton {
                icon: IconCode.FIT_PROJECT
                buttonType: FlatButton.IconOnly
                transparent: true
                toolTipTitle: qsTrc("playback", "Zoom to fit")
                navigation.panel: navigationPanel
                navigation.order: 7

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
                navigation.order: 8

                onClicked: syncView.showStereo = !syncView.showStereo
            }

            FlatButton {
                minWidth: 28
                text: qsTrc("playback", "dB", "Logarithmic amplitude scale")
                accentButton: syncView.logarithmic
                toolTipTitle: qsTrc("playback", "Logarithmic amplitude scale")
                toolTipDescription: qsTrc("playback", "Alt + mouse wheel: amplitude zoom")
                navigation.panel: navigationPanel
                navigation.order: 9

                onClicked: syncView.logarithmic = !syncView.logarithmic
            }

            FlatButton {
                minWidth: 28
                text: qsTrc("playback", "Follow", "Scroll the waveform along with the playback cursor")
                accentButton: syncView.followPlayback
                toolTipTitle: qsTrc("playback", "Follow playback")
                navigation.panel: navigationPanel
                navigation.order: 10

                onClicked: syncView.followPlayback = !syncView.followPlayback
            }

            Item { Layout.fillWidth: true }

            StyledTextLabel {
                text: syncView.tempoText
                horizontalAlignment: Text.AlignRight
            }

            StyledTextLabel {
                Layout.leftMargin: 12
                text: syncView.positionText
                horizontalAlignment: Text.AlignRight
            }
        }

        SeparatorLine {}

        Item {
            Layout.fillWidth: true
            Layout.fillHeight: true

            AudioSyncView {
                id: syncView
                anchors.fill: parent

                backgroundColor: ui.theme.backgroundSecondaryColor
                rulerColor: ui.theme.backgroundPrimaryColor
                waveformColor: ui.theme.accentColor
                gridColor: ui.theme.strokeColor
                textColor: ui.theme.fontPrimaryColor
                accentColor: ui.theme.accentColor
                cursorColor: "#FF6A3D"
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
