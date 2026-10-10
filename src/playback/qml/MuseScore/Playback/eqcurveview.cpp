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
#include "eqcurveview.h"

#include <algorithm>
#include <cmath>
#include <limits>

#include <QPainter>
#include <QPainterPath>

#include "audio/common/channeleq.h"

using namespace mu::playback;
using namespace muse::audio;

// the curve is the same at any sample rate in the audible range: one is enough to draw it
static constexpr double DISPLAY_SAMPLE_RATE = 48000.0;
static constexpr double DB_RANGE = 24.0;
static constexpr double POINT_RADIUS = 8.0;
static constexpr double POINT_HIT_RADIUS = 12.0;

EqCurveView::EqCurveView(QQuickItem* parent)
    : QQuickPaintedItem(parent)
{
    setAntialiasing(true);
}

QVariantMap EqCurveView::eq() const
{
    return m_eqMap;
}

void EqCurveView::setEq(const QVariantMap& eq)
{
    if (m_eqMap == eq) {
        return;
    }

    m_eqMap = eq;

    EqParams params;
    params.enabled = eq.value("enabled", true).toBool();
    const QVariantList bands = eq.value("bands").toList();
    for (size_t i = 0; i < EQ_BAND_COUNT && i < static_cast<size_t>(bands.size()); ++i) {
        const QVariantMap bandMap = bands.at(static_cast<int>(i)).toMap();
        EqBandParams& band = params.bands[i];
        band.type = static_cast<EqBandType>(bandMap.value("type").toInt());
        band.frequency = bandMap.value("frequency").toFloat();
        band.gain = bandMap.value("gain").toFloat();
        band.q = bandMap.value("q").toFloat();
        band.enabled = bandMap.value("enabled").toBool();
    }
    m_eq = params;

    emit eqChanged();
    update();
}

bool EqCurveView::editable() const
{
    return m_editable;
}

void EqCurveView::setEditable(bool editable)
{
    if (m_editable == editable) {
        return;
    }

    m_editable = editable;
    setAcceptedMouseButtons(editable ? Qt::LeftButton : Qt::NoButton);
    setAcceptHoverEvents(editable);

    emit editableChanged();
    update();
}

int EqCurveView::selectedBand() const
{
    return m_selectedBand;
}

void EqCurveView::setSelectedBand(int band)
{
    if (m_selectedBand == band) {
        return;
    }

    m_selectedBand = band;
    emit selectedBandChanged();
    update();
}

QString EqCurveView::hoverText() const
{
    return m_hoverText;
}

QColor EqCurveView::backgroundColor() const
{
    return m_backgroundColor;
}

void EqCurveView::setBackgroundColor(const QColor& color)
{
    m_backgroundColor = color;
    emit colorsChanged();
    update();
}

QColor EqCurveView::curveColor() const
{
    return m_curveColor;
}

void EqCurveView::setCurveColor(const QColor& color)
{
    m_curveColor = color;
    emit colorsChanged();
    update();
}

QColor EqCurveView::gridColor() const
{
    return m_gridColor;
}

void EqCurveView::setGridColor(const QColor& color)
{
    m_gridColor = color;
    emit colorsChanged();
    update();
}

QColor EqCurveView::textColor() const
{
    return m_textColor;
}

void EqCurveView::setTextColor(const QColor& color)
{
    m_textColor = color;
    emit colorsChanged();
    update();
}

QRectF EqCurveView::plotRect() const
{
    if (!m_editable) {
        return QRectF(0, 0, width(), height());
    }

    // room for the dB scale on the left and the frequency scale below
    // and for half of the last frequency label on the right
    return QRectF(30, 6, std::max(0.0, width() - 46), std::max(0.0, height() - 24));
}

double EqCurveView::xForFrequency(double frequency) const
{
    const QRectF rect = plotRect();
    const double minLog = std::log10(EQ_FREQUENCY_MIN);
    const double maxLog = std::log10(EQ_FREQUENCY_MAX);
    return rect.left() + (std::log10(frequency) - minLog) / (maxLog - minLog) * rect.width();
}

double EqCurveView::frequencyForX(double x) const
{
    const QRectF rect = plotRect();
    const double minLog = std::log10(EQ_FREQUENCY_MIN);
    const double maxLog = std::log10(EQ_FREQUENCY_MAX);
    const double ratio = rect.width() > 0 ? (x - rect.left()) / rect.width() : 0.0;
    return std::clamp(std::pow(10.0, minLog + ratio * (maxLog - minLog)),
                      static_cast<double>(EQ_FREQUENCY_MIN), static_cast<double>(EQ_FREQUENCY_MAX));
}

