/*
 * SPDX-License-Identifier: GPL-3.0-only
 * MuseScore-Studio-CLA-applies
 *
 * MuseScore Studio
 * Music Composition & Notation
 *
 * Copyright (C) MuseScore Limited and others
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

#include "appshellcommandsstate.h"

#include "appshelltypes.h"
#include "global/containers.h"

#include "dockwindow/idockwindow.h"

#include "../appshellcommands.h"
#include "project/inotationproject.h"
#include "project/iprojectvideosettings.h"
#include <vector>

using namespace muse;
using namespace muse::rcommand;
using namespace muse::dock;
using namespace mu::appshell;

static const muse::Uri PROJECT_PAGE_URI("musescore://notation");

static const std::vector<Command> PROJECT_PAGE_COMMANDS = {
    DOCK_TOGGLE_PLAYBACK_COMMAND,
    DOCK_TOGGLE_NOTEINPUT_COMMAND,
    DOCK_TOGGLE_PALETTES_COMMAND,
    DOCK_TOGGLE_SECONDARY_PALETTES_COMMAND,
    DOCK_TOGGLE_INSTRUMENTS_COMMAND,
    DOCK_TOGGLE_PROPERTIES_COMMAND,
    DOCK_TOGGLE_SELECTION_FILTER_COMMAND,
    DOCK_TOGGLE_UNDO_HISTORY_COMMAND,
    DOCK_TOGGLE_NAVIGATOR_COMMAND,
    DOCK_TOGGLE_BRAILLE_COMMAND,
    DOCK_TOGGLE_TIMELINE_COMMAND,
    DOCK_TOGGLE_MIXER_COMMAND,
    DOCK_TOGGLE_VIDEO_PANEL_COMMAND,
    DOCK_TOGGLE_TRACK_LIST_COMMAND,
    DOCK_TOGGLE_AUDIO_SYNC_PANEL_COMMAND,
    DOCK_TOGGLE_PIANO_KEYBOARD_COMMAND,
    DOCK_TOGGLE_PERCUSSION_COMMAND,
    DOCK_TOGGLE_STATUSBAR_COMMAND,
    DOCK_TOGGLE_SECONDARY_WINDOW_COMMAND,
};

static const std::vector<Command> VIDEO_COMMANDS = {
    VIDEO_TIMECODE_OFF_COMMAND,
    VIDEO_TIMECODE_ABOVE_BARS_COMMAND,
    VIDEO_TIMECODE_BELOW_BARS_COMMAND,
    SHOW_VIDEO_HITPOINTS_COMMAND,
};

std::string AppshellCommandsState::moduleName() const
{
    return "appshell";
}

void AppshellCommandsState::init()
{
    m_moduleRegister = commandsRegister()->moduleRegister(moduleName());
    IF_ASSERT_FAILED(m_moduleRegister) {
        return;
    }

    mainWindow()->isFullScreenChanged().onNotify(this, [this]() {
        updateCommandStates({ APP_FULLSCREEN_COMMAND });
    });

    appShellState()->isNotationNavigatorVisibleChanged().onNotify(this, [this]() {
        updateCommandStates({ DOCK_TOGGLE_NAVIGATOR_COMMAND });
    });

    brailleConfiguration()->braillePanelEnabledChanged().onNotify(this, [this]() {
        updateCommandStates({ DOCK_TOGGLE_BRAILLE_COMMAND });
    });

    dockWindowProvider()->windowChanged().onNotify(this, [this]() {
        const IDockWindow* window = dockWindowProvider()->window();
        if (!window) {
            return;
        }

        window->secondaryWindowOpenChanged().onNotify(this, [this]() {
            updateCommandStates({ DOCK_TOGGLE_SECONDARY_WINDOW_COMMAND });
        });

        window->docksOpenStatusChanged().onReceive(this, [this](const QStringList& dockNames) {
            std::vector<Command> commands;
            commands.reserve(dockNames.size());
            for (const QString& dockName : dockNames) {
                Command cmd = commandsController()->dockToggleCommand(dockName);
                if (cmd.isValid()) {
                    commands.push_back(std::move(cmd));
                }
            }

            updateCommandStates({ commands });
        });
    });

    interactive()->currentUri().ch.onReceive(this, [this](const muse::Uri&) {
        updateCommandStates(PROJECT_PAGE_COMMANDS);
    });

    projectSession()->projectChanged().onNotify(this, [this]() {
        listenProjectVideoSettings();
        updateCommandStates(VIDEO_COMMANDS);
    });

    listenProjectVideoSettings();
    updateCommandStates();
}

void AppshellCommandsState::deinit()
{
    mainWindow()->isFullScreenChanged().disconnect(this);
}

void AppshellCommandsState::updateCommandStates(const std::vector<Command>& commands)
{
    IF_ASSERT_FAILED(m_moduleRegister) {
        return;
    }

    const auto& commandList = commands.empty() ? m_moduleRegister->commandList() : commands;

    for (const auto& command : commandList) {
        CommandState newState = commandState(command);
        if (m_commandStates[command] != newState) {
            m_commandStates[command] = newState;
            m_commandStateChanged.send(command, newState);
        }
    }
}

CommandState AppshellCommandsState::commandState(const Command& command) const
{
    if (command == APP_FULLSCREEN_COMMAND) {
        return CommandState(true, mainWindow()->isFullScreen());
    }

    if (muse::contains(PROJECT_PAGE_COMMANDS, command)) {
        if (!isProjectPage()) {
            return CommandState(false, false);
        }
    }

    if (command == DOCK_TOGGLE_NAVIGATOR_COMMAND) {
        return CommandState(true, appShellState()->isNotationNavigatorVisible());
    }

    if (command == DOCK_TOGGLE_BRAILLE_COMMAND) {
        return CommandState(true, brailleConfiguration()->braillePanelEnabled());
    }

    if (muse::contains(VIDEO_COMMANDS, command)) {
        return videoCommandState(command);
    }

    if (command == DOCK_TOGGLE_SECONDARY_WINDOW_COMMAND) {
        const IDockWindow* window = dockWindowProvider()->window();
        return CommandState(true, window && window->isSecondaryWindowOpen());
    }

    const DockName dockName = commandsController()->commandDockName(command);
    if (!dockName.isEmpty()) {
        const IDockWindow* window = dockWindowProvider()->window();
        bool opened = window ? window->isDockOpen(dockName) : false;
        return CommandState(true, opened);
    }

    return CommandState(true, false);
}

async::Channel<Command, CommandState> AppshellCommandsState::commandStateChanged() const
{
    return m_commandStateChanged;
}

bool AppshellCommandsState::isProjectPage() const
{
    if (!interactive()) {
        return false;
    }

    return interactive()->isOpened(PROJECT_PAGE_URI).val;
}

void AppshellCommandsState::listenProjectVideoSettings()
{
    mu::project::INotationProjectPtr project = projectSession()->project();
    mu::project::IProjectVideoSettingsPtr settings = project ? project->videoSettings() : nullptr;
    if (!settings) {
        return;
    }

    settings->settingsChanged().onNotify(this, [this]() {
        updateCommandStates(VIDEO_COMMANDS);
    }, Asyncable::Mode::SetReplace);
}

CommandState AppshellCommandsState::videoCommandState(const Command& command) const
{
    mu::project::INotationProjectPtr project = projectSession()->project();
    mu::project::IProjectVideoSettingsPtr settings = project ? project->videoSettings() : nullptr;
    if (!settings) {
        return CommandState(false, command == VIDEO_TIMECODE_OFF_COMMAND || command == SHOW_VIDEO_HITPOINTS_COMMAND);
    }

    if (command == SHOW_VIDEO_HITPOINTS_COMMAND) {
        return CommandState(true, settings->attachment().showHitPoints);
    }

    const mu::project::VideoTimecodeDisplayMode mode = settings->attachment().timecodeDisplayMode;
    if (command == VIDEO_TIMECODE_ABOVE_BARS_COMMAND) {
        return CommandState(true, mode == mu::project::VideoTimecodeDisplayMode::AboveBars);
    }

    if (command == VIDEO_TIMECODE_BELOW_BARS_COMMAND) {
        return CommandState(true, mode == mu::project::VideoTimecodeDisplayMode::BelowBars);
    }

    return CommandState(true, mode == mu::project::VideoTimecodeDisplayMode::Off);
}
