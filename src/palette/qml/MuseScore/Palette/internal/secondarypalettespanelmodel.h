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

#include <QModelIndex>

#include "uicomponents/qml/Muse/UiComponents/zoomablemenumodel.h"

namespace mu::palette {
//! NOTE: the Secondary palettes panel's "…" menu (its zoom) and its persisted state: the palette shown in its
//! second column (by name), and whether that column is collapsed
class SecondaryPalettesPanelModel : public muse::uicomponents::ZoomableMenuModel
{
    Q_OBJECT

    Q_PROPERTY(QString currentPaletteName READ currentPaletteName WRITE setCurrentPaletteName NOTIFY currentPaletteNameChanged)
    Q_PROPERTY(bool collapsed READ collapsed WRITE setCollapsed NOTIFY collapsedChanged)

    QML_ELEMENT

public:
    explicit SecondaryPalettesPanelModel(QObject* parent = nullptr);

    Q_INVOKABLE void load() override;

    QString currentPaletteName() const;
    void setCurrentPaletteName(const QString& name);

    bool collapsed() const;
    void setCollapsed(bool collapsed);

    //! NOTE: the icon (an IconCode) standing for a palette in the first column, or IconCode.NONE for none
    //! (the panel then shows the palette's first cell)
    Q_INVOKABLE int paletteIconCode(const QModelIndex& paletteIndex) const;

    //! NOTE: a short word standing for a palette in the first column instead of an icon, or empty for none
    Q_INVOKABLE QString paletteIconText(const QModelIndex& paletteIndex) const;

signals:
    void currentPaletteNameChanged();
    void collapsedChanged();
};
}
