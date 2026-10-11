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
        width: root.rowWidthFor(channelItem)

        property string accessibleName: (Boolean(root.needReadChannelName) ? channelItem.title + " " : "") + root.headerTitle

        readonly property bool condensedRow: root.isCondensedRow(channelItem)

        spacing: 4

        Repeater {
            id: repeater
            anchors.horizontalCenter: parent.horizontalCenter

            model: content.channelItem.auxSendItemModel

            delegate: Item {
                width: root.rowWidthFor(content.channelItem)
                height: auxSendControl.height

                required property AuxSendItem modelData
                required property int index

                AuxSendControl {
                    id: auxSendControl

                    anchors.horizontalCenter: parent.horizontalCenter
                    //! NOTE: see MixerSoundSection.qml's identical binding -- stays at
                    //! the component's own 96 default until condensed view narrows
                    //! this row past 104. In condensed view this narrows further still,
                    //! to knob(27) + spacing(8) + an 18px arrow button.
                    width: content.condensedRow ? 53 : Math.min(96, parent.width - 8)

                    compact: content.condensedRow

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
