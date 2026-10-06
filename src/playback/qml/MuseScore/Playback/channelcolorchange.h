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

#include <QColor>

#include "types/translatablestring.h"

#include "audio/common/audiotypes.h"
#include "engraving/types/types.h"
#include "project/iprojectaudiosettings.h"
#include "project/iprojectundostack.h"

namespace mu::playback {
//! NOTE: an undoable color change of instrument tracks and aux buses (Mixer, Track list). Made on the project's
//! audio settings, which the panels follow, never on a panel's channel items: the panel may be closed (its items
//! gone) by the time of an undo
class ChannelColorChange
{
public:
    struct Target {
        engraving::InstrumentTrackId instrumentTrackId;
        std::optional<muse::audio::aux_channel_idx_t> auxIndex; //! NOTE: set for an aux bus
    };

    static void apply(const project::IProjectAudioSettingsPtr& settings, const project::IProjectUndoStackPtr& undoStack,
                      const std::vector<Target>& targets, const QColor& color, const muse::TranslatableString& actionName);
};
}
