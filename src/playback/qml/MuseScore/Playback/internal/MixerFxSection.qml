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

    property bool resourcePickingActive: false

    //: FX is an abbreviation of "effects".
    headerTitle: qsTrc("playback", "Audio FX")
    headerHeight: 24

    //! NOTE: an effect dragged out of its channel's slots onto another channel's (any channel): moved there, or
    //! copied with Cmd (Ctrl) held. Each channel's slots register here, to find the one under the mouse
    property var fxDropTargets: []

    function registerFxDropTarget(target) {
        root.fxDropTargets = root.fxDropTargets.concat([ target ])
    }

    function unregisterFxDropTarget(target) {
        root.fxDropTargets = root.fxDropTargets.filter(t => t !== target)
    }

    //! NOTE: the channel's slots under a scene position (with a margin above and below them), or null. The Master's
    //! first: pinned at the right, it's above the channels scrolled under it. None under the pinned labels at the left
    //! (channels scrolled under them)
    function fxDropTargetAt(sceneX, sceneY) {
        if (root.headerVisible && root.item) {
            const posInSection = root.item.mapFromItem(null, sceneX, sceneY)
            if (posInSection.x >= root.headerPinOffsetX && posInSection.x < root.headerPinOffsetX + root.headerWidth) {
                return null
            }
        }

        let found = null

        for (const target of root.fxDropTargets) {
            const pos = target.mapFromItem(null, sceneX, sceneY)
            if (pos.x >= 0 && pos.x < target.width && pos.y >= -target.rowHeight / 2 && pos.y < target.height + target.rowHeight / 2) {
                if (target.channelItem.type === MixerChannelItem.Master) {
                    return target
                }

                found = found ?? target
            }
        }

        return found
    }

    function dropFx(fromChannel, fromIndex, toChannel, insertIndex, duplicate) {
        return root.model.dropFx(fromChannel, fromIndex, toChannel, insertIndex, duplicate)
    }

    function setDragCursor(cursorShape) {
        root.model.setDragCursor(cursorShape)
    }

    function restoreDragCursor() {
        root.model.restoreDragCursor()
    }

    ChannelFxSlots {
        id: fxSlots

        width: root.rowWidthFor(fxSlots.channelItem)

        accessibleName: (Boolean(root.needReadChannelName) ? fxSlots.channelItem.title + " " : "") + root.headerTitle
        navigationRowStart: root.navigationRowStart
        dragHost: root

        onNavigateControlIndexChanged: function(index) {
            root.navigateControlIndexChanged(index)
        }

        onResourcePickingActiveChanged: {
            root.resourcePickingActive = fxSlots.resourcePickingActive
        }
    }
}
