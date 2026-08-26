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

#include <QObject>
#include <QVariantList>
#include <qqmlintegration.h>

#include "engraving/iengravingfont.h"

#include "../abstractelementpopupmodel.h"

namespace mu::notation {
class ArticulationPopupModel : public AbstractElementPopupModel
{
    Q_OBJECT
    QML_ELEMENT

    Q_PROPERTY(QString fontFamily READ fontFamily CONSTANT)
    Q_PROPERTY(QVariantList pages READ pages NOTIFY pagesChanged)
    Q_PROPERTY(bool placeAbove READ placeAbove NOTIFY placeAboveChanged)

public:
    explicit ArticulationPopupModel(QObject* parent = nullptr);

    static bool canOpen(const engraving::EngravingItem* element);

    Q_INVOKABLE void init() override;
    Q_INVOKABLE void changeArticulation(int page, int index);

    QString fontFamily() const;
    QVariantList pages() const;
    bool placeAbove() const;

signals:
    void pagesChanged();
    void placeAboveChanged();

private:
    void load();

    // Page 0: the 5 basic articulation types (default page)
    // Page 1: the 5 "double" combos (paginated left of the base page)
    QVariantList m_pages;
    // Whether the popup's item is currently the "Above" variant of its symId - drives both which
    // glyph variant each page's buttons show and which side of the note the popup itself opens on.
    bool m_above = true;
};
}
