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

#include <set>

#include "../inotationarticulationmaps.h"
#include "../inotationundostack.h"

namespace mu::engraving {
class MasterScore;
}

namespace mu::notation {
class NotationArticulationMaps : public INotationArticulationMaps
{
public:
    explicit NotationArticulationMaps(INotationUndoStackPtr undoStack);

    bool isOverlayEnabled() const override;
    void setOverlayEnabled(bool enabled) override;
    muse::async::Notification overlayEnabledChanged() const override;

    bool isEditorOpened(const engraving::InstrumentTrackId& trackId) const override;
    void setEditorOpened(const engraving::InstrumentTrackId& trackId, bool opened) override;
    muse::async::Notification editorsOpenedChanged() const override;

    ArticulationMapDataConstPtr data() const override;
    void edit(const EditArticulationMapChanges& changes, const muse::TranslatableString& actionName) override;

    void setMasterScore(engraving::MasterScore* masterScore);

private:
    INotationUndoStackPtr m_undoStack;
    engraving::MasterScore* m_masterScore = nullptr;
    bool m_isOverlayEnabled = false;
    muse::async::Notification m_overlayEnabledChanged;
    std::set<engraving::InstrumentTrackId> m_openedEditors;
    muse::async::Notification m_editorsOpenedChanged;
};
}