double EqCurveView::yForDb(double db) const
{
    const QRectF rect = plotRect();
    return rect.center().y() - std::clamp(db, -DB_RANGE, DB_RANGE) / DB_RANGE * rect.height() / 2.0;
}

double EqCurveView::dbForY(double y) const
{
    const QRectF rect = plotRect();
    const double db = rect.height() > 0 ? (rect.center().y() - y) / (rect.height() / 2.0) * DB_RANGE : 0.0;
    return std::clamp(db, static_cast<double>(EQ_GAIN_DB_MIN), static_cast<double>(EQ_GAIN_DB_MAX));
}

//! NOTE At its gain; a band without gain (pass filter) on the 0 dB line
QPointF EqCurveView::bandPoint(size_t band) const
{
    const EqBandParams& params = m_eq.bands[band];
    const double db = eq::bandTypeHasGain(params.type) ? params.gain : 0.0;
    return QPointF(xForFrequency(params.frequency), yForDb(db));
}

int EqCurveView::bandAt(const QPointF& pos) const
{
    int found = -1;
    double nearest = POINT_HIT_RADIUS;

    for (size_t i = 0; i < EQ_BAND_COUNT; ++i) {
        const QPointF point = bandPoint(i);
        const double distance = std::hypot(point.x() - pos.x(), point.y() - pos.y());
        if (distance <= nearest) {
            nearest = distance;
            found = static_cast<int>(i);
        }
    }

    return found;
}

void EqCurveView::paint(QPainter* painter)
{
    painter->setRenderHint(QPainter::Antialiasing, true);
    painter->fillRect(QRectF(0, 0, width(), height()), m_backgroundColor);

    if (m_editable) {
        paintGrid(painter);
    } else {
        // the 0 dB line
        QColor lineColor = m_gridColor;
        painter->setPen(QPen(lineColor, 1));
        painter->drawLine(QPointF(0, yForDb(0)), QPointF(width(), yForDb(0)));
    }

    paintCurve(painter);

    if (m_editable) {
        paintBandPoints(painter);
    }
}

void EqCurveView::paintGrid(QPainter* painter) const
{
    const QRectF rect = plotRect();

    QFont font = painter->font();
    font.setPixelSize(10);
    painter->setFont(font);

    for (int db = -24; db <= 24; db += 6) {
        const double y = yForDb(db);
        painter->setPen(QPen(m_gridColor, db == 0 ? 1.5 : 1));
        painter->drawLine(QPointF(rect.left(), y), QPointF(rect.right(), y));

        painter->setPen(m_textColor);
        const QString label = db > 0 ? QString("+%1").arg(db) : QString::number(db);
        painter->drawText(QRectF(0, y - 7, rect.left() - 4, 14), Qt::AlignRight | Qt::AlignVCenter, label);
    }

    static const std::vector<std::pair<double, QString> > FREQUENCIES {
        { 20, "20" }, { 50, "50" }, { 100, "100" }, { 200, "200" }, { 500, "500" },
        { 1000, "1 k" }, { 2000, "2 k" }, { 5000, "5 k" }, { 10000, "10 k" }, { 20000, "20 k" },
    };

    for (const auto& [frequency, label] : FREQUENCIES) {
        const double x = xForFrequency(frequency);
        painter->setPen(QPen(m_gridColor, 1));
        painter->drawLine(QPointF(x, rect.top()), QPointF(x, rect.bottom()));

        painter->setPen(m_textColor);
        painter->drawText(QRectF(x - 20, rect.bottom() + 2, 40, 14), Qt::AlignCenter, label);
    }
}

void EqCurveView::paintCurve(QPainter* painter) const
{
    const QRectF rect = plotRect();
    if (rect.width() <= 1 || rect.height() <= 1) {
        return;
    }

    const int steps = std::max(2, static_cast<int>(rect.width()));
    const std::vector<eq::BandFilter> filters = eq::activeFilters(m_eq, DISPLAY_SAMPLE_RATE);
    QPainterPath curve;
    for (int i = 0; i <= steps; ++i) {
        const double x = rect.left() + rect.width() * i / steps;
        const double db = eq::responseDb(filters, frequencyForX(x), DISPLAY_SAMPLE_RATE);
        const QPointF point(x, yForDb(db));
        if (i == 0) {
            curve.moveTo(point);
        } else {
            curve.lineTo(point);
        }
    }

    // dimmed when the EQ is off
    QColor color = m_curveColor;
    if (!m_eq.enabled) {
        color.setAlphaF(0.35);
    }

    // the area between the curve and 0 dB
    QPainterPath area = curve;
    area.lineTo(rect.right(), yForDb(0));
    area.lineTo(rect.left(), yForDb(0));
    area.closeSubpath();

    QColor fill = color;
    fill.setAlphaF(color.alphaF() * 0.2);
    painter->setPen(Qt::NoPen);
    painter->setBrush(fill);
    painter->drawPath(area);

    painter->setBrush(Qt::NoBrush);
    painter->setPen(QPen(color, m_editable ? 2.0 : 1.5));
    painter->drawPath(curve);
}

