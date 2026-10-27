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

#include <array>
#include <map>
#include <optional>
#include <QElapsedTimer>
#include <QPointer>
#include <QQuickItem>

#include "project/iprojectsession.h"
#include "async/asyncable.h"
#include "modularity/ioc.h"
#include "notation/inotationconfiguration.h"
#include "notation/notationtypes.h"

#include "layoutsignature.h"
#include "playback/iplaybackcontroller.h"
#include "notevelocitygeometry.h"

namespace mu::engraving {
struct ScoreChanges;
}

namespace mu::notation {
class NoteVelocityOverlay;
class NoteVelocityHandle;

class NotationNoteVelocityController : public muse::Contextable, public muse::async::Asyncable
{
    muse::ContextInject<mu::project::IProjectSession> projectSession = { this };
    muse::GlobalInject<INotationConfiguration> notationConfiguration;
    muse::ContextInject<playback::IPlaybackController> playbackController = { this };

public:
    NotationNoteVelocityController(QQuickItem* overlaysParent, const muse::modularity::ContextPtr& iocCtx);
    ~NotationNoteVelocityController();

    void init();
    void setViewMatrix(const muse::draw::Transform& viewMatrix);

private:
    // Necessary since SysStaff doesn't hold a reference to its system, which is needed
    // for calculating a SysStaff's relative position...
    struct SysStaffKey {
        const System* system = nullptr;
        staff_idx_t staffIdx = muse::nidx;

        bool isValid() const
        {
            return system && !system->measures().empty() && staffIdx != muse::nidx;
        }

        bool operator<(const SysStaffKey& k) const
        {
            // Compare the System pointer by address only - never dereference it here. This key
            // is looked up against entries left over from a previous rebuild (to reuse an
            // existing overlay item instead of recreating it), and a view mode switch
            // (Page <-> Continuous) destroys and recreates every System, so a stale key still
            // sitting in the map at that point has a dangling `system` - dereferencing it (as
            // `system->first()->index()` used to) is a use-after-free/crash.
            if (system != k.system) {
                return system < k.system;
            }
            return staffIdx < k.staffIdx;
        }
    };

    // One entry per note. Entries belonging to the same chord are kept contiguous and sorted
    // highest-pitch-first, matching NoteVelocityOverlay's expected back-to-front paint order.
    struct NoteEntry {
        mu::engraving::Note* note = nullptr;
        double leftX = 0.0;
        double rightX = 0.0;
        NoteVelocityYRange yRange;
    };

    struct NoteLocation {
        SysStaffKey key;
        int rectIndex = -1;
    };

    // The overlay item, its notes and its canvas-space band rect were previously three separate
    // maps kept in lockstep by every add/remove/clear - a single map to this struct removes the
    // risk of them silently desyncing for a staff.
    struct StaffOverlayData {
        NoteVelocityOverlay* overlay = nullptr;
        std::vector<NoteEntry> notes;
        muse::RectF bandRect;
    };

    struct VelocityChange {
        mu::engraving::Note* note = nullptr;
        int velocity = 0;
    };

    // A note found again in the score when it's needed - a Note* kept across event loop turns can be deleted meanwhile
    struct NoteRef {
        track_idx_t track = muse::nidx;
        mu::engraving::Fraction tick;
        int pitch = 0;
    };

    // The handles on the range selection's frame: left corner, middle, right corner
    enum class HandleKind {
        RampLeft,
        Shift,
        RampRight,
    };

    using OverlaysMap = std::map<SysStaffKey, StaffOverlayData>;
    using NoteLocationMap = std::map<mu::engraving::Note*, NoteLocation>;

    void rebuildAllOverlays();
    void createOverlayForStaff(const System* system, staff_idx_t staffIdx, OverlaysMap& newOverlays);
    void updateOverlaysGeometry();
    void updateSelectionHighlight();
    void applyOverlayColors(NoteVelocityOverlay* overlay) const;

