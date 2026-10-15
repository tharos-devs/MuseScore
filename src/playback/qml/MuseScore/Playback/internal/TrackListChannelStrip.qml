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

    readonly property color trackColor: root.channelItem.hasCustomColor ? root.channelItem.color : ui.theme.accentColor

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

            Rectangle {
                id: titleBar

                width: parent.width
                height: 24
                radius: 2

                color: root.trackColor

                StyledTextLabel {
                    anchors.fill: parent
                    anchors.leftMargin: 6
                    anchors.rightMargin: 6

                    horizontalAlignment: Text.AlignHCenter
                    elide: Text.ElideRight
                    font: ui.theme.bodyBoldFont
                    //! NOTE: readable on light and dark track colors alike
                    color: (0.299 * root.trackColor.r + 0.587 * root.trackColor.g + 0.114 * root.trackColor.b) > 0.6
                           ? "black" : "white"
                    text: root.channelItem.title
                }
            }

            //! NOTE: at the bottom, ending with the right column (Mute/Solo by its last aux send)
            Column {
                id: leftColumn

                anchors.bottom: parent.bottom
                width: root.columnWidth

                spacing: root.blockSpacing

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
                        navigationPanel: root.navigationPanel
                        navigationRowStart: root.gainRow
                        accessibleName: root.channelItem.title + " " + qsTrc("playback", "Gain")
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
                        navigationRowStart: root.panRow
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
                        navigationRowStart: root.volumeRow
                        accessibleName: root.channelItem.title + " " + qsTrc("playback", "Volume")
                    }

                    ChannelFader {
                        anchors.horizontalCenter: parent.horizontalCenter
                        channelItem: root.channelItem
                        navigationPanel: root.navigationPanel
                        navigationRowStart: root.faderRow
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
                            navigationName: "ChannelStrip"
                            muteNavigationRow: root.muteRow
                            muteNavigationColumn: 0
                            soloNavigationRow: root.muteRow + 1
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
