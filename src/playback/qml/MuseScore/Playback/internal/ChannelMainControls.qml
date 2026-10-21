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

import Muse.Ui
import Muse.UiComponents
import MuseScore.Playback

//! NOTE: a channel's main controls, one above the other: Gain, Pan, volume (field, fader with meters) and Mute/Solo
//! (the Track list's channel strip, the Audio sync window's channel)
Column {
    id: root

    required property MixerChannelItem channelItem
    //! NOTE: the titles' width
    property int controlWidth: 60
    //! NOTE: the Gain and Pan knobs' size
    property real knobRadius: 16
    //! NOTE: between the fader and its meters (see ChannelFader)
    property int faderMetersSpacing: 8
    //! NOTE: the EQ's thumbnail below Gain, untitled (the curve says what it is)
    property bool showEq: false

    property NavigationPanel navigationPanel: null
    //! NOTE: its navigation rows, from this one: Gain and Pan (knob, field), volume field, fader, Mute/Solo, EQ
    property int navigationRowStart: 0
    property string navigationName: "ChannelControls"

    //! NOTE: what a click on Mute/Solo changes is up to the panel (see ChannelMuteSoloButtons)
    signal muteToggled(bool muted)
    signal soloToggled(bool solo)

    component BlockTitle: StyledTextLabel {
        //! NOTE: centered over its control, even when the column is wider (e.g. the fader with its meters)
        anchors.horizontalCenter: parent ? parent.horizontalCenter : undefined
        width: root.controlWidth
        horizontalAlignment: Text.AlignHCenter
        color: ui.theme.fontSecondaryColor
    }

    Column {
        anchors.horizontalCenter: parent.horizontalCenter
        spacing: 4

        BlockTitle {
            text: qsTrc("playback", "Gain")
        }

        ChannelKnobControl {
            anchors.horizontalCenter: parent.horizontalCenter
            channelItem: root.channelItem
            isGain: true
            knobRadius: root.knobRadius
            navigationPanel: root.navigationPanel
            navigationRowStart: root.navigationRowStart
            accessibleName: root.channelItem.title + " " + qsTrc("playback", "Gain")
        }

        ChannelEqThumbnail {
            anchors.horizontalCenter: parent.horizontalCenter
            visible: root.showEq
            width: root.controlWidth - 12
            height: 30
            channelItem: root.channelItem
            navigationPanel: root.navigationPanel
            navigationRowStart: root.navigationRowStart + 8
            accessibleName: root.channelItem.title + " " + qsTrc("playback", "EQ")
        }
    }

    Column {
        anchors.horizontalCenter: parent.horizontalCenter
        spacing: 4

        BlockTitle {
            text: qsTrc("playback", "Pan")
        }

        ChannelKnobControl {
            anchors.horizontalCenter: parent.horizontalCenter
            channelItem: root.channelItem
            navigationPanel: root.navigationPanel
            knobRadius: root.knobRadius
            navigationRowStart: root.navigationRowStart + 2
            accessibleName: root.channelItem.title + " " + qsTrc("playback", "Pan")
        }
    }

    Column {
        anchors.horizontalCenter: parent.horizontalCenter
        spacing: 6

        BlockTitle {
            text: qsTrc("playback", "Volume")
        }

        ChannelVolumeField {
            anchors.horizontalCenter: parent.horizontalCenter
            channelItem: root.channelItem
            navigationPanel: root.navigationPanel
            navigationRowStart: root.navigationRowStart + 4
            accessibleName: root.channelItem.title + " " + qsTrc("playback", "Volume")
        }

        ChannelFader {
            anchors.horizontalCenter: parent.horizontalCenter
            channelItem: root.channelItem
            metersSpacing: root.faderMetersSpacing
            navigationPanel: root.navigationPanel
            navigationRowStart: root.navigationRowStart + 5
            accessibleName: root.channelItem.title + " " + qsTrc("playback", "Volume")
        }

        //! NOTE: closer to the fader: its meters' scale ends below its handle
        Item {
            anchors.horizontalCenter: parent.horizontalCenter
            width: muteSoloButtons.width
            height: muteSoloButtons.height - 8

            ChannelMuteSoloButtons {
                id: muteSoloButtons

                y: -8
                spacing: 6

                channelItem: root.channelItem
                navigationPanel: root.navigationPanel
                navigationName: root.navigationName
                muteNavigationRow: root.navigationRowStart + 6
                muteNavigationColumn: 0
                soloNavigationRow: root.navigationRowStart + 6 + 1
                soloNavigationColumn: 0
                accessibleName: root.channelItem.title

                onMuteToggled: function(muted) {
                    root.muteToggled(muted)
                }

                onSoloToggled: function(solo) {
                    root.soloToggled(solo)
                }
            }
        }
    }
}
