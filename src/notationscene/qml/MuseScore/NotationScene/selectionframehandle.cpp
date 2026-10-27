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

#include "selectionframehandle.h"

#include <algorithm>

#include <QFontMetrics>
#include <QMouseEvent>
#include <QPainter>

using namespace mu::notation;

constexpr static qreal HANDLE_SQUARE_SIZE_PX = 9.0;

// Wider to the right while dragging, for the value label (not before: the item's whole area has the handle's cursor);
// wider still for a longer label
constexpr static qreal HANDLE_DRAG_WIDTH_PX = 60.0;

constexpr static qreal HANDLE_LABEL_FONT_PX = 11.0;
constexpr static qreal HANDLE_LABEL_GAP_PX = 4.0;
constexpr static qreal HANDLE_LABEL_PADDING_X_PX = 4.0;
constexpr static qreal HANDLE_LABEL_PADDING_Y_PX = 2.0;
constexpr static qreal HANDLE_LABEL_CORNER_RADIUS_PX = 3.0;

SelectionFrameHandle::SelectionFrameHandle(QQuickItem* parent)
    : QQuickPaintedItem(parent)
{
    setAcceptedMouseButtons(Qt::LeftButton);
    setWidth(SIZE_PX);
    setHeight(SIZE_PX);
    setCursor(Qt::ArrowCursor);
}

// The value label chip needs to stay legible against whatever the score's own background currently is (light/dark/
// high-contrast paper, or a user-customized color) - picking its colors from that background's luminance, rather than
// hardcoding per theme, keeps it correct even for a custom paper color that doesn't match either preset.
std::pair<QColor, QColor> SelectionFrameHandle::valueLabelColors(const QColor& background)
{
    const double luminance = 0.299 * background.red() + 0.587 * background.green() + 0.114 * background.blue();
    if (luminance > 128.0) {
        return { QColor(40, 40, 40, 235), QColor(255, 255, 255) };
    }
    return { QColor(235, 235, 235, 235), QColor(20, 20, 20) };
}

void SelectionFrameHandle::setColor(const QColor& color)
{
    m_color = color;
    update();
}

void SelectionFrameHandle::setValueLabelColors(const QColor& background, const QColor& text)
{
    m_valueLabelBgColor = background;
    m_valueLabelTextColor = text;
    update();
}

static QFont labelFont()
{
    QFont font;
    font.setPixelSize(static_cast<int>(HANDLE_LABEL_FONT_PX));
    return font;
}

void SelectionFrameHandle::setLabel(const QString& label)
{
    if (m_label == label) {
        return;
    }
    m_label = label;
    if (m_pressed) {
        setWidth(dragWidth());
    }
    update();
}

qreal SelectionFrameHandle::dragWidth() const
{
    const qreal textWidth = QFontMetrics(labelFont()).size(Qt::TextSingleLine, m_label).width();
    return std::max(HANDLE_DRAG_WIDTH_PX, squareRect().right() + HANDLE_LABEL_GAP_PX + textWidth + 2 * HANDLE_LABEL_PADDING_X_PX + 1.0);
}

void SelectionFrameHandle::setCenter(const QPointF& center)
{
    setX(center.x() - SIZE_PX / 2.0);
    setY(center.y() - SIZE_PX / 2.0);
}

QRectF SelectionFrameHandle::squareRect() const
{
    const qreal offset = (SIZE_PX - HANDLE_SQUARE_SIZE_PX) / 2.0;
    return QRectF(offset, offset, HANDLE_SQUARE_SIZE_PX, HANDLE_SQUARE_SIZE_PX);
}

void SelectionFrameHandle::paint(QPainter* painter)
{
    painter->setRenderHint(QPainter::Antialiasing);
    painter->setPen(Qt::NoPen);
    painter->setBrush(m_color);
    painter->drawRect(squareRect());

    if (!m_pressed || m_label.isEmpty()) {
        return;
    }

    const QFont font = labelFont();
    painter->setFont(font);

    const QSize textSize = QFontMetrics(font).size(Qt::TextSingleLine, m_label);
    const qreal chipWidth = textSize.width() + 2 * HANDLE_LABEL_PADDING_X_PX;
    const qreal chipHeight = textSize.height() + 2 * HANDLE_LABEL_PADDING_Y_PX;
    const QRectF chipRect(squareRect().right() + HANDLE_LABEL_GAP_PX, (SIZE_PX - chipHeight) / 2.0, chipWidth, chipHeight);

    painter->setBrush(m_valueLabelBgColor);
    painter->drawRoundedRect(chipRect, HANDLE_LABEL_CORNER_RADIUS_PX, HANDLE_LABEL_CORNER_RADIUS_PX);

    painter->setPen(m_valueLabelTextColor);
    painter->drawText(chipRect, Qt::AlignCenter, m_label);
}

