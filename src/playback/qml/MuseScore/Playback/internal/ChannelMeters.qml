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

//! NOTE: a channel's left and right meters (Mixer, Track list), the same width on screen: their gap is rounded to
//! whole physical pixels too (see VolumePressureMeter's indicatorWidth)
Row {
    id: root

    property real leftChannelPressure: -60.0
    property real rightChannelPressure: -60.0

    property real meterLength: 140
    property real meterThickness: 6
    property int overloadLength: 4
    property real gap: 2

    //! NOTE: the dB ruler, right of the right meter
    property bool showRuler: false

    spacing: leftMeter.snapped(root.meterThickness + root.gap) - leftMeter.indicatorWidth

    VolumePressureMeter {
        id: leftMeter

        meterLength: root.meterLength
        meterThickness: root.meterThickness
        overloadLength: root.overloadLength
        currentVolumePressure: root.leftChannelPressure
    }

    VolumePressureMeter {
        meterLength: root.meterLength
        meterThickness: root.meterThickness
        overloadLength: root.overloadLength
        currentVolumePressure: root.rightChannelPressure
        showRuler: root.showRuler
    }
}
