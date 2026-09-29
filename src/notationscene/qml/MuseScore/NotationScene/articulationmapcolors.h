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

#pragma once

#include <vector>

#include <QColor>

namespace mu::notation {
//! NOTE: an articulation without its own color=#RRGGBB takes the palette color of its position
//! in the map - saturated, dark enough for white text on top and colored text on a light lane
inline QColor artMapPaletteColor(size_t entryIndex)
{
    static const std::vector<QColor> PALETTE {
        QColor(0x1F, 0x6F, 0xC5), // blue
        QColor(0xD0, 0x3B, 0x3B), // red
        QColor(0x2E, 0x8B, 0x3E), // green
        QColor(0x8A, 0x4F, 0xC7), // purple
        QColor(0xD9, 0x6C, 0x06), // orange
        QColor(0x0E, 0x8C, 0x96), // teal
        QColor(0xB0, 0x3A, 0x8E), // magenta
        QColor(0x7A, 0x5A, 0x2E), // brown
    };

    return PALETTE[entryIndex % PALETTE.size()];
}
}
