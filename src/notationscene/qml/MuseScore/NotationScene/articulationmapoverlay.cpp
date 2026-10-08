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
#include <QGuiApplication>
#include <QHoverEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QRegion>
#include <QToolTip>

using namespace mu::notation;

constexpr static qreal ARTMAP_CLICK_MOVE_THRESHOLD_PX = 3.0;
constexpr static int ARTMAP_TOOLTIP_KEY_POLL_MS = 100;
constexpr static qreal ARTMAP_CHIP_V_MARGIN_PX = 2.0;
constexpr static qreal ARTMAP_CHIP_PADDING_X_PX = 4.0;
constexpr static qreal ARTMAP_CHIP_MIN_WIDTH_PX = 6.0;
constexpr static qreal ARTMAP_CHIP_GAP_PX = 2.0;
constexpr static qreal ARTMAP_CHIP_CORNER_RADIUS_PX = 3.0;
constexpr static qreal ARTMAP_MIN_FONT_PX = 9.0;
constexpr static qreal ARTMAP_MAX_FONT_PX = 13.0;
constexpr static qreal ARTMAP_LINE_WIDTH_PX = 2.0;
constexpr static int ARTMAP_HOVERED_CHIP_ALPHA = 60;
constexpr static qreal ARTMAP_TARGET_MARKER_SIZE_PX = 5.0;
// A chord's span starts a bit before it: the mouse reaches a note's left side before its position
constexpr static qreal ARTMAP_TARGET_LEAD_PX = 4.0;
static const QColor ARTMAP_TARGET_MARKER_COLOR(90, 90, 90);

static double artMapLuminance(const QColor& color)
{
    return 0.299 * color.red() + 0.587 * color.green() + 0.114 * color.blue();
}

static QColor artMapTextColorFor(const QColor& fill)
{
    return artMapLuminance(fill) > 150.0 ? QColor(20, 20, 20) : QColor(255, 255, 255);
}

//! NOTE: the see-through (hovered) chip's text: its own color, darkened as needed to read on the light paper
//! and the faint tint of that same color under it (a light yellow on its own would be invisible)
static QColor artMapSeeThroughTextColorFor(const QColor& color)
{
    constexpr double MAX_LUMINANCE = 105.0;

    QColor result = color;
    result.setAlpha(255);
    for (int i = 0; i < 12 && artMapLuminance(result) > MAX_LUMINANCE; ++i) {
        result = result.darker(120);
    }
    return result;
}

ArticulationMapOverlay::ArticulationMapOverlay(QQuickItem* parent)
    : QQuickPaintedItem(parent)
{
    // Left button only: a right click must reach MuseScore's own context menu underneath
    setAcceptedMouseButtons(Qt::LeftButton);
    setAcceptHoverEvents(true);

    // The lane has no keyboard focus: Cmd is watched while a chip is hovered
    m_tooltipTimer.setInterval(ARTMAP_TOOLTIP_KEY_POLL_MS);
    QObject::connect(&m_tooltipTimer, &QTimer::timeout, this, [this]() {
        updateChipTooltip();
    });
}

ArticulationMapOverlay::~ArticulationMapOverlay()
{
    hideChipTooltip();
}

//! NOTE: the full name of the hovered chip ("Legato > Auto-speed"), only while Cmd (Ctrl elsewhere) is held,
//! under the lane: never over another chip, nor in the way of a click
void ArticulationMapOverlay::updateChipTooltip()
{
    const bool wanted = m_hoveredChip >= 0 && m_hoveredChip < m_chips.size() && !m_pressed
                        && (QGuiApplication::queryKeyboardModifiers() & Qt::ControlModifier);
    if (!wanted) {
        if (m_tooltipShown) {
            QToolTip::hideText();
            m_tooltipShown = false;
        }
        return;
    }

    if (m_tooltipShown) {
        return;
    }

    const QRectF rect = chipRectPx(m_chips.at(m_hoveredChip));
    // QToolTip shows the tip at an offset from the given position (2, 16 px): cancelled, to stick to the chip
    constexpr QPointF TOOLTIP_OFFSET_PX(2.0, 16.0);
    constexpr qreal TOOLTIP_GAP_PX = 2.0;
    const QPointF globalPos = mapToGlobal(QPointF(rect.left(), rect.bottom() + TOOLTIP_GAP_PX)) - TOOLTIP_OFFSET_PX;
    QToolTip::showText(globalPos.toPoint(), m_chips.at(m_hoveredChip).fullName);
    m_tooltipShown = true;
}

