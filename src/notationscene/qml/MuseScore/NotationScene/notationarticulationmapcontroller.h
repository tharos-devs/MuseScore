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

#include <map>
#include <optional>
#include <vector>

#include <QQuickItem>

#include "async/asyncable.h"
#include "context/iglobalcontext.h"
#include "modularity/ioc.h"
#include "notation/inotationarticulationmaps.h"
#include "notation/notationtypes.h"
#include "playback/iplaybackcontroller.h"

namespace mu::notation {
class ArticulationMapOverlay;

//! NOTE: shows, under every staff whose instrument has an articulation map, which articulation each
//! chord plays (see ArticulationMapOverlay), and lets the user place/move/remove articulation marks
class NotationArticulationMapController : public muse::Contextable, public muse::async::Asyncable
{
    muse::ContextInject<mu::context::IGlobalContext> globalContext = { this };
    muse::ContextInject<playback::IPlaybackController> playbackController = { this };

public:
    NotationArticulationMapController(QQuickItem* overlaysParent, const muse::modularity::ContextPtr& iocCtx);

    void init();
    void setViewMatrix(const muse::draw::Transform& viewMatrix);

private:
    //! NOTE: System pointers are compared by address only, never dereferenced (see NotationNoteVelocityController)
    struct SysStaffKey {
        const System* system = nullptr;
        staff_idx_t staffIdx = muse::nidx;

        bool operator<(const SysStaffKey& k) const
        {
            if (system != k.system) {
                return system < k.system;
            }
            return staffIdx < k.staffIdx;
        }
    };

    struct ChordEntry {
        mu::engraving::Chord* chord = nullptr; // in the current (possibly excerpt) score
        mu::engraving::Chord* masterChord = nullptr; // articulation marks and playback belong to the master score
        double canvasX = 0.0;
        muse::String entryId;
        mu::engraving::ResolvedArticulation::Source source = mu::engraving::ResolvedArticulation::Source::Default;
        std::optional<ArticulationMark> mark;
    };

    struct StaffOverlayData {
        ArticulationMapOverlay* overlay = nullptr;
        std::vector<ChordEntry> chords;
        std::vector<size_t> chipChords; // chip index -> index in chords
        mu::engraving::InstrumentTrackId trackId;
        muse::RectF bandRect;
    };

    using OverlaysMap = std::map<SysStaffKey, StaffOverlayData>;

    void onCurrentNotationChanged();
    void scheduleRebuild();
    void rebuildAllOverlays();
    void createOverlayForStaff(const System* system, staff_idx_t staffIdx, OverlaysMap& newOverlays);
    void updateOverlaysGeometry();

    void onClicked(const SysStaffKey& key, int chipIndex, qreal xN, const QPointF& globalPos);
    void onChipDragged(const SysStaffKey& key, int chipIndex, qreal deltaXN, bool completed);
    void onDragCancelled(const SysStaffKey& key, int chipIndex);

    void showMenu(const SysStaffKey& key, size_t chordIndex, const QPointF& globalPos);
    void setMarks(const std::vector<mu::engraving::Chord*>& masterChords, const std::optional<ArticulationMark>& mark,
                  const muse::TranslatableString& actionName);
    std::vector<mu::engraving::Chord*> targetMasterChords(const StaffOverlayData& data, size_t chordIndex) const;
    void auditionChord(const mu::engraving::Chord* chord);

    //! NOTE: the tick offset of a mark <-> its horizontal distance from the chord, in canvas units
    double ticksPerCanvasUnit(const mu::engraving::Chord* chord) const;
    int clampTickOffset(const StaffOverlayData& data, size_t chordIndex, int tickOffset) const;

    INotationArticulationMapsPtr articulationMaps() const;
    INotationPtr currentNotation() const;
    mu::engraving::Score* score() const;

    QQuickItem* m_overlaysParent = nullptr;
    OverlaysMap m_overlaysByStaff;
    muse::draw::Transform m_viewMatrix;
    bool m_rebuildScheduled = false;
    bool m_rebuildAfterDrag = false;
};
}
