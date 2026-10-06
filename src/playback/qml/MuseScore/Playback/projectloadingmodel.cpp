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

#include "projectloadingmodel.h"

#include <algorithm>

#include "async/notifylist.h"

#include "engraving/dom/part.h"
#include "notation/imasternotation.h"
#include "notation/inotationparts.h"
#include "project/inotationproject.h"
#include "project/iprojectaudiosettings.h"

using namespace mu::playback;

//! NOTE: how long everything must have been loaded, with the application responsive, before the window closes:
//! there are short gaps between two loading steps (e.g. the audio tracks, then the plugins' states)
static constexpr qint64 LOADED_FOR_MS = 500;
static constexpr int CHECK_INTERVAL_MS = 50;
//! NOTE: a check later than this means the main thread was blocked: not responsive yet
static constexpr qint64 BLOCKED_MS = 150;
//! NOTE: in case a loading step never reports its end
static constexpr qint64 TIMEOUT_MS = 180000;

ProjectLoadingModel::ProjectLoadingModel(QObject* parent)
    : QObject(parent), muse::Contextable(muse::iocCtxForQmlObject(this))
{
}

void ProjectLoadingModel::load()
{
    pluginsMainThreadTasks()->taskStarting().onReceive(this, [this](const std::string& pluginName) {
        m_currentPluginName = pluginName;
        updateMessage();
    });

    //! NOTE: the message changes when the next task starts: until then, it's still about this plugin
    pluginsMainThreadTasks()->pluginLoaded().onReceive(this, [this](const std::string&) {
        ++m_loadedPluginCount;
    });

    m_openedFor.start();
    m_sinceLastCheck.start();

    m_checkTimer.setInterval(CHECK_INTERVAL_MS);
    connect(&m_checkTimer, &QTimer::timeout, this, &ProjectLoadingModel::checkFinished);
    m_checkTimer.start();
}

QString ProjectLoadingModel::message() const
{
    return m_message;
}

//! NOTE: the VST3 instruments and effects of the project's tracks, aux buses, video and master: each one is loaded
//! and its saved state restored on the main thread, the long part of a project's loading
size_t ProjectLoadingModel::projectPluginCount() const
{
    const project::INotationProjectPtr project = context()->currentProject();
    if (!project) {
        return 0;
    }

    const project::IProjectAudioSettingsPtr settings = project->audioSettings();

    size_t count = 0;
    auto countFx = [&count](const muse::audio::AudioFxChain& chain) {
        for (const auto& [order, fx] : chain) {
            if (fx.type() == muse::audio::AudioFxType::VstFx) {
                ++count;
            }
        }
    };

    for (const engraving::Part* part : project->masterNotation()->parts()->partList()) {
        for (const engraving::InstrumentTrackId& trackId : part->instrumentTrackIdList()) {
            if (settings->trackInputParams(trackId).type() == muse::audio::AudioSourceType::Vsti) {
                ++count;
            }
            if (settings->trackHasExistingOutputParams(trackId)) {
                countFx(settings->trackOutputParams(trackId).fxChain);
            }
        }
    }

    for (muse::audio::aux_channel_idx_t index : settings->auxOutputParamsIndices()) {
        countFx(settings->auxOutputParams(index).fxChain);
    }

    countFx(settings->masterAudioOutputParams().fxChain);
    countFx(playbackController()->videoOutputParams().fxChain);

    return count;
}

void ProjectLoadingModel::updateMessage()
{
    if (!m_pluginCount && context()->currentProject()) {
        m_pluginCount = projectPluginCount();
    }

    if (!m_pluginCount || *m_pluginCount == 0 || m_currentPluginName.empty()) {
        return;
    }

    //! NOTE: the plugin being loaded is the one after those already loaded
    const size_t total = *m_pluginCount;
    const size_t current = std::min(m_loadedPluginCount + 1, total);

    m_message = QString("%1/%2 %3").arg(current).arg(total).arg(QString::fromStdString(m_currentPluginName));
    emit messageChanged();
}

bool ProjectLoadingModel::isLoading() const
{
    if (!context()->currentProject()) {
        return true;
    }

    //! NOTE: started when the project becomes the current one, finished once all its audio tracks are loaded.
    //! Without a working audio engine (e.g. no audio device), the playback is never set up: nothing to wait for
    const bool waitsForAudio = audioPlayback()->isInited();
    if (waitsForAudio && (playbackController()->loadingProgress().isStarted() || !playbackController()->isPlaybackInited())) {
        return true;
    }

    return pluginsMainThreadTasks()->isBusy();
}

void ProjectLoadingModel::checkFinished()
{
    const bool blocked = m_sinceLastCheck.restart() > BLOCKED_MS;

    if (m_openedFor.elapsed() > TIMEOUT_MS) {
        m_checkTimer.stop();
        emit loadingFinished();
        return;
    }

    if (blocked || isLoading()) {
        m_loadedFor.invalidate();
        return;
    }

    if (!m_loadedFor.isValid()) {
        m_loadedFor.start();
        return;
    }

    if (m_loadedFor.elapsed() >= LOADED_FOR_MS) {
        m_checkTimer.stop();
        emit loadingFinished();
    }
}
