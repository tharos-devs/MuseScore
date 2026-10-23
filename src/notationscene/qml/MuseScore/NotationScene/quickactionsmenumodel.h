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
#include <vector>

#include "modularity/ioc.h"

#include "uicomponents/qml/Muse/UiComponents/abstractmenumodel.h"

#include "project/iprojectsession.h"
#include "notation/inotationconfiguration.h"
#include "playback/iplaybackcontroller.h"

#include "muse_framework_config.h"
#ifdef MUSE_MODULE_VST
#include "vst/ivstpluginstateprovider.h"
#endif

namespace mu::notation {
//! NOTE: the buttons acting on one instrument's curves and editors, shared by the circular toolbar and the row of
//! icons at the top of a VST staff's context menu: each runs the same command as the existing button doing the
//! same thing. Built when the menu opens, so their state is the one at that moment
class QuickActionsMenuModel : public muse::uicomponents::AbstractMenuModel
{
    Q_OBJECT
    QML_ELEMENT;
    QML_UNCREATABLE("Not creatable as it is an abstract base class")

public:
    void handleMenuItem(const QString& itemId) override;

protected:
    muse::GlobalInject<INotationConfiguration> notationConfiguration;
    muse::ContextInject<project::IProjectSession> projectSession = { this };
    muse::ContextInject<playback::IPlaybackController> playbackController = { this };
#ifdef MUSE_MODULE_VST
    muse::GlobalInject<muse::vst::IVstPluginStateProvider> vstPluginStateProvider;
#endif

    explicit QuickActionsMenuModel(QObject* parent = nullptr);

    //! NOTE: the instrument the buttons act on, at its position (an instrument change makes it another track)
    virtual std::optional<engraving::InstrumentTrackId> actionsTrackId() const = 0;

    //! NOTE: the MIDI CCs with their own button
    static const std::vector<int>& buttonMidiCcs();

    //! NOTE: the last other MIDI CC picked, -1 if none: shared by all the menus, its button stays the same in each
    static int lastMidiCc();
    static void setLastMidiCc(int controller);
    void updateLastMidiCc();

    muse::uicomponents::MenuItem* makeAutomationItem(const QString& id, const std::string& type, int controller,
                                                     const muse::TranslatableString& title, bool enabled);
    muse::uicomponents::MenuItem* makeMidiCcItem(const QString& id, int controller, bool enabled);
    muse::uicomponents::MenuItem* makeLastMidiCcItem(bool enabled);
    muse::uicomponents::MenuItem* makeArticulationMapEditorItem(bool* hasMap = nullptr);
    muse::uicomponents::MenuItem* makeInstrumentEditorItem();

    //! NOTE: the note editors, the curves and the editors of the instrument, in a row of icons
    muse::uicomponents::MenuItem* makeIconRow(const QString& id);

private:
    bool isInstrumentEditorOpened() const;
};
}
