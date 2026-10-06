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

    //! NOTE: replaces the plain header label (blank for this section) with global Mute/Solo
    //! toggles, visually identical to the per-channel ones below - see
    //! GlobalMuteSoloToggle for the remember/restore logic
    headerComponent: Component {
        Item {
            width: root.headerWidth
            height: root.headerHeight

            GlobalMuteSoloButtons {
                anchors.horizontalCenter: parent.horizontalCenter
                anchors.verticalCenter: parent.verticalCenter

                spacing: 6

                model: root.model
            }
        }
    }

    Item {
        id: content

        required property MixerChannelItem channelItem

        height: childrenRect.height
        width: root.rowWidthFor(channelItem)

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

        ChannelMuteSoloButtons {
            id: muteSoloRow

            anchors.horizontalCenter: parent.horizontalCenter

            spacing: 6

            channelItem: content.channelItem

            navigationPanel: content.channelItem.panel
            muteNavigationRow: root.navigationRowStart
            muteNavigationColumn: 0
            soloNavigationRow: root.navigationRowStart + 1
            soloNavigationColumn: 0
            accessibleName: content.accessibleName

            onNavigateControlIndexChanged: function(index) {
                root.navigateControlIndexChanged(index)
            }

            //! NOTE: applies to the whole current selection when the clicked
            //! channel is itself part of it (same "clicked-and-selected drags/
            //! affects the whole selection, otherwise just this one" idiom as
            //! the multi-select drag reorder and "add channel for selected
            //! tracks" actions elsewhere in this file's siblings)
            onMuteToggled: function(muted) {
                if (content.channelItem.selected) {
                    root.model.setMutedForSelectedChannels(muted)
                } else {
                    content.channelItem.muted = muted
                }
            }

            onSoloToggled: function(solo) {
                if (content.channelItem.selected) {
                    root.model.setSoloForSelectedChannels(solo)
                } else {
                    content.channelItem.solo = solo
                }
            }
        }
    }
}
