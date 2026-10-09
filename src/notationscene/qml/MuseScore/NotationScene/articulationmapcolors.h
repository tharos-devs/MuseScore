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

#include <algorithm>
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

//! NOTE: gradients to recolor articulations with, as dark as the palette above
inline const std::vector<std::vector<QColor> >& artMapColorGradients()
{
    static const std::vector<std::vector<QColor> > GRADIENTS {
        // neighbor hues follow each other: a wide range of colors, none muddy in between
        { QColor(0x1F, 0x4F, 0xB5), QColor(0x8A, 0x4F, 0xC7), QColor(0xB0, 0x3A, 0x8E), QColor(0xD0, 0x3B, 0x3B),
          QColor(0xD9, 0x6C, 0x06) }, // blue to orange, through purple and red
        { QColor(0x7A, 0x9A, 0x1E), QColor(0x2E, 0x8B, 0x3E), QColor(0x0E, 0x8C, 0x96), QColor(0x1F, 0x6F, 0xC5),
          QColor(0x4B, 0x2C, 0x9E) }, // lime to indigo, through green, teal and blue
        { QColor(0xB0, 0x28, 0x3A), QColor(0xD9, 0x6C, 0x06), QColor(0xA0, 0x80, 0x00), QColor(0x2E, 0x8B, 0x3E),
          QColor(0x0E, 0x8C, 0x96) }, // red to teal, through orange, gold and green
        { QColor(0x7A, 0x5A, 0x2E), QColor(0xD9, 0x6C, 0x06), QColor(0xD0, 0x3B, 0x3B), QColor(0xB0, 0x3A, 0x8E),
          QColor(0x6A, 0x3F, 0xB0) }, // brown to purple, through orange, red and magenta
        { QColor(0xD0, 0x3B, 0x3B), QColor(0xD9, 0x6C, 0x06), QColor(0xB0, 0x8A, 0x00), QColor(0x2E, 0x8B, 0x3E),
          QColor(0x0E, 0x8C, 0x96), QColor(0x1F, 0x6F, 0xC5), QColor(0x8A, 0x4F, 0xC7) }, // rainbow
    };

    return GRADIENTS;
}

//! NOTE: the color at t (0 = the first one, 1 = the last one) along a gradient
inline QColor artMapGradientColor(const std::vector<QColor>& stops, double t)
{
    if (stops.size() < 2) {
        return stops.empty() ? QColor() : stops.front();
    }

    const double position = std::clamp(t, 0.0, 1.0) * (stops.size() - 1);
    const size_t index = std::min(static_cast<size_t>(position), stops.size() - 2);
    const double f = position - index;
    const QColor& a = stops[index];
    const QColor& b = stops[index + 1];

    return QColor::fromRgbF(a.redF() + (b.redF() - a.redF()) * f, a.greenF() + (b.greenF() - a.greenF()) * f,
                            a.blueF() + (b.blueF() - a.blueF()) * f);
}
}
