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
import Muse.UiComponents

//! NOTE: a channel's color in the Track list's rows, its menu (Edit color…, Reset color) on a left click, like the
//! Timeline's color strips
Rectangle {
    id: root

    property color trackColor: ui.theme.accentColor
    property bool resetEnabled: true

    signal colorSelected(color color)
    signal resetRequested()

    width: 8
    height: 24

    color: root.trackColor

    MouseArea {
        anchors.fill: parent
        anchors.margins: -2

        onClicked: function(mouse) {
            colorMenu.show(Qt.point(mouse.x, mouse.y))
        }
    }

    ContextMenuLoader {
        id: colorMenu

        items: [
            { id: "editColor", title: qsTrc("playback", "Edit color…") },
            { id: "resetColor", title: qsTrc("playback", "Reset color"), enabled: root.resetEnabled }
        ]

        onHandleMenuItem: function(itemId) {
            if (itemId === "editColor") {
                colorPickerModel.selectColor(root.trackColor, false)
            } else if (itemId === "resetColor") {
                root.resetRequested()
            }
        }
    }

    ColorPickerModel {
        id: colorPickerModel

        onColorSelected: function(color) {
            root.colorSelected(color)
        }
    }
}
