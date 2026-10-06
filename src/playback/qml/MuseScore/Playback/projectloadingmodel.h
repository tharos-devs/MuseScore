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

#include <QElapsedTimer>
#include <QObject>
#include <QTimer>
#include <qqmlintegration.h>

#include "async/asyncable.h"
#include "modularity/ioc.h"

#include "audio/main/iplayback.h"
#include "audioplugins/iaudiopluginsmainthreadtasks.h"
#include "context/iglobalcontext.h"
#include "playback/iplaybackcontroller.h"

namespace mu::playback {
//! NOTE: the project loading window's state: what's loading (the plugin being loaded, e.g. "3/9 <plugin name>": the
//! 3rd of the project's 9 VST3 plugins), and when everything is: the project is open, its audio loaded (unless the
//! audio engine couldn't start), no plugin work left, and the application responsive again (some loading blocks the
//! main thread, see IAudioPluginsMainThreadTasks)
class ProjectLoadingModel : public QObject, public muse::async::Asyncable, public muse::Contextable
{
    Q_OBJECT

    Q_PROPERTY(QString message READ message NOTIFY messageChanged)

    QML_ELEMENT

    muse::GlobalInject<muse::audioplugins::IAudioPluginsMainThreadTasks> pluginsMainThreadTasks;
    muse::ContextInject<muse::audio::IPlayback> audioPlayback = { this };
    muse::ContextInject<IPlaybackController> playbackController = { this };
    muse::ContextInject<context::IGlobalContext> context = { this };

public:
    explicit ProjectLoadingModel(QObject* parent = nullptr);

    Q_INVOKABLE void load();

    QString message() const;

signals:
    void messageChanged();
    void loadingFinished();

private:
    void checkFinished();
    bool isLoading() const;
    void updateMessage();
    size_t projectPluginCount() const;

    QString m_message;
    std::string m_currentPluginName;
    size_t m_loadedPluginCount = 0;
    //! NOTE: counted once the project is the current one
    std::optional<size_t> m_pluginCount;

    QTimer m_checkTimer;
    QElapsedTimer m_sinceLastCheck;
    QElapsedTimer m_loadedFor;
    QElapsedTimer m_openedFor;
};
}
