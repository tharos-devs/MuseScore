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

    property bool resourcePickingActive: false

    headerTitle: qsTrc("playback", "Sound")

    Item {
        id: content

        required property MixerChannelItem channelItem

        height: resourceControl.height
        width: root.rowWidthFor(channelItem)

        property string accessibleName: (Boolean(root.needReadChannelName) ? channelItem.title + " " : "") + root.headerTitle

        visible: !channelItem.outputOnly

        AudioResourceControl {
            id: resourceControl

            anchors.horizontalCenter: parent.horizontalCenter
            height: 26
            //! NOTE: defaults to the component's own 96 in both the normal (108-wide)
            //! and Master cases (content.width - 8 = 100/100, min still 96) -- only
            //! actually shrinks the control below 96 when condensed view narrows
            //! content.width past that.
            width: Math.min(96, content.width - 8)

            supportsByPassing: false
            resourceItemModel: content.channelItem.inputResourceItem ?? null

            accentColor: content.channelItem.hasCustomColor ? content.channelItem.color : ui.theme.accentColor

            navigationPanel: content.channelItem.panel
            navigationRowStart: root.navigationRowStart
            accessibleName: content.accessibleName

            onTitleClicked: {
                if (content.channelItem.inputResourceItem) {
                    content.channelItem.inputResourceItem.requestToLaunchNativeEditorView()
                }
            }

            onNavigateControlIndexChanged: function(index) {
                root.navigateControlIndexChanged(index)
            }

            onResourcePickingActiveChanged: {
                root.resourcePickingActive = resourceControl.resourcePickingActive
            }
        }
    }
}