void EqCurveView::paintBandPoints(QPainter* painter) const
{
    QFont font = painter->font();
    font.setPixelSize(10);
    font.setBold(true);
    painter->setFont(font);

    for (size_t i = 0; i < EQ_BAND_COUNT; ++i) {
        const EqBandParams& band = m_eq.bands[i];
        const QPointF point = bandPoint(i);
        const bool selected = static_cast<int>(i) == m_selectedBand;
        const bool active = band.enabled && m_eq.enabled;

        QColor ring = active ? m_textColor : m_gridColor;
        QColor fill = selected ? m_curveColor : m_backgroundColor;
        if (!active) {
            fill.setAlphaF(0.6);
        }

        painter->setPen(QPen(ring, 1.5));
        painter->setBrush(fill);
        painter->drawEllipse(point, POINT_RADIUS, POINT_RADIUS);

        painter->setPen(active ? m_textColor : m_gridColor);
        painter->drawText(QRectF(point.x() - POINT_RADIUS, point.y() - POINT_RADIUS, 2 * POINT_RADIUS, 2 * POINT_RADIUS),
                          Qt::AlignCenter, QString::number(i + 1));
    }
}

void EqCurveView::mousePressEvent(QMouseEvent* event)
{
    const int band = bandAt(event->position());
    if (band < 0) {
        event->ignore();
        return;
    }

    m_draggedBand = band;
    setSelectedBand(band);
    emit bandDragStarted(band);
    event->accept();
}

void EqCurveView::mouseMoveEvent(QMouseEvent* event)
{
    if (m_draggedBand < 0) {
        return;
    }

    const QPointF pos = event->position();
    const bool hasGain = eq::bandTypeHasGain(m_eq.bands[m_draggedBand].type);
    emit bandDragged(m_draggedBand, frequencyForX(pos.x()),
                     hasGain ? dbForY(pos.y()) : std::numeric_limits<double>::quiet_NaN());
    updateHoverText(pos);
}

void EqCurveView::mouseReleaseEvent(QMouseEvent*)
{
    if (m_draggedBand < 0) {
        return;
    }

    const int band = m_draggedBand;
    m_draggedBand = -1;
    emit bandDragFinished(band);
}

//! NOTE An interrupted drag (e.g. the window losing the mouse) ends like a released one
void EqCurveView::mouseUngrabEvent()
{
    mouseReleaseEvent(nullptr);
}

void EqCurveView::hoverMoveEvent(QHoverEvent* event)
{
    updateHoverText(event->position());
}

void EqCurveView::hoverLeaveEvent(QHoverEvent*)
{
    if (!m_hoverText.isEmpty()) {
        m_hoverText.clear();
        emit hoverTextChanged();
    }
}

void EqCurveView::wheelEvent(QWheelEvent* event)
{
    const int band = bandAt(event->position());
    if (band < 0 || !eq::bandTypeHasQ(m_eq.bands[band].type)) {
        event->ignore();
        return;
    }

    const double notches = event->angleDelta().y() / 120.0;
    const double q = std::clamp(m_eq.bands[band].q * std::pow(1.1, notches), static_cast<double>(EQ_Q_MIN),
                                static_cast<double>(EQ_Q_MAX));
    setSelectedBand(band);
    emit bandQScrolled(band, q);
    event->accept();
}

//! NOTE The frequency and the note under the mouse, and the curve's value there
void EqCurveView::updateHoverText(const QPointF& pos)
{
    const QRectF rect = plotRect();
    QString text;

    if (rect.contains(pos)) {
        static const char* NOTE_NAMES[] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };

        const double frequency = frequencyForX(pos.x());
        const int midiNote = static_cast<int>(std::lround(69.0 + 12.0 * std::log2(frequency / 440.0)));
        const QString note = QString("%1%2").arg(NOTE_NAMES[((midiNote % 12) + 12) % 12]).arg(midiNote / 12 - 1);
        const double db = eq::responseDb(m_eq, frequency, DISPLAY_SAMPLE_RATE);

        text = QString("%1 Hz   %2   %3 dB").arg(frequency, 0, 'f', 1).arg(note).arg(db, 0, 'f', 1);
    }

    if (m_hoverText != text) {
        m_hoverText = text;
        emit hoverTextChanged();
    }
}
