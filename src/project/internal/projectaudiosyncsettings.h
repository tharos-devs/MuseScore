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

#pragma once

#include <QJsonObject>

#include "types/ret.h"
#include "engraving/infrastructure/mscreader.h"
#include "engraving/infrastructure/mscwriter.h"

#include "../iprojectaudiosyncsettings.h"

namespace mu::project {
class ProjectAudioSyncSettings : public IProjectAudioSyncSettings
{
public:
    const ReferenceAudioSettings& referenceAudio() const override;
    void setReferenceAudio(const ReferenceAudioSettings& settings) override;
    void clearReferenceAudio() override;

    muse::async::Notification settingsChanged() const override;

    const AudioSyncViewState& viewState() const override;
    void setViewState(const AudioSyncViewState& state) override;
    muse::async::Notification viewStateChanged() const override;

    //! NOTE projectPath: the score's file, to resolve the audio's path relative to it
    muse::Ret read(const engraving::MscReader& reader, const muse::io::path_t& projectPath);
    muse::Ret write(engraving::MscWriter& writer, const muse::io::path_t& projectPath) const;

    void makeDefault();

private:
    ReferenceAudioSettings m_referenceAudio;
    AudioSyncViewState m_viewState;
    muse::async::Notification m_viewStateChanged;
    muse::async::Notification m_settingsChanged;
};

using ProjectAudioSyncSettingsPtr = std::shared_ptr<ProjectAudioSyncSettings>;
}
