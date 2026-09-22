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

MixerPanelSection {
    id: root

    Item {
        id: content

        required property MixerChannelItem channelItem

        height: childrenRect.height
        width: root.rowWidthFor(channelItem)

        property string accessibleName: (Boolean(root.needReadChannelName) ? channelItem.title + " " : "") + root.headerTitle

        readonly property bool condensedRow: root.isCondensedRow(channelItem)

        Row {
            anchors.horizontalCenter: parent.horizontalCenter
            //! NOTE: the fader's own visible rail/handle sit within the left portion of
            //! VolumeSlider's box (its dB ruler ticks occupy the right portion, see
            //! VolumeSlider.qml), so a mathematically-centered Row still reads as
            //! shifted right -- nudged back left in condensed view to compensate.
            anchors.horizontalCenterOffset: content.condensedRow ? -4 : 0

            //! NOTE: condensed view narrows this to bring the L/R meters closer to the
            //! fader -- the fader itself keeps its dB ruler (see VolumeSlider.qml),
            //! unlike the meters' own ruler below, so the width saving has to come
            //! from spacing instead.
            spacing: content.condensedRow ? 2 : 8

            VolumeSlider {
                volumeLevel: content.channelItem.volumeLevel
                stepSize: 1.0
                enabled: !content.channelItem.hasVolumeAutomation
                opacity: enabled ? 1.0 : ui.theme.itemOpacityDisabled

                navigation.panel: content.channelItem.panel
                navigation.row: root.navigationRowStart
                navigation.accessible.name: content.accessibleName + " " + readableVolumeLevel
                navigation.onActiveChanged: {
                    if (navigation.active) {
                        root.navigateControlIndexChanged({row: navigation.row, column: navigation.column})
                    }
                }

                onVolumeLevelMoved: function(level) {
                    content.channelItem.volumeLevel = Math.round(level * 10) / 10
                }

                onDragStarted: {
                    content.channelItem.beginVolumeChange()
                }

                onDragFinished: {
                    content.channelItem.endVolumeChange()
                }

                onIncreaseRequested: {
                    content.channelItem.beginVolumeChange()
                    content.channelItem.volumeLevel += stepSize
                    content.channelItem.endVolumeChange()
                }

                onDecreaseRequested: {
                    content.channelItem.beginVolumeChange()
                    content.channelItem.volumeLevel -= stepSize
                    content.channelItem.endVolumeChange()
                }
            }

            Row {
                anchors.verticalCenter: parent.verticalCenter

                spacing: 2

                VolumePressureMeter {
                    id: leftPressure
                    currentVolumePressure: content.channelItem.leftChannelPressure
                }

                VolumePressureMeter {
                    id: rightPressure
                    currentVolumePressure: content.channelItem.rightChannelPressure
                    //! NOTE: condensed view drops the dB ruler on regular channels to
                    //! save the 20px it costs (see VolumePressureMeter.qml) -- Master
                    //! keeps it regardless, since it never narrows in condensed view.
                    showRuler: !content.condensedRow
                }
            }
        }
    }
}
