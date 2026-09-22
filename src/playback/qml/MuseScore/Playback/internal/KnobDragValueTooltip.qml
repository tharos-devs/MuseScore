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

//! NOTE: shows a knob's exact value while it's being dragged, for callers that hide
//! (or never had) an always-visible numeric field -- MixerGainSection.qml/
//! MixerBalanceSection.qml in the Mixer's condensed view, and AuxSendControl.qml's send
//! knob unconditionally (it never has a permanent numeric field, in any view). Meant to
//! be instantiated as a child of the knob it belongs to, so its default anchors
//! (centered above the parent) position it correctly with no further setup beyond
//! `visible` and `text`. Opaque background (same recipe as PopupContent.qml's tooltip)
//! since a plain label would be unreadable floating over neighboring knobs/meters.
Rectangle {
    id: root

    property alias text: label.text

    anchors.horizontalCenter: parent ? parent.horizontalCenter : undefined
    anchors.bottom: parent ? parent.top : undefined
    anchors.bottomMargin: 2
    z: 100

    width: label.implicitWidth + 8
    height: label.implicitHeight + 4
    radius: 4
    color: ui.theme.popupBackgroundColor
    border.color: ui.theme.strokeColor

    StyledTextLabel {
        id: label
        anchors.centerIn: parent
    }
}
