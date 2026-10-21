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

//! NOTE: a channel of the Track list with the Mixer's controls (see TrackListModel::stripChannelItem()), below its name
//! on its color, in two columns of the same width: Gain, Pan, volume and Mute/Solo; then EQ and every Audio FX and Aux
//! send slot. Scrolled when its area is lower than it (naturalHeight)
Item {
    id: root

    required property MixerChannelItem channelItem
    //! NOTE: not selected: the channel shown last (or the first one), its name bar dimmed
    property bool channelSelected: true
    property NavigationPanel navigationPanel: null

    //! NOTE: the controls' navigation rows, top to bottom: Gain and Pan (knob, field), volume field, fader,
    //! Mute/Solo, EQ, then the slots (AudioResourceControl: 3 rows, AuxSendControl: 5)
    readonly property int gainRow: 0
    readonly property int panRow: 2
    readonly property int volumeRow: 4
    readonly property int faderRow: 5
    readonly property int muteRow: 6
    readonly property int eqRow: 8
    readonly property int fxRow: 9
    readonly property int auxSendsRow: root.fxRow + 3 * 5

    //! NOTE: what a click on Mute/Solo changes is up to the Track list (the selection, see ChannelMuteSoloButtons)
    signal muteToggled(bool muted)
    signal soloToggled(bool solo)

    readonly property int margin: 8
    readonly property int bottomMargin: 12
    readonly property int columnPadding: 12
    readonly property int blockSpacing: 10
    //! NOTE: 2: still a visible line once the strip is zoomed out (a 1 px one may fall between two screen pixels)
    readonly property int separatorWidth: 2
    readonly property int columnWidth: Math.floor((root.width - 2 * root.margin - root.separatorWidth) / 2)
    readonly property int controlWidth: root.columnWidth - 2 * root.columnPadding
    //! NOTE: like an effect slot (see AudioResourceControl's width in ChannelFxSlots)
    readonly property int slotWidth: Math.min(96, root.controlWidth - 8)

    readonly property real naturalHeight: content.height + root.margin + root.bottomMargin

    component BlockTitle: StyledTextLabel {
        width: root.controlWidth
        horizontalAlignment: Text.AlignHCenter
        color: ui.theme.fontSecondaryColor
    }

    StyledFlickable {
        id: flickable

        anchors.fill: parent

        contentWidth: width
        contentHeight: root.naturalHeight

        Item {
            id: content

            x: root.margin
            y: root.margin
            width: root.width - 2 * root.margin
            height: titleBar.height + root.margin + Math.max(leftColumn.height, rightColumn.height)

            ChannelNameBar {
                id: titleBar

                width: parent.width
                channelItem: root.channelItem
                opacity: root.channelSelected ? 1.0 : 0.5
            }

            //! NOTE: at the bottom, ending with the right column (Mute/Solo by its last aux send)
            Column {
                id: leftColumn

                anchors.bottom: parent.bottom
                width: root.columnWidth

                spacing: root.blockSpacing

                ChannelMainControls {
                    anchors.horizontalCenter: parent.horizontalCenter
                    channelItem: root.channelItem
                    controlWidth: root.controlWidth
                    spacing: root.blockSpacing
                    navigationPanel: root.navigationPanel
                    navigationRowStart: root.gainRow
                    navigationName: "ChannelStrip"

                    onMuteToggled: function(muted) {
                        root.muteToggled(muted)
                    }

                    onSoloToggled: function(solo) {
                        root.soloToggled(solo)
                    }
                }
            }

            //! NOTE: between the columns only (SeparatorLine would span the title too)
            Rectangle {
                anchors.top: titleBar.bottom
                anchors.topMargin: root.margin
                anchors.bottom: parent.bottom
                anchors.left: leftColumn.right
                width: root.separatorWidth
                color: ui.theme.strokeColor
            }

            Column {
                id: rightColumn

                anchors.bottom: parent.bottom
                anchors.left: leftColumn.right
                anchors.leftMargin: root.separatorWidth
                width: root.columnWidth

                spacing: 6

                Column {
                    anchors.horizontalCenter: parent.horizontalCenter
                    spacing: 4

                    BlockTitle {
                        text: qsTrc("playback", "EQ")
                    }

                    ChannelEqThumbnail {
                        anchors.horizontalCenter: parent.horizontalCenter
                        width: root.slotWidth
                        channelItem: root.channelItem
                        navigationPanel: root.navigationPanel
                        navigationRowStart: root.eqRow
                        //! NOTE: in the column of the strip's other controls (see ChannelMainControls)
                        navigationColumn: -1
                        accessibleName: root.channelItem.title + " " + qsTrc("playback", "EQ")
                    }
                }

                Column {
                    anchors.horizontalCenter: parent.horizontalCenter
                    spacing: 4

                    BlockTitle {
                        //: FX is an abbreviation of "effects".
                        text: qsTrc("playback", "Audio FX")
                    }

                    ChannelFxSlots {
                        width: root.controlWidth
                        channelItem: root.channelItem
                        navigationPanel: root.navigationPanel
                        navigationRowStart: root.fxRow
                        accessibleName: root.channelItem.title + " " + qsTrc("playback", "Audio FX")
                    }
                }

                Column {
                    anchors.horizontalCenter: parent.horizontalCenter
                    spacing: 4

                    BlockTitle {
                        text: qsTrc("playback", "Aux sends")
                    }

                    ChannelAuxSends {
                        width: root.controlWidth
                        channelItem: root.channelItem
                        navigationPanel: root.navigationPanel
                        navigationRowStart: root.auxSendsRow
                        accessibleName: root.channelItem.title + " " + qsTrc("playback", "Aux sends")
                    }
                }
            }
        }
    }
}
