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

#include "modularity/ioc.h"

#include "menuitemsutils.h"
#include "uicomponents/qml/Muse/UiComponents/abstractmenumodel.h"

#include "actions/iactionsdispatcher.h"
#include "context/iglobalcontext.h"
#include "notation/inotationconfiguration.h"
#include "playback/iplaybackcontroller.h"

#include "muse_framework_config.h"
#ifdef MUSE_MODULE_VST
#include "vst/ivstpluginstateprovider.h"
#endif

namespace mu::notation {
//! NOTE: the buttons of the circular toolbar (see CircularToolbar.qml), each running the same command as the
//! existing button doing the same thing. Rebuilt each time the toolbar shows, which closes again once one is
//! clicked, so their state is the one at that moment
class CircularToolbarModel : public muse::uicomponents::AbstractMenuModel
{
    Q_OBJECT
    QML_ELEMENT;

    //! NOTE: without one, the articulation map editor button still opens an empty editor
    Q_PROPERTY(
        bool focusedStaffHasArticulationMap READ focusedStaffHasArticulationMap NOTIFY focusedStaffHasArticulationMapChanged)

    muse::GlobalInject<INotationConfiguration> notationConfiguration;
    muse::ContextInject<context::IGlobalContext> globalContext = { this };
    muse::ContextInject<playback::IPlaybackController> playbackController = { this };
    muse::ContextInject<muse::actions::IActionsDispatcher> actionsDispatcher = { this };
#ifdef MUSE_MODULE_VST
    muse::GlobalInject<muse::vst::IVstPluginStateProvider> vstPluginStateProvider;
#endif

public:
    //! NOTE: the buttons are found by their id, see CircularToolbar.qml
    Q_INVOKABLE void prepareOpening();

    void handleMenuItem(const QString& itemId) override;

    bool focusedStaffHasArticulationMap() const;

signals:
    void focusedStaffHasArticulationMapChanged();

private:
    muse::uicomponents::MenuItem* makeAutomationItem(const QString& id, const std::string& type, int controller,
                                                     const muse::TranslatableString& title, bool enabled);
    muse::uicomponents::MenuItem* makeMoreMidiCcMenu(bool enabled);
    muse::uicomponents::MenuItem* makeLastMidiCcItem(bool enabled);
    void updateLastMidiCc();
    muse::uicomponents::MenuItem* makeArticulationMapEditorItem();
    muse::uicomponents::MenuItem* makeInstrumentEditorItem();

    //! NOTE: the instrument of the selected staff, at the selection
    std::optional<engraving::InstrumentTrackId> focusedTrackId() const;
    bool isInstrumentEditorOpened() const;

    ReplacedMenuItems m_replacedItems;
    bool m_focusedStaffHasArticulationMap = false;

    //! NOTE: the last MIDI CC picked in the "…" menu, -1 if none
    int m_lastMidiCc = -1;
};
}
