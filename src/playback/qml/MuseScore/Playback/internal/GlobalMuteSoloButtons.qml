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

//! NOTE: the global Mute/Solo toggles (Mixer header, Track list), visually identical to the channels' ones. The
//! model provides globalMuteEngaged/globalSoloEngaged and toggleGlobalMute()/toggleGlobalSolo() (see
//! GlobalMuteSoloToggle for the remember/restore logic)
Row {
    id: root

    property var model: null

    property bool toolTipsEnabled: true

    property NavigationPanel navigationPanel: null
    property int navigationRow: 0
    property int navigationColumnStart: 0

    FlatToggleButton {
        height: 20
        width: 20

        icon: IconCode.MUTE
        checked: root.model ? root.model.globalMuteEngaged : false

        toolTipTitle: root.toolTipsEnabled ? qsTrc("playback", "Toggle mutes") : ""
        toolTipDescription: root.toolTipsEnabled ? qsTrc("playback", "Disable all active mutes; click again to restore them") : ""

        navigation.panel: root.navigationPanel
        navigation.name: "GlobalMuteButton"
        navigation.row: root.navigationRow
        navigation.column: root.navigationColumnStart
        navigation.accessible.name: qsTrc("playback", "Toggle mutes")

        onToggled: {
            root.model.toggleGlobalMute()
        }
    }

    FlatToggleButton {
        height: 20
        width: 20

        icon: IconCode.SOLO
        checked: root.model ? root.model.globalSoloEngaged : false

        toolTipTitle: root.toolTipsEnabled ? qsTrc("playback", "Toggle solos") : ""
        toolTipDescription: root.toolTipsEnabled ? qsTrc("playback", "Disable all active solos; click again to restore them") : ""

        navigation.panel: root.navigationPanel
        navigation.name: "GlobalSoloButton"
        navigation.row: root.navigationRow
        navigation.column: root.navigationColumnStart + 1
        navigation.accessible.name: qsTrc("playback", "Toggle solos")

        onToggled: {
            root.model.toggleGlobalSolo()
        }
    }
}
