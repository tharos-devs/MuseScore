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

#include "projectaudiosyncsettings.h"

#include <QDir>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonParseError>

#include "types/bytearray.h"

using namespace mu::project;
using namespace muse;

static constexpr int AUDIO_SYNC_SETTINGS_VERSION = 1;

void mu::project::updateReferenceAudio(const IProjectAudioSyncSettingsPtr& settings,
                                       const std::function<void(ReferenceAudioSettings&)>& mutate)
{
    if (!settings || !settings->referenceAudio().isValid()) {
        return;
    }

    ReferenceAudioSettings updated = settings->referenceAudio();
    mutate(updated);
    settings->setReferenceAudio(updated);
}

const ReferenceAudioSettings& ProjectAudioSyncSettings::referenceAudio() const
{
    return m_referenceAudio;
}

void ProjectAudioSyncSettings::setReferenceAudio(const ReferenceAudioSettings& settings)
{
    if (m_referenceAudio == settings) {
        return;
    }

    m_referenceAudio = settings;
    m_settingsChanged.notify();
}

void ProjectAudioSyncSettings::clearReferenceAudio()
{
    setReferenceAudio(ReferenceAudioSettings());
}

muse::async::Notification ProjectAudioSyncSettings::settingsChanged() const
{
    return m_settingsChanged;
}

muse::Ret ProjectAudioSyncSettings::read(const engraving::MscReader& reader, const muse::io::path_t& projectPath)
{
    makeDefault();

    const ByteArray json = reader.readAudioSyncJsonFile();
    if (json.empty()) {
        return make_ret(Ret::Code::Ok);
    }

    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(json.toQByteArrayNoCopy(), &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
        return make_ret(Ret::Code::Ok);
    }

    const QJsonObject rootObj = document.object();
    if (rootObj.value("version").toInt(0) != AUDIO_SYNC_SETTINGS_VERSION) {
        return make_ret(Ret::Code::Ok);
    }

    const QJsonObject audioObj = rootObj.value("referenceAudio").toObject();
    QString path = audioObj.value("path").toString();
    if (path.isEmpty()) {
        return make_ret(Ret::Code::Ok);
    }

    //! NOTE The absolute path first; then the relative one (score and audio moved together);
    //! a missing file keeps its absolute path, so the window can offer to locate it
    if (!QFileInfo::exists(path) && !projectPath.empty()) {
        const QDir projectDir = QFileInfo(projectPath.toQString()).absoluteDir();
        const QString relativePath = audioObj.value("relativePath").toString();
        const QString candidates[] = {
            relativePath.isEmpty() ? QString() : QDir::cleanPath(projectDir.absoluteFilePath(relativePath)),
            projectDir.absoluteFilePath(QFileInfo(path).fileName()),
        };

        for (const QString& candidate : candidates) {
            if (!candidate.isEmpty() && QFileInfo::exists(candidate)) {
                path = candidate;
                break;
            }
        }
    }

    m_referenceAudio.path = path;
    m_referenceAudio.muted = audioObj.value("muted").toBool(false);
    m_referenceAudio.solo = audioObj.value("solo").toBool(false);
    m_referenceAudio.includeInExport = audioObj.value("includeInExport").toBool(false);
    m_referenceAudio.showStereo = audioObj.value("showStereo").toBool(true);

    return make_ret(Ret::Code::Ok);
}

muse::Ret ProjectAudioSyncSettings::write(engraving::MscWriter& writer, const muse::io::path_t& projectPath) const
{
    if (!m_referenceAudio.isValid()) {
        return make_ret(Ret::Code::Ok);
    }

    QJsonObject audioObj;
    audioObj["path"] = m_referenceAudio.path.toQString();
    if (!projectPath.empty()) {
        const QDir projectDir = QFileInfo(projectPath.toQString()).absoluteDir();
        audioObj["relativePath"] = projectDir.relativeFilePath(m_referenceAudio.path.toQString());
    }
    audioObj["muted"] = m_referenceAudio.muted;
    audioObj["solo"] = m_referenceAudio.solo;
    audioObj["includeInExport"] = m_referenceAudio.includeInExport;
    audioObj["showStereo"] = m_referenceAudio.showStereo;

    QJsonObject rootObj;
    rootObj["version"] = AUDIO_SYNC_SETTINGS_VERSION;
    rootObj["referenceAudio"] = audioObj;

    const QByteArray json = QJsonDocument(rootObj).toJson();
    writer.writeAudioSyncJsonFile(ByteArray::fromQByteArrayNoCopy(json));

    return make_ret(Ret::Code::Ok);
}

void ProjectAudioSyncSettings::makeDefault()
{
    m_referenceAudio = ReferenceAudioSettings();
}