void ArticulationMapOverlay::hideChipTooltip()
{
    m_tooltipTimer.stop();
    if (m_tooltipShown) {
        QToolTip::hideText();
        m_tooltipShown = false;
    }
}

void ArticulationMapOverlay::setContent(const QVector<ChipData>& chips, const QVector<LineData>& lines)
{
    m_chips = chips;
    m_lines = lines;
    m_hoveredChip = -1;
    hideChipTooltip();
    update();
}

void ArticulationMapOverlay::setChordPositions(const QVector<qreal>& chordXNs)
{
    m_chordXNs = chordXNs;
    m_hoverTargetXN = -1.0;
    update();
}

void ArticulationMapOverlay::setPinnedTargetX(qreal xN)
{
    if (m_pinnedTargetXN == xN) {
        return;
    }

    m_pinnedTargetXN = xN;
    update();
}

//! NOTE: the chord the mouse is in the span of (from it to the next one), also over a chip - so anywhere
//! between a chip's chord and the next one is that chip's; before the first chord, the first one.
//! A click goes to it (see mouseReleaseEvent())
qreal ArticulationMapOverlay::targetChordXN(const QPointF& posPx) const
{
    const qreal posXN = (posPx.x() + ARTMAP_TARGET_LEAD_PX) / std::max(1.0, width());
    qreal first = -1.0;
    qreal best = -1.0;
    for (const qreal xN : m_chordXNs) {
        if (first < 0.0 || xN < first) {
            first = xN;
        }
        if (xN <= posXN && (best < 0.0 || xN > best)) {
            best = xN;
        }
    }

    return best >= 0.0 ? best : first;
}