void SelectionFrameHandle::mousePressEvent(QMouseEvent* e)
{
    m_pressed = true;
    setWidth(dragWidth());
    // In scene coordinates: the view may scroll (and this item move) while dragging
    m_pressSceneY = e->scenePosition().y();
    e->accept();
    emit dragged(0.0, false);
    update();
}

void SelectionFrameHandle::mouseMoveEvent(QMouseEvent* e)
{
    if (m_pressed) {
        emit dragged(e->scenePosition().y() - m_pressSceneY, false);
    }
}

void SelectionFrameHandle::mouseReleaseEvent(QMouseEvent* e)
{
    if (!m_pressed) {
        return;
    }
    emit dragged(e->scenePosition().y() - m_pressSceneY, true);
    m_pressed = false;
    setWidth(SIZE_PX);
    update();
}

void SelectionFrameHandle::mouseUngrabEvent()
{
    if (m_pressed) {
        m_pressed = false;
        setWidth(SIZE_PX);
        emit dragCancelled();
        update();
    }
}

SelectionFrameHandles::SelectionFrameHandles(QQuickItem* parent, const DraggedCallback& onDragged, const CancelledCallback& onCancelled)
{
    for (Kind kind : { Kind::Left, Kind::Middle, Kind::Right }) {
        SelectionFrameHandle* handle = new SelectionFrameHandle(parent);
        handle->setZ(1.0); // above the overlay's own items
        handle->setVisible(false);

        QObject::connect(handle, &SelectionFrameHandle::dragged, [onDragged, kind](qreal deltaYPx, bool completed) {
            onDragged(kind, deltaYPx, completed);
        });
        QObject::connect(handle, &SelectionFrameHandle::dragCancelled, [onCancelled]() {
            onCancelled();
        });

        m_handles[static_cast<size_t>(kind)] = handle;
    }
}

void SelectionFrameHandles::setColors(const QColor& color, const QColor& scoreBackground)
{
    if (color == m_color && scoreBackground == m_scoreBackground) {
        return;
    }
    m_color = color;
    m_scoreBackground = scoreBackground;

    const auto [labelBackground, labelText] = SelectionFrameHandle::valueLabelColors(scoreBackground);
    for (SelectionFrameHandle* handle : m_handles) {
        handle->setColor(color);
        handle->setValueLabelColors(labelBackground, labelText);
    }
}

void SelectionFrameHandles::setAreas(std::vector<muse::RectF> areas)
{
    m_areas = std::move(areas);
}

void SelectionFrameHandles::updateGeometry(const muse::draw::Transform& viewMatrix, bool visible)
{
    visible = visible && !m_areas.empty();
    for (SelectionFrameHandle* handle : m_handles) {
        handle->setVisible(visible);
    }
    if (!visible) {
        return;
    }

    const muse::RectF first = viewMatrix.map(m_areas.front());
    const muse::RectF last = viewMatrix.map(m_areas.back());
    handle(Kind::Left)->setCenter(QPointF(first.left(), first.top()));
    handle(Kind::Middle)->setCenter(QPointF(first.center().x(), first.top()));
    handle(Kind::Right)->setCenter(QPointF(last.right(), last.top()));

    // On a narrow frame, the middle handle would be under the corner ones
    handle(Kind::Middle)->setVisible(first.width() >= 3 * SelectionFrameHandle::SIZE_PX);
}

void SelectionFrameHandles::setLabel(Kind kind, const QString& label)
{
    handle(kind)->setLabel(label);
}

bool SelectionFrameHandles::isDragging() const
{
    for (const SelectionFrameHandle* handle : m_handles) {
        if (handle->isDragging()) {
            return true;
        }
    }
    return false;
}
