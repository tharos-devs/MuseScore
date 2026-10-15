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

MixerPanelSection {
    id: root

    Item {
        id: content

        required property MixerChannelItem channelItem

        readonly property bool condensedRow: root.isCondensedRow(channelItem)

        height: fader.height
        width: root.rowWidthFor(channelItem)

        ChannelFader {
            id: fader

            anchors.horizontalCenter: parent.horizontalCenter
            //! NOTE: the fader's own visible rail/handle sit within the left portion of VolumeSlider's box (its dB
            //! ruler ticks occupy the right portion, see VolumeSlider.qml), so a mathematically-centered Row still
            //! reads as shifted right -- nudged back left in condensed view to compensate.
            anchors.horizontalCenterOffset: content.condensedRow ? -4 : 0

            channelItem: content.channelItem
            //! NOTE: Master keeps its meters' ruler regardless, since it never narrows in condensed view
            compact: content.condensedRow

            navigationRowStart: root.navigationRowStart
            accessibleName: (Boolean(root.needReadChannelName) ? content.channelItem.title + " " : "") + root.headerTitle

            onNavigateControlIndexChanged: function(index) {
                root.navigateControlIndexChanged(index)
            }
        }
    }
}
