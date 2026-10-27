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

#include "notevelocityhandle.h"

#include <QMouseEvent>
#include <QPainter>

using namespace mu::notation;

constexpr static qreal HANDLE_SQUARE_SIZE_PX = 9.0;

// A little larger than the square, easier to grab; wider to the right while dragging, for the value label (not
// before: the item's whole area has the handle's cursor)
constexpr static qreal HANDLE_ITEM_SIZE_PX = 19.0; // also fits the label's height
constexpr static qreal HANDLE_DRAG_WIDTH_PX = 60.0;

constexpr static qreal HANDLE_LABEL_FONT_PX = 11.0;
constexpr static qreal HANDLE_LABEL_GAP_PX = 4.0;
constexpr static qreal HANDLE_LABEL_PADDING_X_PX = 4.0;
constexpr static qreal HANDLE_LABEL_PADDING_Y_PX = 2.0;
constexpr static qreal HANDLE_LABEL_CORNER_RADIUS_PX = 3.0;

NoteVelocityHandle::NoteVelocityHandle(QQuickItem* parent)
    : QQuickPaintedItem(parent)
{
    setAcceptedMouseButtons(Qt::LeftButton);
    setWidth(HANDLE_ITEM_SIZE_PX);
    setHeight(HANDLE_ITEM_SIZE_PX);
    setCursor(Qt::ArrowCursor);
}

void NoteVelocityHandle::setColor(const QColor& color)
{
    m_color = color;
    update();
}

void NoteVelocityHandle::setValueLabelColors(const QColor& background, const QColor& text)
{
    m_valueLabelBgColor = background;
    m_valueLabelTextColor = text;
    update();
}

void NoteVelocityHandle::setLabel(const QString& label)
{
    if (m_label == label) {
        return;
    }
    m_label = label;
    update();
}

void NoteVelocityHandle::setCenter(const QPointF& center)
{
    setX(center.x() - HANDLE_ITEM_SIZE_PX / 2.0);
    setY(center.y() - HANDLE_ITEM_SIZE_PX / 2.0);
}

QRectF NoteVelocityHandle::squareRect() const
{
    const qreal offset = (HANDLE_ITEM_SIZE_PX - HANDLE_SQUARE_SIZE_PX) / 2.0;
    return QRectF(offset, offset, HANDLE_SQUARE_SIZE_PX, HANDLE_SQUARE_SIZE_PX);
}

void NoteVelocityHandle::paint(QPainter* painter)
{
    painter->setRenderHint(QPainter::Antialiasing);
    painter->setPen(Qt::NoPen);
    painter->setBrush(m_color);
    painter->drawRect(squareRect());

    if (!m_pressed || m_label.isEmpty()) {
        return;
    }

    QFont font = painter->font();
    font.setPixelSize(static_cast<int>(HANDLE_LABEL_FONT_PX));
    painter->setFont(font);

    const QSize textSize = QFontMetrics(font).size(Qt::TextSingleLine, m_label);
    const qreal chipWidth = textSize.width() + 2 * HANDLE_LABEL_PADDING_X_PX;
    const qreal chipHeight = textSize.height() + 2 * HANDLE_LABEL_PADDING_Y_PX;
    const QRectF chipRect(squareRect().right() + HANDLE_LABEL_GAP_PX, (HANDLE_ITEM_SIZE_PX - chipHeight) / 2.0, chipWidth, chipHeight);

    painter->setBrush(m_valueLabelBgColor);
    painter->drawRoundedRect(chipRect, HANDLE_LABEL_CORNER_RADIUS_PX, HANDLE_LABEL_CORNER_RADIUS_PX);

    painter->setPen(m_valueLabelTextColor);
    painter->drawText(chipRect, Qt::AlignCenter, m_label);
}

void NoteVelocityHandle::mousePressEvent(QMouseEvent* e)
{
    m_pressed = true;
    setWidth(HANDLE_DRAG_WIDTH_PX);
    // In scene coordinates: the view may scroll (and this item move) while dragging
    m_pressSceneY = e->scenePosition().y();
    e->accept();
    emit dragged(0.0, false);
    update();
}

void NoteVelocityHandle::mouseMoveEvent(QMouseEvent* e)
{
    if (m_pressed) {
        emit dragged(e->scenePosition().y() - m_pressSceneY, false);
    }
}

void NoteVelocityHandle::mouseReleaseEvent(QMouseEvent* e)
{
    if (!m_pressed) {
        return;
    }
    emit dragged(e->scenePosition().y() - m_pressSceneY, true);
    m_pressed = false;
    setWidth(HANDLE_ITEM_SIZE_PX);
    update();
}

void NoteVelocityHandle::mouseUngrabEvent()
{
    if (m_pressed) {
        m_pressed = false;
        setWidth(HANDLE_ITEM_SIZE_PX);
        emit dragCancelled();
        update();
    }
}
