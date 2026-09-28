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

#include "notationarticulationmaps.h"

#include "engraving/dom/masterscore.h"

#include "log.h"

using namespace mu::notation;

NotationArticulationMaps::NotationArticulationMaps(INotationUndoStackPtr undoStack)
    : m_undoStack(std::move(undoStack))
{
}

bool NotationArticulationMaps::isOverlayEnabled() const
{
    return m_isOverlayEnabled;
}

void NotationArticulationMaps::setOverlayEnabled(bool enabled)
{
    if (m_isOverlayEnabled == enabled) {
        return;
    }

    m_isOverlayEnabled = enabled;
    m_overlayEnabledChanged.notify();
}

muse::async::Notification NotationArticulationMaps::overlayEnabledChanged() const
{
    return m_overlayEnabledChanged;
}

ArticulationMapDataConstPtr NotationArticulationMaps::data() const
{
    return m_masterScore ? m_masterScore->articulationMapData() : nullptr;
}

void NotationArticulationMaps::edit(const EditArticulationMapChanges& changes, const muse::TranslatableString& actionName)
{
    IF_ASSERT_FAILED(m_masterScore && m_undoStack) {
        return;
    }

    if (changes.empty()) {
        return;
    }

    m_undoStack->transaction(actionName, [&](engraving::Transaction&) {
        m_masterScore->editArticulationMap(changes);
    });
}

void NotationArticulationMaps::setMasterScore(engraving::MasterScore* masterScore)
{
    m_masterScore = masterScore;
}