    void onCurrentNotationChanged();
    void scheduleRebuild();
    void onBarDragged(const SysStaffKey& key, int rectIndex, qreal deltaYN, bool completed);
    void onDragCancelled(const SysStaffKey& key, int rectIndex);
    void onBarsPainted(const SysStaffKey& key, const QPointF& fromN, const QPointF& toN, bool completed);
    void onPaintCancelled();
    void revertPreviews(const std::vector<mu::engraving::Note*>& notes);
    void createSelectionHandles();
    void updateSelectionHandles();
    void updateHandlesGeometry();
    std::vector<mu::engraving::Note*> handleTargetNotes() const;
    void onHandleDragged(HandleKind kind, qreal deltaYPx, bool completed);
    void onHandleDragCancelled();
    // deltaCanvasY: the mouse's displacement since the press; label: the value shown next to the handle
    std::vector<VelocityChange> handleChanges(HandleKind kind, double deltaCanvasY, QString& label) const;
    // The canvas height of a note's bar from velocity 0 to 127 (negative: upwards), 0 without a bar
    double velocitySpan(const mu::engraving::Note* note) const;
    int velocityDelta(const mu::engraving::Note* note, double deltaCanvasY) const;
    // Widens [minCanvasY, maxCanvasY] to the displacements that take the note from start to the minimum/maximum
    void widenVelocityLimits(const mu::engraving::Note* note, int start, double& minCanvasY, double& maxCanvasY) const;
    bool isGestureActive() const;
    void rebuildIfDeferredByGesture();
    // Cmd+click on a bar: a small field next to it to type the note's velocity (and the other selected notes')
    void showVelocityEditor(const NoteRef& noteRef, const QPointF& globalPos);
    mu::engraving::Note* resolveNote(const NoteRef& noteRef) const;
    void closeVelocityEditor();
    std::vector<VelocityChange> withTieChains(const std::vector<VelocityChange>& changes) const;
    void commitVelocities(const std::vector<VelocityChange>& changes);
    void previewBarHeight(const NoteLocation& location, int newVelocity);
    void auditionNote(const mu::engraving::Note* note, int velocity);
    bool auditionThrottleElapsed() const;
    void markAudition(int velocity);
    void resetAuditionThrottle();

    std::vector<mu::engraving::Note*> selectedNotes() const;

    // What the dynamics-marking/hairpin context alone would produce at this note's tick, with no
    // per-note override - used both as the displayed baseline for unedited notes and as the base
    // that a VeloType::OFFSET_VAL note's percentage override applies on top of.
    int contextVelocity(const mu::engraving::Note* note) const;

    // The velocity a note effectively plays at right now: its own explicit override if it has
    // one, otherwise the dynamics-marking/hairpin level alone would produce at its tick - used as
    // the displayed baseline for unedited notes, so nudging one starts from a musically coherent
    // value instead of an arbitrary flat default.
    int displayedVelocity(const mu::engraving::Note* note) const;

    INotationNoteVelocityPtr noteVelocity() const;
    INotationPtr currentNotation() const;
    mu::engraving::Score* score() const;

    QQuickItem* m_overlaysParent = nullptr;
    OverlaysMap m_overlaysByStaff;
    LayoutSignature m_layoutSignature;
    NoteLocationMap m_noteLocations;
    muse::draw::Transform m_viewMatrix;
    bool m_rebuildScheduled = false;
    QPointer<QObject> m_velocityEditor; // a QWidget (see showVelocityEditor())
    bool m_rebuildDeferredByGesture = false; // see rebuildAllOverlays()

    // The current paint gesture (see onBarsPainted())
    bool m_painting = false;
    bool m_paintAborted = false; // the score changed meanwhile: its notes may be gone
    std::map<mu::engraving::Note*, int> m_paintedVelocities;
    struct UnheardChange {
        mu::engraving::Note* note = nullptr;
        int velocity = 0;
    };
    std::optional<UnheardChange> m_paintUnheard; // the last change, not auditioned yet (throttled)

    std::array<NoteVelocityHandle*, 3> m_handles {}; // indexed by HandleKind
    std::vector<muse::RectF> m_selectionAreas; // the range selection's frame, one rect per system, in canvas coordinates

    // The current handle gesture (see onHandleDragged()): the notes it changes, with their velocities on press
    std::map<mu::engraving::Note*, int> m_handleStartVelocities;
    bool m_handleDragging = false;
    bool m_handleAborted = false; // the score changed meanwhile: its notes may be gone

    // Avoids re-triggering the audition sound on every single mouse-move event during a drag -
    // only once per actually-distinct velocity value, and never faster than a fixed minimum
    // interval (see AUDITION_MIN_INTERVAL_MS).
    int m_lastAuditionedVelocity = -1;
    QElapsedTimer m_auditionThrottle;
};
}
