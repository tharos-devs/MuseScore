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

#include "articulationmapoverlay.h"

#include <algorithm>
#include <cmath>

#include <QFontMetricsF>
#include <QHoverEvent>
#include <QMouseEvent>
#include <QPainter>

using namespace mu::notation;

constexpr static qreal ARTMAP_CLICK_MOVE_THRESHOLD_PX = 3.0;
constexpr static qreal ARTMAP_CHIP_V_MARGIN_PX = 2.0;
constexpr static qreal ARTMAP_CHIP_PADDING_X_PX = 4.0;
constexpr static qreal ARTMAP_CHIP_MIN_WIDTH_PX = 6.0;
constexpr static qreal ARTMAP_CHIP_GAP_PX = 2.0;
constexpr static qreal ARTMAP_CHIP_CORNER_RADIUS_PX = 3.0;
constexpr static qreal ARTMAP_MIN_FONT_PX = 9.0;
constexpr static qreal ARTMAP_MAX_FONT_PX = 13.0;
constexpr static qreal ARTMAP_LINE_WIDTH_PX = 2.0;

static QColor artMapTextColorFor(const QColor& fill)
{
    const double luminance = 0.299 * fill.red() + 0.587 * fill.green() + 0.114 * fill.blue();
    return luminance > 150.0 ? QColor(20, 20, 20) : QColor(255, 255, 255);
}

ArticulationMapOverlay::ArticulationMapOverlay(QQuickItem* parent)
    : QQuickPaintedItem(parent)
{
    // Left button only: a right click must reach MuseScore's own context menu underneath
    setAcceptedMouseButtons(Qt::LeftButton);
    setAcceptHoverEvents(true);
}

void ArticulationMapOverlay::setContent(const QVector<ChipData>& chips, const QVector<LineData>& lines)
{
    m_chips = chips;
    m_lines = lines;
    m_hoveredChip = -1;
    update();
}

const QVector<ArticulationMapOverlay::ChipData>& ArticulationMapOverlay::chips() const
{
    return m_chips;
}

void ArticulationMapOverlay::setChipPreviewX(int index, qreal xN)
{
    if (index < 0 || index >= m_chips.size()) {
        return;
    }

    m_chips[index].xN = xN;
    update();
}

void ArticulationMapOverlay::setColors(const QColor& background, const QColor& text)
{
    m_backgroundColor = background;
    m_textColor = text;
    update();
}

bool ArticulationMapOverlay::isDragging() const
{
    return m_pressed && m_movedPastClickThreshold;
}

//! NOTE: a chip never spans over the next one - on consecutive short notes, chips shrink (down to a
//! colored tick) instead of colliding; the hovered chip is the exception, shown in full on top
QRectF ArticulationMapOverlay::chipRectPx(const ChipData& chip) const
{
    const int index = static_cast<int>(&chip - m_chips.constData());

    QFont font;
    font.setPixelSize(static_cast<int>(std::clamp(height() * 0.55, ARTMAP_MIN_FONT_PX, ARTMAP_MAX_FONT_PX)));
    font.setBold(true);
    const QFontMetricsF metrics(font);

    const bool hovered = index == m_hoveredChip;
    const QString& text = hovered ? chip.fullName : chip.label;
    qreal chipWidth = std::max(ARTMAP_CHIP_MIN_WIDTH_PX, metrics.horizontalAdvance(text) + 2 * ARTMAP_CHIP_PADDING_X_PX);

    const qreal leftPx = chip.xN * width();

    if (!hovered) {
        qreal nextLeftPx = width();
        for (const ChipData& other : m_chips) {
            const qreal otherLeftPx = other.xN * width();
            if (otherLeftPx > leftPx + 0.5) {
                nextLeftPx = std::min(nextLeftPx, otherLeftPx);
            }
        }
        chipWidth = std::max(ARTMAP_CHIP_MIN_WIDTH_PX, std::min(chipWidth, nextLeftPx - leftPx - ARTMAP_CHIP_GAP_PX));
    }

    return QRectF(leftPx, ARTMAP_CHIP_V_MARGIN_PX, chipWidth, std::max(1.0, height() - 2 * ARTMAP_CHIP_V_MARGIN_PX));
}

