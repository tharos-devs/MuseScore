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

    headerTitle: qsTrc("playback", "Pan")

    Item {
        id: content

        required property MixerChannelItem channelItem

        height: control.height
        width: root.rowWidthFor(channelItem)

        ChannelKnobControl {
            id: control

            anchors.horizontalCenter: parent.horizontalCenter

            channelItem: content.channelItem
            isGain: false
            compact: root.isCondensedRow(content.channelItem)

            navigationRowStart: root.navigationRowStart
            accessibleName: (Boolean(root.needReadChannelName) ? content.channelItem.title + " " : "") + root.headerTitle

            onNavigateControlIndexChanged: function(index) {
                root.navigateControlIndexChanged(index)
            }
        }
    }
}
