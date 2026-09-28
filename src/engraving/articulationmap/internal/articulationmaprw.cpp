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

#include "articulationmaprw.h"

#include "engraving/articulationmap/articulationmapdata.h"
#include "engraving/articulationmap/articulationmapparser.h"

#include "global/serialization/json.h"
#include "global/log.h"

using namespace mu::engraving;

static constexpr const char* MAPS_KEY = "maps";
static constexpr const char* MARKS_KEY = "marks";
static constexpr const char* PART_ID_KEY = "partId";
static constexpr const char* INSTRUMENT_ID_KEY = "instrumentId";
static constexpr const char* SOURCE_KEY = "source";
static constexpr const char* SOURCE_PATH_KEY = "sourcePath";
static constexpr const char* NAME_KEY = "name";
static constexpr const char* CHORD_ID_KEY = "chordId";
static constexpr const char* ENTRY_KEY = "entry";
static constexpr const char* SCOPE_KEY = "scope";
static constexpr const char* TICK_OFFSET_KEY = "tickOffset";

static const std::string SCOPE_SINGLE_CHORD = "singleChord";

void ArticulationMapRW::read(ArticulationMapData& data, const muse::ByteArray& json)
{
    TRACEFUNC;

    if (json.empty()) {
        return;
    }

    std::string err;
    const muse::JsonDocument doc = muse::JsonDocument::fromJson(json, &err);
    if (!err.empty() || !doc.isObject()) {
        LOGE() << "Failed to parse articulation maps json: " << err;
        return;
    }

    const muse::JsonObject root = doc.rootObject();

    //! NOTE: maps are stored as their source text, so the parser stays the single source of truth for their format
    const muse::JsonArray mapArray = root.value(MAPS_KEY).toArray();
    for (size_t i = 0; i < mapArray.size(); ++i) {
        const muse::JsonObject mapObj = mapArray.at(i).toObject();

        InstrumentTrackId trackId;
        trackId.partId = muse::ID(mapObj.value(PART_ID_KEY).toString().toStdString());
        trackId.instrumentId = mapObj.value(INSTRUMENT_ID_KEY).toString();
        if (!trackId.isValid()) {
            continue;
        }

        ExpressionMap map = ArticulationMapParser::parse(mapObj.value(SOURCE_KEY).toString()).map;
        map.sourcePath = mapObj.value(SOURCE_PATH_KEY).toString();
        if (map.name.empty()) {
            // e.g. named after its file, when it has no @name line
            map.name = mapObj.value(NAME_KEY).toString();
        }
        data.setMap(trackId, map);
    }

    const muse::JsonArray markArray = root.value(MARKS_KEY).toArray();
    for (size_t i = 0; i < markArray.size(); ++i) {
        const muse::JsonObject markObj = markArray.at(i).toObject();

        const EID chordId = EID::fromStdString(markObj.value(CHORD_ID_KEY).toString().toStdString());
        if (!chordId.isValid()) {
            continue;
        }

        ArticulationMark mark;
        mark.entryId = markObj.value(ENTRY_KEY).toString();
        mark.scope = markObj.value(SCOPE_KEY).toStdString() == SCOPE_SINGLE_CHORD
                     ? ArticulationMark::Scope::SingleChord
                     : ArticulationMark::Scope::Latched;
        mark.tickOffset = markObj.value(TICK_OFFSET_KEY).toInt();

        data.setMark(chordId, mark);
    }
}

muse::ByteArray ArticulationMapRW::write(const ArticulationMapData& data, const MarkFilter& keepMark)
{
    TRACEFUNC;

    muse::JsonArray mapArray;
    for (const auto& [trackId, map] : data.maps()) {
        muse::JsonObject mapObj;
        mapObj[PART_ID_KEY] = trackId.partId.toStdString();
        mapObj[INSTRUMENT_ID_KEY] = trackId.instrumentId;
        mapObj[SOURCE_KEY] = map.sourceText;
        if (!map.name.empty()) {
            mapObj[NAME_KEY] = map.name;
        }
        if (!map.sourcePath.empty()) {
            mapObj[SOURCE_PATH_KEY] = map.sourcePath;
        }
        mapArray << mapObj;
    }

    muse::JsonArray markArray;
    for (const auto& [chordId, mark] : data.marks()) {
        if (keepMark && !keepMark(chordId)) {
            continue;
        }

        muse::JsonObject markObj;
        markObj[CHORD_ID_KEY] = chordId.toStdString();
        markObj[ENTRY_KEY] = mark.entryId;
        if (mark.scope == ArticulationMark::Scope::SingleChord) {
            markObj[SCOPE_KEY] = SCOPE_SINGLE_CHORD;
        }
        if (mark.tickOffset != 0) {
            markObj[TICK_OFFSET_KEY] = mark.tickOffset;
        }
        markArray << markObj;
    }

    muse::JsonObject root;
    root[MAPS_KEY] = mapArray;
    root[MARKS_KEY] = markArray;

    return muse::JsonDocument(root).toJson();
}
