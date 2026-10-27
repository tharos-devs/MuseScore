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
#include <functional>
#include <utility>
#include <vector>

#include <QColor>
#include <QQuickPaintedItem>
#include <QString>

#include "draw/types/geometry.h"
#include "draw/types/transform.h"

namespace mu::notation {
// A handle on the top edge of the range selection's frame, while the velocity bars or the automation curves are shown:
// dragged up or down, it changes the selected values (see NotationNoteVelocityController, NotationAutomationController)
class SelectionFrameHandle : public QQuickPaintedItem
{
    Q_OBJECT

public:
    explicit SelectionFrameHandle(QQuickItem* parent);

    static constexpr qreal SIZE_PX = 19.0; // a little larger than the square, easier to grab; also fits the label's height

    // The value label's background and text colors, legible on this score background
    static std::pair<QColor, QColor> valueLabelColors(const QColor& background);

    void setColor(const QColor& color);
    void setValueLabelColors(const QColor& background, const QColor& text);

    // Shown next to the handle while it's dragged
    void setLabel(const QString& label);

    // The square's center, in the parent's coordinates
    void setCenter(const QPointF& center);

    void paint(QPainter* painter) override;

    bool isDragging() const { return m_pressed; }

signals:
    // deltaYPx is the mouse's vertical displacement since the press, in pixels (0 on press)
    void dragged(qreal deltaYPx, bool completed);
    void dragCancelled();

protected:
    void mousePressEvent(QMouseEvent* e) override;
    void mouseMoveEvent(QMouseEvent* e) override;
    void mouseReleaseEvent(QMouseEvent* e) override;
    void mouseUngrabEvent() override;

private:
    QRectF squareRect() const;
    qreal dragWidth() const;

    QColor m_color;
    QColor m_valueLabelBgColor;
    QColor m_valueLabelTextColor;
    QString m_label;

    bool m_pressed = false;
    qreal m_pressSceneY = 0.0;
};

// The three handles on the range selection's frame: the corners of its top edge (the right one on the frame's last
// system, when it spans several) and the middle of its first system. Owned by their parent item
class SelectionFrameHandles
{
public:
    enum class Kind {
        Left,
        Middle,
        Right,
    };

    using DraggedCallback = std::function<void (Kind kind, qreal deltaYPx, bool completed)>;
    using CancelledCallback = std::function<void ()>;

    SelectionFrameHandles(QQuickItem* parent, const DraggedCallback& onDragged, const CancelledCallback& onCancelled);

    // The handles' color, and their label's for this score background
    void setColors(const QColor& color, const QColor& scoreBackground);

    // The frame's rectangle on each of its systems, in canvas coordinates; none hides the handles
    void setAreas(std::vector<muse::RectF> areas);
    void updateGeometry(const muse::draw::Transform& viewMatrix, bool visible);

    void setLabel(Kind kind, const QString& label);
    bool isDragging() const;

private:
    SelectionFrameHandle* handle(Kind kind) const { return m_handles.at(static_cast<size_t>(kind)); }

    std::array<SelectionFrameHandle*, 3> m_handles {}; // indexed by Kind
    std::vector<muse::RectF> m_areas;
    QColor m_color;
    QColor m_scoreBackground;
};
}
