/*
 * SPDX-License-Identifier: GPL-3.0-only
 * MuseScore-Studio-CLA-applies
 *
 * MuseScore Studio
 * Music Composition & Notation
 *
 * Copyright (C) 2023 MuseScore Limited and others
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

    headerTitle: qsTrc("playback", "Aux sends")
    headerHeight: 24

    Column {
        id: content

        required property MixerChannelItem channelItem

        y: 0

        height: childrenRect.height
        width: root.channelItemWidth

        property string accessibleName: (Boolean(root.needReadChannelName) ? channelItem.title + " " : "") + root.headerTitle

        spacing: 4

        Repeater {
            id: repeater
            anchors.horizontalCenter: parent.horizontalCenter

            //! NOTE: an Aux/Group bus channel's own outgoing aux-send slots are configurable
            //! in the UI but never actually routed by the engine (Mixer::process() only
            //! consults aux sends for regular instrument tracks) - hide them here instead of
            //! showing a control that silently does nothing. The column stays present (with
            //! no items) so this channel's strip keeps the same width as every other section.
            model: content.channelItem.type === MixerChannelItem.Aux ? [] : content.channelItem.auxSendItemModel

            delegate: Item {
                width: root.channelItemWidth
                height: auxSendControl.height

                required property AuxSendItem modelData
                required property int index

                AuxSendControl {
                    id: auxSendControl

                    anchors.horizontalCenter: parent.horizontalCenter

                    auxSendItemModel: parent.modelData

                    accentColor: content.channelItem.hasCustomColor ? content.channelItem.color : ui.theme.accentColor

                    navigationPanel: content.channelItem.panel
                    navigationRowStart: root.navigationRowStart + parent.index * 5 // NOTE: 5 - AuxSendControl spans rows +0..+4
                    navigationName: parent.modelData.id
                    accessibleName: content.accessibleName

                    onNavigateControlIndexChanged: function(index) {
                        root.navigateControlIndexChanged(index)
                    }
                }
            }
        }
    }
}
