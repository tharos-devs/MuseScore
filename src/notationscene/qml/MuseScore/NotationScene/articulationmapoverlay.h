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

#include <QColor>
#include <QQuickPaintedItem>
#include <QString>
#include <QTimer>
#include <QVector>

// A lane drawn under a staff, showing which articulation of its instrument's articulation map
// each chord plays: a chip wherever the articulation changes (or a mark/score articulation sits),
// and a thin line in the active articulation's color in between.
//
// All X coordinates are normalized [0, 1], relative to this item's own width.

namespace mu::notation {
class ArticulationMapOverlay : public QQuickPaintedItem
{
    Q_OBJECT

public:
    struct ChipData {
        enum class Kind : unsigned char {
            Mark, // a manual mark on this chord
            Alias, // a score articulation of this chord (e.g. staccato dot)
            Inherited, // a latched mark or the default, shown where it resumes
        };

        qreal xN = 0.0; // where the change is sent (chord position + the mark's fine offset)
        qreal chordXN = 0.0;
        QString label; // short, e.g. "Leg"
        QString fullName;
        QColor color;
        Kind kind = Kind::Inherited;
        bool singleChord = false; // a mark applying to its chord only
    };

    struct LineData {
        qreal fromXN = 0.0;
        qreal toXN = 0.0;
        QColor color;
    };

    explicit ArticulationMapOverlay(QQuickItem* parent);
    ~ArticulationMapOverlay() override;

    void setContent(const QVector<ChipData>& chips, const QVector<LineData>& lines);

    //! NOTE: the chords of the lane, to show which one a click would give an articulation to
    void setChordPositions(const QVector<qreal>& chordXNs);
    //! NOTE: keeps that marker on a chord (e.g. while its articulation menu is open), -1 to release it
    void setPinnedTargetX(qreal xN);
    const QVector<ChipData>& chips() const;

    //! NOTE: live drag preview, without touching the score
    void setChipPreviewX(int index, qreal xN);

    bool isDragging() const;

    void paint(QPainter* painter) override;

signals:
    void chipDragged(int chipIndex, qreal deltaXN, bool completed);
    void dragCancelled(int chipIndex);
    //! NOTE: a plain left click anywhere on the lane, chips included (it's for the note under the target marker)
    void clicked(qreal xN, const QPointF& globalPos);

protected:
    void hoverMoveEvent(QHoverEvent* e) override;
    void hoverLeaveEvent(QHoverEvent* e) override;
    void mousePressEvent(QMouseEvent* e) override;
    void mouseMoveEvent(QMouseEvent* e) override;
    void mouseReleaseEvent(QMouseEvent* e) override;
    void mouseUngrabEvent() override;

private:
    QRectF chipRectPx(const ChipData& chip) const;
    int hitTestPx(const QPointF& posPx) const;
    qreal targetChordXN(const QPointF& posPx) const;
    void updateChipTooltip();
    void hideChipTooltip();
    void drawTargetMarker(QPainter* painter, qreal xN) const;

    QVector<ChipData> m_chips;
    QVector<LineData> m_lines;
    QVector<qreal> m_chordXNs;

    qreal m_hoverTargetXN = -1.0;
    qreal m_pinnedTargetXN = -1.0;

    int m_hoveredChip = -1;
    bool m_hoveringChip = false;

    bool m_pressed = false;
    int m_activeChip = -1;
    Qt::MouseButton m_pressedButton = Qt::NoButton;
    qreal m_dragStartXPx = 0.0;
    bool m_movedPastClickThreshold = false;

    QTimer m_tooltipTimer;
    bool m_tooltipShown = false;
};
}
