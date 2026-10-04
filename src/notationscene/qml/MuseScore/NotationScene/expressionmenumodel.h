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

#include "modularity/ioc.h"

#include "uicomponents/qml/Muse/UiComponents/abstractmenumodel.h"

#include "notation/inotationconfiguration.h"

namespace mu::notation {
// Backs the toolbar Expression split-button: its dropdown lists the note offset, note velocity
// and articulation editors, each shown/hidden independently (they can be combined). Showing one
// also makes it the editor of the button (see NotationActionController), which a click toggles.
class ExpressionMenuModel : public muse::uicomponents::AbstractMenuModel
{
    Q_OBJECT
    QML_ELEMENT;

    Q_PROPERTY(QString currentTitle READ currentTitle NOTIFY currentChanged)
    Q_PROPERTY(int currentIcon READ currentIcon NOTIFY currentChanged)
    Q_PROPERTY(bool currentIconIsLarge READ currentIconIsLarge NOTIFY currentChanged)
    Q_PROPERTY(QStringList titles READ titles CONSTANT)

    muse::GlobalInject<INotationConfiguration> notationConfiguration;

public:
    Q_INVOKABLE void init();

    QString currentTitle() const;
    int currentIcon() const;
    //! NOTE: the articulation glyph is drawn larger than the others by the icon font
    bool currentIconIsLarge() const;
    QStringList titles() const;

signals:
    void currentChanged();

private:
    void updateItems();
};
}
