/*
 * SPDX-License-Identifier: GPL-3.0-only
 * MuseScore-CLA-applies
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

import Muse.Ui
import Muse.UiComponents
import MuseScore.Playback

//! NOTE: a channel's name on its color (the accent color without a custom one): the Track list's channel strip, the
//! Audio sync window's channel
Rectangle {
    id: root

    required property MixerChannelItem channelItem
    //! NOTE: as in the Mixer (see MixerTitleSection): the color half transparent and framed, the text in the theme's
    //! color; otherwise the color as is, rounded, the text black or white
    property bool mixerStyle: false

    readonly property color trackColor: root.channelItem.hasCustomColor ? root.channelItem.color : ui.theme.accentColor

    height: root.mixerStyle ? 22 : 24
    radius: root.mixerStyle ? 0 : 2

    color: root.mixerStyle ? Qt.rgba(root.trackColor.r, root.trackColor.g, root.trackColor.b, 0.5) : root.trackColor
    border.color: root.trackColor
    border.width: root.mixerStyle ? 1 : 0

    StyledTextLabel {
        anchors.fill: parent
        anchors.leftMargin: 6
        anchors.rightMargin: 6

        horizontalAlignment: Text.AlignHCenter
        elide: Text.ElideRight
        font: ui.theme.bodyBoldFont
        //! NOTE: readable on light and dark track colors alike
        color: root.mixerStyle ? ui.theme.fontPrimaryColor
               : (0.299 * root.trackColor.r + 0.587 * root.trackColor.g + 0.114 * root.trackColor.b) > 0.6 ? "black" : "white"
        text: root.channelItem.title
    }
}
