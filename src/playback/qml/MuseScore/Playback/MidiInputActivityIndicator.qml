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

import Muse.Ui
import MuseScore.Playback

//! NOTE: a small dot, faint when idle (so it can be found), green on each MIDI message received
Item {
    id: root

    implicitWidth: 16
    implicitHeight: 16

    MidiInputActivityModel {
        id: activityModel
    }

    Component.onCompleted: {
        activityModel.load()
    }

    Rectangle {
        anchors.centerIn: parent

        width: 7
        height: width
        radius: width / 2

        color: activityModel.active ? "#3FCB5A" : ui.theme.fontPrimaryColor
        opacity: activityModel.active ? 1.0 : 0.25
    }

    MouseArea {
        id: mouseArea

        anchors.fill: parent
        hoverEnabled: true
        acceptedButtons: Qt.NoButton

        onContainsMouseChanged: {
            if (containsMouse) {
                ui.tooltip.show(root, qsTrc("playback", "MIDI input activity"))
            } else {
                ui.tooltip.hide(root)
            }
        }
    }
}
