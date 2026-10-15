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

//! NOTE: a channel's volume field, in dB (Mixer, Track list)
TextInputField {
    id: root

    required property MixerChannelItem channelItem

    property NavigationPanel navigationPanel: channelItem.panel
    property int navigationRowStart: 0
    property string accessibleName: ""

    signal navigateControlIndexChanged(var index)

    height: 24
    width: 46

    //! NOTE: the volume curve drawn on the score sets it then
    enabled: !root.channelItem.hasVolumeAutomation

    textHorizontalAlignment: Qt.AlignHCenter
    textSidePadding: 0
    background.radius: 2

    navigation.name: "VolumeInputField"
    navigation.panel: root.navigationPanel
    navigation.row: root.navigationRowStart
    navigation.accessible.name: root.accessibleName + " " + currentText
    navigation.onActiveChanged: {
        if (navigation.active) {
            root.navigateControlIndexChanged({row: navigation.row, column: navigation.column})
        }
    }

    validator: DoubleInputValidator {
        top: root.channelItem.volumeLevelMax
        bottom: root.channelItem.volumeLevelMin
        decimal: 1
    }

    currentText: Math.round(root.channelItem.volumeLevel * 10) / 10

    onTextEdited: function(newTextValue) {
        if (root.channelItem.volumeLevel !== Number(newTextValue)) {
            root.channelItem.beginVolumeChange()
            root.channelItem.volumeLevel = Number(newTextValue)
            root.channelItem.endVolumeChange()
        }
    }
}
