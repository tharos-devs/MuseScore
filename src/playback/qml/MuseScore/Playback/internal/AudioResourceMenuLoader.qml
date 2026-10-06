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

import QtQuick

import Muse.UiComponents
import MuseScore.Playback

//! NOTE: an audio resource's menu (sounds, effects), as resolved by its item: open it with requestOpen()
StyledMenuLoader {
    id: root

    property AbstractAudioResourceItem resourceItemModel: null

    isSearchable: true

    function requestOpen() {
        if (root.resourceItemModel) {
            root.resourceItemModel.requestAvailableResources()
        }
    }

    onHandleMenuItem: function(itemId) {
        if (root.resourceItemModel) {
            Qt.callLater(root.resourceItemModel.handleMenuItem, itemId)
        }
    }

    Connections {
        target: root.resourceItemModel

        function onAvailableResourceListResolved(resources) {
            root.toggleOpened(resources)
        }
    }
}
