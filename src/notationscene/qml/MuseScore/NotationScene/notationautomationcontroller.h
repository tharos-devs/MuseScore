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
#include <unordered_set>
#include <vector>
#include <QPointF>
#include <QQuickItem>

#include "context/iglobalcontext.h"
#include "async/asyncable.h"
#include "ui/iuiconfiguration.h"
#include "ui/iuicontextconfiguration.h"
#include "notation/notationtypes.h"
#include "notation/inotationconfiguration.h"
#include "notation/inotationcontextconfiguration.h"
#include "engraving/automation/automationdata.h"
#include "engraving/automation/automationtypes.h"

namespace muse::uicomponents {
class PolylinePlot;
}

namespace mu::engraving {
class Staff;
struct ScoreChanges;
}

namespace mu::notation {
class NotationAutomationController : public muse::Contextable, public muse::async::Asyncable
{
    muse::ContextInject<mu::context::IGlobalContext> globalContext = { this };
    muse::ContextInject<muse::ui::IUiContextConfiguration> uiContextConfiguration = { this };
    muse::GlobalInject<muse::ui::IUiConfiguration> uiConfiguration;
    muse::GlobalInject<INotationConfiguration> notationConfiguration;
    muse::ContextInject<INotationContextConfiguration> notationContextConfiguration = { this };
    muse::GlobalInject<mu::engraving::IEngravingConfiguration> engravingConfiguration;

public:
    NotationAutomationController(QQuickItem* linesParent, const muse::modularity::ContextPtr& iocCtx);

    void init();
    void setViewMatrix(const muse::draw::Transform& viewMatrix);

private:
    // Necessary since SysStaff doesn't hold a reference to its system, which is needed
    // for calculating a SysStaff's relative position...
    struct SysStaffKey {
        const System* system = nullptr;
        const staff_idx_t staffIdx = muse::nidx;

        bool isValid() const
        {
            return system && !system->measures().empty() && staffIdx != muse::nidx;
        }

        bool operator==(const SysStaffKey& k) const
        {
            IF_ASSERT_FAILED(isValid() && k.isValid()) {
                return false;
            }
            return system == k.system && staffIdx == k.staffIdx;
        }

        //! NOTE: systems are told apart by address, never dereferenced: two systems can start with measures of the same
        //! index (e.g. multimeasure rests), which made them collide as one key; and keys can outlive their system
        //! across a relayout (see NotationNoteOffsetController's identical fix)
        bool operator<(const SysStaffKey& k) const
        {
            if (system == k.system) {
                return staffIdx < k.staffIdx;
            }
            return std::less<const System*>()(system, k.system);
        }
    };

    using PolylinesSet = std::unordered_set<muse::uicomponents::PolylinePlot*>;
    using SysStaffToPolylinesMap = std::map<const SysStaffKey, const PolylinesSet>;

    struct PointData {
        enum class PointType : unsigned char {
            UNKNOWN,
            IN,
            OUT,
            BOTH
        };
        int polylinePointIndex = -1;
        int tick = -1;
        QPointF qPointF;
        PointType pointType = PointType::UNKNOWN;
        bool outside = false; // a neighbor outside of the system: not shown, only shapes the line up to its edges
    };

    using PointsDataMap = std::map<SysStaffKey, QVector<PointData> >;

    struct TickStaffRange {
        int tickFrom = -1;
        int tickTo = -1;
        staff_idx_t staffIdxFrom = muse::nidx;
        staff_idx_t staffIdxTo = muse::nidx;
    };

    struct PendingScoreState {
        bool hasChanges = false;
        bool structural = false;
        std::optional<TickStaffRange> boundary;
    };

    SysStaffToPolylinesMap createPolylinesForSystem(const System* system);
    muse::uicomponents::PolylinePlot* createPolylineForStaff(const System* system, staff_idx_t staffIdx);
    QVector<PointData> pointsDataInStaff(const System* system, const mu::engraving::Staff* staff,
                                         const muse::RectF& sysStaffCanvasRect) const;

