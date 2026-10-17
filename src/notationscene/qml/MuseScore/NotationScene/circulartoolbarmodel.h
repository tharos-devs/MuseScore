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

#include <optional>

#include "menuitemsutils.h"
#include "quickactionsmenumodel.h"

namespace mu::notation {
//! NOTE: the buttons of the circular toolbar (see CircularToolbar.qml). Rebuilt each time the toolbar shows, which
//! closes again once one is clicked, so their state is the one at that moment
class CircularToolbarModel : public QuickActionsMenuModel
{
    Q_OBJECT
    QML_ELEMENT;

    //! NOTE: without one, the articulation map editor button still opens an empty editor
    Q_PROPERTY(
        bool focusedStaffHasArticulationMap READ focusedStaffHasArticulationMap NOTIFY focusedStaffHasArticulationMapChanged)

public:
    //! NOTE: the buttons are found by their id, see CircularToolbar.qml
    Q_INVOKABLE void prepareOpening();

    void handleMenuItem(const QString& itemId) override;

    bool focusedStaffHasArticulationMap() const;

signals:
    void focusedStaffHasArticulationMapChanged();

private:
    //! NOTE: the instrument of the selected staff, at the selection
    std::optional<engraving::InstrumentTrackId> actionsTrackId() const override;

    muse::uicomponents::MenuItem* makeMoreMidiCcMenu(bool enabled);

    ReplacedMenuItems m_replacedItems;
    bool m_focusedStaffHasArticulationMap = false;
};
}