void ArticulationMapOverlay::paint(QPainter* painter)
{
    painter->setRenderHint(QPainter::Antialiasing);

    QColor borderColor = m_textColor;
    borderColor.setAlpha(60);
    painter->setPen(QPen(borderColor, 1.0));
    painter->setBrush(m_backgroundColor);
    painter->drawRoundedRect(QRectF(0.5, 0.5, width() - 1.0, height() - 1.0), ARTMAP_CHIP_CORNER_RADIUS_PX, ARTMAP_CHIP_CORNER_RADIUS_PX);

    const qreal midY = height() / 2.0;
    for (const LineData& line : m_lines) {
        QColor color = line.color;
        color.setAlpha(200);
        painter->setPen(QPen(color, ARTMAP_LINE_WIDTH_PX, Qt::SolidLine, Qt::FlatCap));
        painter->drawLine(QPointF(line.fromXN * width(), midY), QPointF(line.toXN * width(), midY));
    }

    QFont font = painter->font();
    font.setPixelSize(static_cast<int>(std::clamp(height() * 0.55, ARTMAP_MIN_FONT_PX, ARTMAP_MAX_FONT_PX)));
    font.setBold(true);
    painter->setFont(font);
    const QFontMetricsF metrics(font);

    const auto drawChip = [&](int index) {
        const ChipData& chip = m_chips.at(index);
        const QRectF rect = chipRectPx(chip);

        // A fine offset moves the change away from its chord - keep a guide back to the chord
        if (std::abs(chip.xN - chip.chordXN) * width() > 0.5) {
            painter->setPen(QPen(chip.color, 1.0));
            painter->drawLine(QPointF(chip.chordXN * width(), 0), QPointF(chip.chordXN * width(), height()));
        }

        // The color says which articulation
        const QColor textColor = artMapTextColorFor(chip.color);
        painter->setPen(Qt::NoPen);
        painter->setBrush(chip.color);
        painter->drawRoundedRect(rect, ARTMAP_CHIP_CORNER_RADIUS_PX, ARTMAP_CHIP_CORNER_RADIUS_PX);

        const QRectF textRect = rect;

        // Shrunk chips show as many leading letters as fit (no ellipsis), down to the first one,
        // then become a plain colored tick; the text stays centered, so both paddings stay equal
        const QString& text = index == m_hoveredChip ? chip.fullName : chip.label;
        const qreal textWidth = textRect.width() - 2 * ARTMAP_CHIP_PADDING_X_PX;
        QString shownText = text;
        while (shownText.size() > 1 && metrics.horizontalAdvance(shownText) > textWidth) {
            shownText.chop(1);
        }
        shownText = shownText.trimmed();
        if (metrics.horizontalAdvance(shownText) > textRect.width() - 2.0) {
            shownText.clear();
        }

        if (!shownText.isEmpty()) {
            painter->setPen(textColor);
            painter->drawText(textRect, Qt::AlignCenter, shownText);
        }
    };

    for (int i = 0; i < m_chips.size(); ++i) {
        if (i != m_hoveredChip) {
            drawChip(i);
        }
    }

    if (m_hoveredChip >= 0 && m_hoveredChip < m_chips.size()) {
        drawChip(m_hoveredChip);
    }
}

int ArticulationMapOverlay::hitTestPx(const QPointF& posPx) const
{
    if (m_hoveredChip >= 0 && m_hoveredChip < m_chips.size() && chipRectPx(m_chips.at(m_hoveredChip)).contains(posPx)) {
        return m_hoveredChip;
    }

    for (int i = static_cast<int>(m_chips.size()) - 1; i >= 0; --i) {
        if (chipRectPx(m_chips.at(i)).contains(posPx)) {
            return i;
        }
    }

    return -1;
}

void ArticulationMapOverlay::hoverMoveEvent(QHoverEvent* e)
{
    const int hit = hitTestPx(e->position());
    if (hit != m_hoveredChip) {
        m_hoveredChip = hit;
        update();
    }

    // See the cursor-priority notes in notevelocityoverlay.cpp: the displayed cursor follows the
    // topmost item that declared one, so declare it here, over the whole lane
    // Every chip can be moved: moving an automatic one turns it into a mark (see the controller)
    setCursor(hit >= 0 ? Qt::SizeHorCursor : Qt::ArrowCursor);
    m_hoveringChip = true;
}

void ArticulationMapOverlay::hoverLeaveEvent(QHoverEvent*)
{
    m_hoveringChip = false;
    if (m_hoveredChip != -1) {
        m_hoveredChip = -1;
        update();
    }
    unsetCursor();
}

void ArticulationMapOverlay::mousePressEvent(QMouseEvent* e)
{
    m_pressed = true;
    m_pressedButton = e->button();
    m_activeChip = hitTestPx(e->position());
    m_dragStartXPx = e->position().x();
    m_movedPastClickThreshold = false;
    e->accept();
}

void ArticulationMapOverlay::mouseMoveEvent(QMouseEvent* e)
{
    if (!m_pressed || m_pressedButton != Qt::LeftButton || m_activeChip < 0) {
        return;
    }

    if (std::abs(e->position().x() - m_dragStartXPx) > ARTMAP_CLICK_MOVE_THRESHOLD_PX) {
        m_movedPastClickThreshold = true;
    }

    if (m_movedPastClickThreshold) {
        emit chipDragged(m_activeChip, (e->position().x() - m_dragStartXPx) / std::max(1.0, width()), false);
    }
}

void ArticulationMapOverlay::mouseReleaseEvent(QMouseEvent* e)
{
    if (!m_pressed) {
        return;
    }

    const int chip = m_activeChip;
    const bool wasDrag = m_movedPastClickThreshold;

    m_pressed = false;
    m_activeChip = -1;
    m_movedPastClickThreshold = false;

    if (wasDrag) {
        emit chipDragged(chip, (e->position().x() - m_dragStartXPx) / std::max(1.0, width()), true);
        return;
    }

    emit clicked(chip, e->position().x() / std::max(1.0, width()), e->globalPosition());
}

void ArticulationMapOverlay::mouseUngrabEvent()
{
    if (m_pressed && m_movedPastClickThreshold) {
        emit dragCancelled(m_activeChip);
    }

    m_pressed = false;
    m_activeChip = -1;
    m_movedPastClickThreshold = false;
}