void ArticulationMapOverlay::drawTargetMarker(QPainter* painter, qreal xN) const
{
    const qreal x = xN * width();

    painter->setPen(QPen(ARTMAP_TARGET_MARKER_COLOR, 1.0, Qt::DashLine));
    painter->drawLine(QPointF(x, 0), QPointF(x, height()));

    const qreal s = ARTMAP_TARGET_MARKER_SIZE_PX;
    const QPointF triangle[3] = { QPointF(x - s, 0), QPointF(x + s, 0), QPointF(x, s) };
    painter->setPen(Qt::NoPen);
    painter->setBrush(ARTMAP_TARGET_MARKER_COLOR);
    painter->drawPolygon(triangle, 3);
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

bool ArticulationMapOverlay::isDragging() const
{
    return m_pressed && m_movedPastClickThreshold;
}

//! NOTE: a chip shows its whole name, cut only where the next chip starts - on consecutive changes, chips
//! shrink (down to a colored tick); hovering doesn't change that (see updateChipTooltip()). It may span over
//! the next notes: while one of them is the target, the chip is cut before it (see chipRectPx())
QRectF ArticulationMapOverlay::fullChipRectPx(const ChipData& chip) const
{
    QFont font;
    font.setPixelSize(static_cast<int>(std::clamp(height() * 0.55, ARTMAP_MIN_FONT_PX, ARTMAP_MAX_FONT_PX)));
    font.setBold(true);
    const QFontMetricsF metrics(font);

    qreal chipWidth = std::max(ARTMAP_CHIP_MIN_WIDTH_PX, metrics.horizontalAdvance(chip.label) + 2 * ARTMAP_CHIP_PADDING_X_PX);

    const qreal leftPx = chip.xN * width();

    qreal nextLeftPx = width();
    for (const ChipData& other : m_chips) {
        const qreal otherLeftPx = other.xN * width();
        if (otherLeftPx > leftPx + 0.5) {
            nextLeftPx = std::min(nextLeftPx, otherLeftPx);
        }
    }
    chipWidth = std::max(ARTMAP_CHIP_MIN_WIDTH_PX, std::min(chipWidth, nextLeftPx - leftPx - ARTMAP_CHIP_GAP_PX));

    return QRectF(leftPx, ARTMAP_CHIP_V_MARGIN_PX, chipWidth, std::max(1.0, height() - 2 * ARTMAP_CHIP_V_MARGIN_PX));
}

//! NOTE: a click goes to the note under the target marker: when that's another note than the chip's own one,
//! the chip is cut just before it, so that note shows as a free slot (a click there adds, not edits the chip)
QRectF ArticulationMapOverlay::chipRectPx(const ChipData& chip) const
{
    QRectF rect = fullChipRectPx(chip);

    const qreal targetXN = m_pinnedTargetXN >= 0.0 ? m_pinnedTargetXN : m_hoverTargetXN;
    if (targetXN < 0.0 || isDragging() || std::abs(targetXN - chip.chordXN) * width() <= 0.5) {
        return rect;
    }

    const qreal targetPx = targetXN * width();
    if (targetPx > rect.left() && targetPx < rect.right() + ARTMAP_TARGET_MARKER_SIZE_PX) {
        const qreal right = targetPx - ARTMAP_TARGET_MARKER_SIZE_PX - ARTMAP_CHIP_GAP_PX;
        rect.setWidth(std::max(ARTMAP_CHIP_MIN_WIDTH_PX, right - rect.left()));
    }

    return rect;
}

void ArticulationMapOverlay::paint(QPainter* painter)
{
    painter->setRenderHint(QPainter::Antialiasing);

    // No lane background: notes and ledger lines below the staff stay visible through it

    // The hovered chip is see-through (see drawChip below): keep the lines out of it, so its text stays readable
    const bool hoveredSeeThrough = m_hoveredChip >= 0 && m_hoveredChip < m_chips.size()
                                   && !(m_pressed && m_hoveredChip == m_activeChip);
    painter->save();
    if (hoveredSeeThrough) {
        QRegion clip(boundingRect().toAlignedRect());
        clip -= QRegion(chipRectPx(m_chips.at(m_hoveredChip)).toAlignedRect());
        painter->setClipRegion(clip);
    }

    const qreal midY = height() / 2.0;
    for (const LineData& line : m_lines) {
        QColor color = line.color;
        color.setAlpha(200);
        painter->setPen(QPen(color, ARTMAP_LINE_WIDTH_PX, Qt::SolidLine, Qt::FlatCap));
        painter->drawLine(QPointF(line.fromXN * width(), midY), QPointF(line.toXN * width(), midY));
    }
    painter->restore();

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

        // The color says which articulation. The hovered chip turns see-through, to see the notes and
        // staff under it; once pressed (e.g. to drag it) it's opaque again
        const bool hovered = index == m_hoveredChip;
        const bool pressed = m_pressed && index == m_activeChip;
        const bool seeThrough = hovered && !pressed;
        QColor fillColor = chip.color;
        if (seeThrough) {
            fillColor.setAlpha(ARTMAP_HOVERED_CHIP_ALPHA);
        }
        const QColor textColor = seeThrough ? artMapSeeThroughTextColorFor(chip.color) : artMapTextColorFor(chip.color);
        painter->setPen(Qt::NoPen);
        painter->setBrush(fillColor);
        painter->drawRoundedRect(rect, ARTMAP_CHIP_CORNER_RADIUS_PX, ARTMAP_CHIP_CORNER_RADIUS_PX);

        const QRectF textRect = rect;

        // Shrunk chips show as many leading letters as fit (no ellipsis), down to the first one,
        // then become a plain colored tick; the text stays centered, so both paddings stay equal
        const QString& text = chip.label;
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

    // Where a click would place the articulation (hidden while dragging a chip)
    const qreal targetXN = m_pinnedTargetXN >= 0.0 ? m_pinnedTargetXN : m_hoverTargetXN;
    if (targetXN >= 0.0 && !isDragging()) {
        drawTargetMarker(painter, targetXN);
    }
}

int ArticulationMapOverlay::hitTestPx(const QPointF& posPx) const
{
    for (int i = static_cast<int>(m_chips.size()) - 1; i >= 0; --i) {
        if (chipRectPx(m_chips.at(i)).contains(posPx)) {
            return i;
        }
    }

    return -1;
}

void ArticulationMapOverlay::hoverMoveEvent(QHoverEvent* e)
{
    // The target first: it decides whether a chip is cut before it (see chipRectPx())
    const qreal targetXN = targetChordXN(e->position());
    const bool targetChanged = targetXN != m_hoverTargetXN;
    m_hoverTargetXN = targetXN;

    // A chip lights up only when a click edits it, i.e. when its own chord is the target (one moved earlier
    // than its chord may span over the previous chord's span)
    int hit = hitTestPx(e->position());
    if (hit >= 0 && std::abs(m_chips.at(hit).chordXN - targetXN) * width() > 0.5) {
        hit = -1;
    }
    if (hit != m_hoveredChip) {
        hideChipTooltip();
    }
    if (hit != m_hoveredChip || targetChanged) {
        m_hoveredChip = hit;
        update();
    }
    if (hit >= 0 && !m_pressed) {
        if (!m_tooltipTimer.isActive()) {
            m_tooltipTimer.start();
        }
        updateChipTooltip();
    }

    // See the cursor-priority notes in notevelocityoverlay.cpp: the displayed cursor follows the
    // topmost item that declared one, so declare it here, over the whole lane
    // The default arrow, chips included: a click is what they're mostly for; the double arrow only shows
    // once a chip is being dragged (see mouseMoveEvent)
    setCursor(Qt::ArrowCursor);
    m_hoveringChip = true;
}

void ArticulationMapOverlay::hoverLeaveEvent(QHoverEvent*)
{
    m_hoveringChip = false;
    hideChipTooltip();
    if (m_hoveredChip != -1 || m_hoverTargetXN >= 0.0) {
        m_hoveredChip = -1;
        m_hoverTargetXN = -1.0;
        update();
    }
    unsetCursor();
}

void ArticulationMapOverlay::mousePressEvent(QMouseEvent* e)
{
    hideChipTooltip();
    m_pressed = true;
    m_pressedButton = e->button();
    m_activeChip = hitTestPx(e->position());
    m_dragStartXPx = e->position().x();
    m_movedPastClickThreshold = false;
    e->accept();
    update();
}

void ArticulationMapOverlay::mouseMoveEvent(QMouseEvent* e)
{
    if (!m_pressed || m_pressedButton != Qt::LeftButton || m_activeChip < 0) {
        return;
    }

    if (!m_movedPastClickThreshold && std::abs(e->position().x() - m_dragStartXPx) > ARTMAP_CLICK_MOVE_THRESHOLD_PX) {
        m_movedPastClickThreshold = true;
        // Every chip can be moved: moving an automatic one turns it into a mark (see the controller)
        setCursor(Qt::SizeHorCursor);
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
    if (wasDrag) {
        setCursor(Qt::ArrowCursor);
    }

    m_pressed = false;
    m_activeChip = -1;
    m_movedPastClickThreshold = false;
    update();

    if (wasDrag) {
        emit chipDragged(chip, (e->position().x() - m_dragStartXPx) / std::max(1.0, width()), true);
        return;
    }

    emit clicked(targetChordXN(e->position()), e->globalPosition());
}

void ArticulationMapOverlay::mouseUngrabEvent()
{
    if (m_pressed && m_movedPastClickThreshold) {
        setCursor(Qt::ArrowCursor);
        emit dragCancelled(m_activeChip);
    }

    m_pressed = false;
    m_activeChip = -1;
    m_movedPastClickThreshold = false;
    update();
}
