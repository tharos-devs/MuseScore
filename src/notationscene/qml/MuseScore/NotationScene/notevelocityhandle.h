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

namespace mu::notation {
// A handle on the top edge of the range selection's frame, while the velocity bars are shown: dragged up or down,
// it changes the velocities of the selected notes (see NotationNoteVelocityController)
class NoteVelocityHandle : public QQuickPaintedItem
{
    Q_OBJECT

public:
    explicit NoteVelocityHandle(QQuickItem* parent);

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

    QColor m_color;
    QColor m_valueLabelBgColor;
    QColor m_valueLabelTextColor;
    QString m_label;

    bool m_pressed = false;
    qreal m_pressSceneY = 0.0;
};
}
