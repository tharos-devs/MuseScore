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

#include <memory>

#include "async/notification.h"

#include "engraving/articulationmap/articulationmapdata.h"
#include "engraving/articulationmap/articulationmaptypes.h"

namespace mu::notation {
using ExpressionMap = mu::engraving::ExpressionMap;
using ExpressionMapEntry = mu::engraving::ExpressionMapEntry;
using ArticulationMark = mu::engraving::ArticulationMark;
using ArticulationMapData = mu::engraving::ArticulationMapData;
using ArticulationMapDataConstPtr = mu::engraving::ArticulationMapDataConstPtr;
using EditArticulationMapChanges = mu::engraving::EditArticulationMapChanges;

//! NOTE: articulation maps (keyswitch/CC articulation changes for third-party VST instruments) of a master notation
class INotationArticulationMaps
{
public:
    virtual ~INotationArticulationMaps() = default;

    virtual bool isOverlayEnabled() const = 0;
    virtual void setOverlayEnabled(bool enabled) = 0;
    virtual muse::async::Notification overlayEnabledChanged() const = 0;

    virtual ArticulationMapDataConstPtr data() const = 0;

    //! NOTE: undoable
    virtual void edit(const EditArticulationMapChanges& changes, const muse::TranslatableString& actionName) = 0;
};

using INotationArticulationMapsPtr = std::shared_ptr<INotationArticulationMaps>;
}
