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

//! NOTE: a channel's volume fader and its L/R meters (Mixer, Track list). Compact: closer meters, without their ruler
Row {
    id: root

    required property MixerChannelItem channelItem
    property bool compact: false
    //! NOTE: between the fader and the meters (with their ruler)
    property int metersSpacing: root.compact ? 2 : 8

    property NavigationPanel navigationPanel: channelItem.panel
    property int navigationRowStart: 0
    property string accessibleName: ""

    signal navigateControlIndexChanged(var index)

    //! NOTE: compact narrows this to bring the L/R meters closer to the fader -- the fader itself keeps its dB ruler
    //! (see VolumeSlider.qml), unlike the meters' own ruler below, so the width saving has to come from spacing instead
    spacing: root.metersSpacing

    VolumeSlider {
        volumeLevel: root.channelItem.volumeLevel
        stepSize: 1.0
        //! NOTE: the volume curve drawn on the score moves it then
        enabled: !root.channelItem.hasVolumeAutomation
        opacity: enabled ? 1.0 : ui.theme.itemOpacityDisabled

        navigation.panel: root.navigationPanel
        navigation.row: root.navigationRowStart
        navigation.accessible.name: root.accessibleName + " " + readableVolumeLevel
        navigation.onActiveChanged: {
            if (navigation.active) {
                root.navigateControlIndexChanged({row: navigation.row, column: navigation.column})
            }
        }

        onVolumeLevelMoved: function(level) {
            root.channelItem.volumeLevel = Math.round(level * 10) / 10
        }

        onDragStarted: {
            root.channelItem.beginVolumeChange()
        }

        onDragFinished: {
            root.channelItem.endVolumeChange()
        }

        onIncreaseRequested: {
            root.channelItem.beginVolumeChange()
            root.channelItem.volumeLevel += stepSize
            root.channelItem.endVolumeChange()
        }

        onDecreaseRequested: {
            root.channelItem.beginVolumeChange()
            root.channelItem.volumeLevel -= stepSize
            root.channelItem.endVolumeChange()
        }
    }

    ChannelMeters {
        anchors.verticalCenter: parent.verticalCenter

        leftChannelPressure: root.channelItem.leftChannelPressure
        rightChannelPressure: root.channelItem.rightChannelPressure
        //! NOTE: compact drops the dB ruler to save the 20px it costs (see VolumePressureMeter.qml)
        showRuler: !root.compact
    }
}
