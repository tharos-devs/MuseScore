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
#include <QVariantMap>
#include <qqmlintegration.h>

#include "audio/common/audiotypes.h"

namespace mu::playback {
//! NOTE A channel EQ's response curve, computed with the same filters as the sound (see channeleq.h).
//! Small (a Mixer channel's thumbnail) or editable: then with its scale, and a point per band to drag
//! (frequency horizontally, gain vertically) or to scroll over (Q)
class EqCurveView : public QQuickPaintedItem
{
    Q_OBJECT
    QML_ELEMENT

    Q_PROPERTY(QVariantMap eq READ eq WRITE setEq NOTIFY eqChanged)
    Q_PROPERTY(bool editable READ editable WRITE setEditable NOTIFY editableChanged)
    Q_PROPERTY(int selectedBand READ selectedBand WRITE setSelectedBand NOTIFY selectedBandChanged)
    Q_PROPERTY(QString hoverText READ hoverText NOTIFY hoverTextChanged)

    Q_PROPERTY(QColor backgroundColor READ backgroundColor WRITE setBackgroundColor NOTIFY colorsChanged)
    Q_PROPERTY(QColor curveColor READ curveColor WRITE setCurveColor NOTIFY colorsChanged)
    Q_PROPERTY(QColor gridColor READ gridColor WRITE setGridColor NOTIFY colorsChanged)
    Q_PROPERTY(QColor textColor READ textColor WRITE setTextColor NOTIFY colorsChanged)

public:
    explicit EqCurveView(QQuickItem* parent = nullptr);

    QVariantMap eq() const;
    void setEq(const QVariantMap& eq);

    bool editable() const;
    void setEditable(bool editable);

    int selectedBand() const;
    void setSelectedBand(int band);

    QString hoverText() const;

    QColor backgroundColor() const;
    void setBackgroundColor(const QColor& color);
    QColor curveColor() const;
    void setCurveColor(const QColor& color);
    QColor gridColor() const;
    void setGridColor(const QColor& color);
    QColor textColor() const;
    void setTextColor(const QColor& color);

    void paint(QPainter* painter) override;

signals:
    void eqChanged();
    void editableChanged();
    void selectedBandChanged();
    void hoverTextChanged();
    void colorsChanged();

    //! NOTE gain is NaN for a band without gain (pass filters)
    void bandDragStarted(int band);
    void bandDragged(int band, double frequency, double gain);
    void bandDragFinished(int band);
    void bandQScrolled(int band, double q);

protected:
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void hoverMoveEvent(QHoverEvent* event) override;
    void hoverLeaveEvent(QHoverEvent* event) override;
    void wheelEvent(QWheelEvent* event) override;

private:
    QRectF plotRect() const;
    double xForFrequency(double frequency) const;
    double frequencyForX(double x) const;
    double yForDb(double db) const;
    double dbForY(double y) const;
    QPointF bandPoint(size_t band) const;
    int bandAt(const QPointF& pos) const;
    void updateHoverText(const QPointF& pos);

    void paintGrid(QPainter* painter) const;
    void paintCurve(QPainter* painter) const;
    void paintBandPoints(QPainter* painter) const;

    QVariantMap m_eqMap;
    muse::audio::EqParams m_eq;
    bool m_editable = false;
    int m_selectedBand = -1;
    int m_draggedBand = -1;
    QString m_hoverText;

    QColor m_backgroundColor = QColor(30, 30, 30);
    QColor m_curveColor = QColor(80, 160, 230);
    QColor m_gridColor = QColor(70, 70, 70);
    QColor m_textColor = QColor(200, 200, 200);
};
}