    mu::engraving::AutomationType currentAutomationType() const;
    //! NOTE: the key of the curve currently shown on the staff (incl. the current MIDI CC number for MidiCC)
    mu::engraving::AutomationCurveKey currentCurveKeyFor(const mu::engraving::Staff* staff) const;

    void applyPolylineStyle(muse::uicomponents::PolylinePlot* polyline, const SysStaffKey& key) const;
    void applyPolylineColors(muse::uicomponents::PolylinePlot* polyline, const SysStaffKey& key) const;
    // TODO: apply within a range? (for efficiency)
    void applyPolylineColorsUnderLine(muse::uicomponents::PolylinePlot* polyline, const SysStaffKey& key) const;
    void applyPointFlags(muse::uicomponents::PolylinePlot* polyline, const SysStaffKey& key) const;
    bool requestSegmentBend(const SysStaffKey& key, int segmentIndex, qreal value);

    //! NOTE: the in, out or both values of an existing point, as an edit sets them
    void setEditedValue(mu::engraving::AutomationPoint& point, PointData::PointType pointType, mu::engraving::real_t value) const;

    // Points inside MuseScore's range selection move together (vertically)
    bool isInRangeSelection(const mu::engraving::Staff* staff, int tick) const;
    bool isGroupSelectable(const SysStaffKey& key, const PointData& pointData, const mu::engraving::AutomationPoint* point) const;
    bool isGroupSelected(const SysStaffKey& key, const PointData& pointData) const;
    void applyGroupFlags(muse::uicomponents::PolylinePlot* polyline, const SysStaffKey& key) const;
    void refreshGroupFlags();
    void startGroupDrag();
    void previewGroupDrag(qreal delta);
    void commitGroupDrag(qreal delta);
    void cancelGroupDrag();
    bool isScoreDrivenPoint(const mu::engraving::AutomationPoint* point) const;
    int utickForTick(int tick) const;
    std::optional<int> firstPassTickForUtick(int utick) const;

    QColor inversionRelativeColor(const muse::ui::ThemeStyleKey& key) const;

    void updatePolylinesGeometry();
    void updatePolylinesColors();
    void onCurrentNotationChanged();
    void rebuildAllPolylines();
    void scheduleRebuild();

    void updateStaffPoints(const SysStaffKey& key);

    void mergePendingChanges(const mu::engraving::AutomationChanges& changes);
    void mergePendingScoreChanges(const mu::engraving::ScoreChanges& changes);
    void scheduleUpdate();
    void processPendingChanges();
    void applyAutomationChanges(const mu::engraving::AutomationChanges& changes);

    bool requestEditPoint(const PointData& oldPointData, const SysStaffKey& key, qreal x, qreal y);
    bool requestAddPoint(const SysStaffKey& key, qreal x, qreal y);
    bool requestRemovePoint(const PointData& pointData, const SysStaffKey& key);
    void editAutomationPoints(const mu::engraving::AutomationCurveKey& key, mu::engraving::AutomationPointEdits& edits);

    const mu::engraving::AutomationPoint* automationPointAt(const SysStaffKey& key, int tick) const;

    INotationAutomationPtr automation() const;
    mu::engraving::AutomationDataConstPtr automationData() const;
    mu::engraving::AutomationCurve displayedCurve(const mu::engraving::AutomationCurveKey& key) const;
    INotationPtr currentNotation() const;
    mu::engraving::Score* score() const;

    QQuickItem* m_linesParent = nullptr;
    SysStaffToPolylinesMap m_stavesToLinesMap;
    PointsDataMap m_pointsDataByStaff;
    muse::draw::Transform m_viewMatrix;
    mu::engraving::AutomationChanges m_pendingChanges;
    PendingScoreState m_pendingScoreState;
    bool m_updateScheduled = false;
    bool m_rebuildScheduled = false;

    struct GroupDrag {
        bool active = false;
        std::map<SysStaffKey, QVector<QPointF> > origins; // the points of every polyline with selected points, at start
        std::map<SysStaffKey, std::vector<int> > selected; // their selected points (indices), at start
    };
    GroupDrag m_groupDrag;
};
}
