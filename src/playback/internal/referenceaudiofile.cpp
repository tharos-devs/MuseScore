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

#include <QFileInfo>
#include <QStringList>

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
                                                   const project::IProjectAudioSyncSettingsPtr& settings)
{
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

    setReferenceAudioPath(configuration, settings, path);
    return true;
}

void mu::playback::audiosync::setReferenceAudioPath(IPlaybackConfiguration* configuration,
                                                    const project::IProjectAudioSyncSettingsPtr& settings, const muse::io::path_t& path)
{
    if (!settings || path.empty()) {
        return;
    }

    if (configuration) {
        configuration->addRecentReferenceAudioFile(path.toQString());
    }

    project::ReferenceAudioSettings updated = settings->referenceAudio();
    updated.path = path;
    settings->setReferenceAudio(updated);
}
