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

import QtQuick

import Muse.Ui
import Muse.UiComponents
import MuseScore.Playback

//! NOTE: a channel's Mute/Solo buttons (Mixer, Track list): their states for every channel type. What a click
//! changes is up to the caller (e.g. the Mixer applies it to the whole selection), see muteToggled/soloToggled
Row {
    id: root

    property MixerChannelItem channelItem: null

    property NavigationPanel navigationPanel: null
    property string navigationName: ""
    property int muteNavigationRow: 0
    property int muteNavigationColumn: 0
    property int soloNavigationRow: 0
    property int soloNavigationColumn: 1
    property string accessibleName: ""

    signal muteToggled(bool muted)
    signal soloToggled(bool solo)

    signal navigateControlIndexChanged(var index)

    FlatToggleButton {
        id: muteButton

        height: 20
        width: 20

        icon: IconCode.MUTE
        checked: root.channelItem.muted

        // TODO: not use `enabled` for this, but present visually in some other way
        enabled: !(root.channelItem.muted && root.channelItem.forceMute)

        navigation.name: root.navigationName + "MuteButton"
        navigation.panel: root.navigationPanel
        navigation.row: root.muteNavigationRow
        navigation.column: root.muteNavigationColumn
        navigation.accessible.name: root.accessibleName + " " + qsTrc("playback", "Mute")
        navigation.onActiveChanged: {
            if (navigation.active) {
                root.navigateControlIndexChanged({row: navigation.row, column: navigation.column})
            }
        }

        onToggled: {
            root.muteToggled(!checked)
        }
    }

    FlatToggleButton {
        id: soloButton

        height: 20
        width: 20

        icon: IconCode.SOLO
        checked: root.channelItem.solo

        //! NOTE: solo is only meaningful for a "group" bus (soloing it, or a track
        //! that feeds it, correctly isolates them together - see
        //! PlaybackController::updateSoloMuteStates()) - a regular send/return aux
        //! bus is left disabled here, same as before, since there's no well-defined
        //! standard behavior for "solo through a send" (see AuxSendItem/DAW research)
        enabled: (root.channelItem.type !== MixerChannelItem.Aux || root.channelItem.isGroupBus)
                 && (!root.channelItem.muted || root.channelItem.forceMute)
        visible: root.channelItem.type !== MixerChannelItem.Master && root.channelItem.type !== MixerChannelItem.Metronome

        navigation.name: root.navigationName + "SoloButton"
        navigation.panel: root.navigationPanel
        navigation.row: root.soloNavigationRow
        navigation.column: root.soloNavigationColumn
        navigation.accessible.name: root.accessibleName + " " + qsTrc("playback", "Solo")
        navigation.onActiveChanged: {
            if (navigation.active) {
                root.navigateControlIndexChanged({row: navigation.row, column: navigation.column})
            }
        }

        onToggled: {
            root.soloToggled(!root.channelItem.solo)
        }
    }
}
