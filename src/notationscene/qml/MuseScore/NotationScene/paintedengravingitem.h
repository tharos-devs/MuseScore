/*
 * SPDX-License-Identifier: GPL-3.0-only
 * MuseScore-Studio-CLA-applies
 *
 * MuseScore Studio
 * Music Composition & Notation
 *
 * Copyright (C) 2024 MuseScore Limited and others
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
#include <qqmlintegration.h>

#include "modularity/ioc.h"
#include "engraving/iengravingconfiguration.h"
#include "engraving/rendering/isinglerenderer.h"
#include "engraving/dom/engravingitem.h"

namespace mu::notation {
class PaintedEngravingItem : public QQuickPaintedItem
{
    Q_OBJECT

    Q_PROPERTY(QVariant engravingItem READ engravingItemVariant WRITE setEngravingItemVariant NOTIFY engravingItemVariantChanged)
    Q_PROPERTY(int numStaffLines READ numStaffLines WRITE setNumStaffLines NOTIFY numStaffLinesChanged)

    Q_PROPERTY(double spatium READ spatium WRITE setSpatium NOTIFY spatiumChanged)

    //! NOTE: of the item and the staff lines, these fully opaque (e.g. the theme's text color); unset: the engraving's default
    //! (black), staff lines half transparent
    Q_PROPERTY(QColor color READ color WRITE setColor NOTIFY colorChanged)

    QML_ELEMENT

    muse::GlobalInject<engraving::IEngravingConfiguration> configuration;
    muse::GlobalInject<engraving::rendering::ISingleRenderer> renderer;

public:
    explicit PaintedEngravingItem(QQuickItem* parent = nullptr);

    QVariant engravingItemVariant() const;
    void setEngravingItemVariant(QVariant engravingItemVariant);

    int numStaffLines() const;
    void setNumStaffLines(int numStaffLines);

    double spatium() const;
    void setSpatium(double spatium);

    QColor color() const;
    void setColor(const QColor& color);

    void paint(QPainter* painter) override;

signals:
    void engravingItemVariantChanged();
    void numStaffLinesChanged();
    void spatiumChanged();
    void colorChanged();

private:
    void paintNotationPreview(muse::draw::Painter& painter) const;

    mu::engraving::ElementPtr m_item;
    int m_numStaffLines = 0;

    double m_spatium = 1.0;
    QColor m_color;
};
}
