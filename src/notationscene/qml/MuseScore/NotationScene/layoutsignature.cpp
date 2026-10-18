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

#include "layoutsignature.h"

#include "engraving/dom/score.h"
#include "engraving/dom/system.h"

using namespace mu::notation;
using namespace mu::engraving;

LayoutSignature LayoutSignature::of(const Score* score)
{
    LayoutSignature signature;
    if (!score) {
        return signature;
    }

    for (const System* system : score->systems()) {
        signature.m_systems.push_back(system);

        const PointF pos = system->canvasPos();
        signature.m_geometry.push_back(pos.x());
        signature.m_geometry.push_back(pos.y());
        signature.m_geometry.push_back(system->width());

        for (const SysStaff* staff : system->staves()) {
            signature.m_geometry.push_back(staff->show() ? staff->bbox().y() : -1.0);
            signature.m_geometry.push_back(staff->bbox().height());
        }
    }

    return signature;
}

bool LayoutSignature::refresh(const Score* score)
{
    LayoutSignature current = of(score);
    if (current == *this) {
        return false;
    }
    *this = std::move(current);
    return true;
}
