/*
 * SPDX-License-Identifier: GPL-3.0-only
 * MuseScore-Studio-CLA-applies
 *
 * MuseScore Studio
 * Music Composition & Notation
 *
 * Copyright (C) 2021 MuseScore Limited and others
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

MixerPanelSection {
    id: root

    Item {
        id: content

        required property MixerChannelItem channelItem

        height: childrenRect.height
        width: root.channelItemWidth

        property string accessibleName: (Boolean(root.needReadChannelName) ? channelItem.title + " " : "") + root.headerTitle

        //! NOTE: distinguishes the two Aux-bus flavors at a glance - "FX" for a regular
        //! send/return bus, "GRP" for a group/subgroup bus (see MixerChannelItem::isGroupBus).
        //! Anchored off muteSoloRow (not part of it) so it never shifts the mute/solo
        //! buttons' centered position for non-Aux channels.
        StyledTextLabel {
            id: auxTypeLabel

            visible: content.channelItem.type === MixerChannelItem.Aux

            anchors.right: muteSoloRow.left
            anchors.rightMargin: 6
            anchors.verticalCenter: muteSoloRow.verticalCenter

            text: content.channelItem.isGroupBus ? qsTrc("playback", "GRP") : qsTrc("playback", "FX")

            font.pixelSize: 10
            font.capitalization: Font.AllUppercase
            color: ui.theme.fontSecondaryColor
            opacity: 0.5
        }

        Row {
            id: muteSoloRow

            anchors.horizontalCenter: parent.horizontalCenter

            spacing: 6

            FlatToggleButton {
                id: muteButton

                height: 20
                width: 20

                icon: IconCode.MUTE
                checked: content.channelItem.muted

                // TODO: not use `enabled` for this, but present visually in some other way
                enabled: !(content.channelItem.muted && content.channelItem.forceMute)

                navigation.name: "MuteButton"
                navigation.panel: content.channelItem.panel
                navigation.row: root.navigationRowStart
                navigation.accessible.name: content.accessibleName + " " + qsTrc("playback", "Mute")
                navigation.onActiveChanged: {
                    if (navigation.active) {
                        root.navigateControlIndexChanged({row: navigation.row, column: navigation.column})
                    }
                }

                onToggled: {
                    content.channelItem.muted = !checked
                }
            }

            FlatToggleButton {
                id: soloButton

                height: 20
                width: 20

                icon: IconCode.SOLO
                checked: content.channelItem.solo

                //! NOTE: solo is only meaningful for a "group" bus (soloing it, or a track
                //! that feeds it, correctly isolates them together - see
                //! PlaybackController::updateSoloMuteStates()) - a regular send/return aux
                //! bus is left disabled here, same as before, since there's no well-defined
                //! standard behavior for "solo through a send" (see AuxSendItem/DAW research)
                enabled: (content.channelItem.type !== MixerChannelItem.Aux || content.channelItem.isGroupBus)
                         && (!content.channelItem.muted || content.channelItem.forceMute)
                visible: content.channelItem.type !== MixerChannelItem.Master && content.channelItem.type !== MixerChannelItem.Metronome

                navigation.name: "SoloButton"
                navigation.panel: content.channelItem.panel
                navigation.row: root.navigationRowStart + 1
                navigation.accessible.name: content.accessibleName + " " + qsTrc("playback", "Solo")
                navigation.onActiveChanged: {
                    if (navigation.active) {
                        root.navigateControlIndexChanged({row: navigation.row, column: navigation.column})
                    }
                }

                onToggled: {
                    content.channelItem.solo = !content.channelItem.solo
                }
            }
        }
    }
}
