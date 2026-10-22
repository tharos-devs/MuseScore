/*
 * SPDX-License-Identifier: GPL-3.0-only
 * MuseScore-CLA-applies
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

#include "referenceaudiofile.h"

#include <cmath>

#include <QFileInfo>
#include <QStringList>

#include "notation/imasternotation.h"
#include "notation/inotationautomation.h"
#include "project/iprojectundostack.h"

#include "translation.h"

using namespace muse;

static const QStringList AUDIO_FILE_SUFFIXES {
    "wav", "wave", "aif", "aiff", "mp3", "flac", "ogg", "oga", "opus", "m4a", "aac", "wma",
    // The audio of a video file is fine too
    "mp4", "mov", "m4v", "mkv", "webm", "avi"
};

bool mu::playback::audiosync::isSupportedAudioFile(const muse::io::path_t& path)
{
    return AUDIO_FILE_SUFFIXES.contains(QFileInfo(path.toQString()).suffix().toLower());
}

bool mu::playback::audiosync::chooseReferenceAudio(IInteractive* interactive, IPlaybackConfiguration* configuration,
                                                   IPlaybackController* controller, const project::INotationProjectPtr& project)
{
    const project::IProjectAudioSyncSettingsPtr settings = project ? project->audioSyncSettings() : nullptr;
    if (!interactive || !settings) {
        return false;
    }

    QStringList patterns;
    for (const QString& suffix : AUDIO_FILE_SUFFIXES) {
        patterns << "*." + suffix;
    }

    const std::vector<std::string> filter {
        muse::trc("playback", "Audio files") + " (" + patterns.join(' ').toStdString() + ")",
        muse::trc("playback", "All files") + " (*)"
    };

    const io::path_t currentPath = settings->referenceAudio().path;
    const io::path_t path = interactive->selectOpeningFileSync(muse::trc("playback", "Choose reference audio"),
                                                               currentPath.empty() ? io::path_t() : io::dirpath(currentPath),
                                                               filter);
    if (path.empty()) {
        return false;
    }

    setReferenceAudioPath(configuration, controller, project, path);
    return true;
}

void mu::playback::audiosync::setReferenceAudioPath(IPlaybackConfiguration* configuration, IPlaybackController* controller,
                                                    const project::INotationProjectPtr& project, const muse::io::path_t& path)
{
    const project::IProjectAudioSyncSettingsPtr settings = project ? project->audioSyncSettings() : nullptr;
    if (!settings || path.empty()) {
        return;
    }

    if (configuration) {
        configuration->addRecentReferenceAudioFile(path.toQString());
    }

    const project::ReferenceAudioSettings& current = settings->referenceAudio();
    if (current.path == path) {
        return;
    }

    //! NOTE The same recording located again (moved or renamed): its sync points and settings still apply
    if (!current.path.empty() && !QFileInfo::exists(current.path.toQString())) {
        project::ReferenceAudioSettings located = current;
        located.path = path;
        settings->setReferenceAudio(located);
        return;
    }

    startOver(controller, project, path);
}

void mu::playback::audiosync::startOver(IPlaybackController* controller, const project::INotationProjectPtr& project,
                                        const muse::io::path_t& newPath)
{
    const project::IProjectAudioSyncSettingsPtr settings = project ? project->audioSyncSettings() : nullptr;
    if (!settings) {
        return;
    }

    if (const notation::INotationAutomationPtr automation = project->masterNotation() ? project->masterNotation()->automation() : nullptr) {
        //! NOTE On, it stays on: its points made again from the score's tempo, from 00:00 (a point on every measure)
        automation->clearAudioSync(/*keepEnabled*/ automation->isAudioSyncEnabled() && !newPath.empty());
    }

    //! NOTE The pitch, delay and loop are the reference audio's settings; the next audio is fitted in the view
    project::ReferenceAudioSettings fresh;
    fresh.path = newPath;
    const project::ReferenceAudioSettings previous = settings->referenceAudio();
    const project::AudioSyncViewState previousViewState = settings->viewState();
    settings->setViewState(project::AudioSyncViewState());
    settings->setReferenceAudio(fresh);

    //! NOTE Undone after the sync points (cleared just before, on the score's stack): the previous audio back, then
    //! its points
    if (const project::IProjectUndoStackPtr undoStack = project->undoStack()) {
        const std::weak_ptr<project::IProjectAudioSyncSettings> weakSettings = settings;
        undoStack->push(muse::TranslatableString("undoableAction", "Change reference audio"), [weakSettings, fresh]() {
            if (const auto settings = weakSettings.lock()) {
                settings->setViewState(project::AudioSyncViewState());
                settings->setReferenceAudio(fresh);
            }
        }, [weakSettings, previous, previousViewState]() {
            if (const auto settings = weakSettings.lock()) {
                settings->setViewState(previousViewState);
                settings->setReferenceAudio(previous);
            }
        });
    }

    if (controller && std::abs(controller->tempoMultiplier() - 1.0) > 1e-6) {
        controller->setTempoMultiplier(1.0);
    }
}
