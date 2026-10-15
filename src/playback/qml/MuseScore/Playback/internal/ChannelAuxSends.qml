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

//! NOTE: a channel's aux send slots (Mixer, Track list): a bus and a send level each. Compact: narrower slots
Column {
    id: root

    required property MixerChannelItem channelItem
    property bool compact: false

    property NavigationPanel navigationPanel: channelItem.panel
    property int navigationRowStart: 0
    property string accessibleName: ""

    signal navigateControlIndexChanged(var index)

    width: 108
    spacing: 4

    Repeater {
        model: root.channelItem.auxSendItemModel

        delegate: Item {
            id: sendRow

            required property AuxSendItem modelData
            required property int index

            width: root.width
            height: auxSendControl.height

            AuxSendControl {
                id: auxSendControl

                anchors.horizontalCenter: parent.horizontalCenter
                //! NOTE: stays at the component's own 96 default until the row narrows past 104. Compact narrows this
                //! further still, to knob(27) + spacing(8) + an 18px arrow button
                width: root.compact ? 53 : Math.min(96, sendRow.width - 8)

                compact: root.compact

                auxSendItemModel: sendRow.modelData

                accentColor: root.channelItem.hasCustomColor ? root.channelItem.color : ui.theme.accentColor

                navigationPanel: root.navigationPanel
                navigationRowStart: root.navigationRowStart + sendRow.index * 5 // NOTE: 5 - AuxSendControl spans rows +0..+4
                navigationName: sendRow.modelData.id
                accessibleName: root.accessibleName

                onNavigateControlIndexChanged: function(index) {
                    root.navigateControlIndexChanged(index)
                }
            }
        }
    }
}
