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
#include <optional>
#include <unordered_map>
#include <vector>

#include "global/types/string.h"
#include "mpe/events.h"
#include "mpe/mpetypes.h"

#include "engraving/infrastructure/eid.h"
#include "engraving/types/types.h"

namespace mu::engraving {
//! NOTE: one articulation of a sample library, and the raw MIDI messages that select it
struct ExpressionMapEntry {
    muse::String id; // full label path, e.g. "Legato > Fast"
    std::vector<muse::mpe::MidiMessage> messages;
    std::vector<muse::mpe::ArticulationType> aliases; // score articulations that select this entry automatically
    std::optional<int> keyswitchOffsetMs; // overrides ExpressionMap::keyswitchOffsetMs
    int notesOffsetMs = 0; // shifts the notes played with this articulation (e.g. slow legato attacks)
    std::optional<uint32_t> color; // 0xRRGGBB, how the articulation is shown in the score
    bool disabled = false; // kept in the file, but never offered nor sent

    bool operator==(const ExpressionMapEntry& e) const
    {
        return id == e.id && messages == e.messages && aliases == e.aliases
               && keyswitchOffsetMs == e.keyswitchOffsetMs && notesOffsetMs == e.notesOffsetMs && color == e.color
               && disabled == e.disabled;
    }
};

//! NOTE: the articulations of one sample library, parsed from a text file (see ArticulationMapParser)
struct ExpressionMap {
    muse::String name;
    muse::String sourceText; // kept verbatim, so the score stays playable without the original file
    muse::String sourcePath; // where it was loaded from, to reload it after the file is edited
    int keyswitchOffsetMs = 0; // how long before its note an articulation change is sent
    muse::String defaultEntryId;
    std::vector<ExpressionMapEntry> entries;

    //! NOTE: a disabled entry is not found, so whatever selects it falls back to the next choice
    const ExpressionMapEntry* entry(const muse::String& id) const
    {
        for (const ExpressionMapEntry& e : entries) {
            if (e.id == id) {
                return e.disabled ? nullptr : &e;
            }
        }

        return nullptr;
    }

    //! NOTE: entry order is the priority order when a chord has several score articulations
    const ExpressionMapEntry* entryForArticulations(const muse::mpe::ArticulationMap& articulations) const
    {
        for (const ExpressionMapEntry& e : entries) {
            if (e.disabled) {
                continue;
            }

            for (const muse::mpe::ArticulationType type : e.aliases) {
                if (articulations.contains(type)) {
                    return &e;
                }
            }
        }

        return nullptr;
    }

    int keyswitchOffsetMsFor(const ExpressionMapEntry& e) const
    {
        return e.keyswitchOffsetMs.value_or(keyswitchOffsetMs);
    }

    bool operator==(const ExpressionMap& m) const
    {
        return name == m.name && sourceText == m.sourceText && sourcePath == m.sourcePath && keyswitchOffsetMs == m.keyswitchOffsetMs
               && defaultEntryId == m.defaultEntryId && entries == m.entries;
    }
};

using ExpressionMapPtr = std::shared_ptr<const ExpressionMap>;

//! NOTE: a manual articulation change, anchored to the chord it applies to
struct ArticulationMark {
    enum class Scope : unsigned char {
        Latched = 0, // stays active until the next mark
        SingleChord, // applies to its chord only, then the previous articulation resumes
    };

    muse::String entryId;
    Scope scope = Scope::Latched;
    int tickOffset = 0; // fine adjustment of when the change is sent, relative to the chord

    bool operator==(const ArticulationMark& m) const
    {
        return entryId == m.entryId && scope == m.scope && tickOffset == m.tickOffset;
    }
};

//! NOTE: which articulation a chord ends up playing, and why
struct ResolvedArticulation {
    enum class Source : unsigned char {
        OwnMark = 0, // a mark on the chord itself
        Alias, // a score articulation of the chord (e.g. a staccato dot)
        Latched, // an earlier latched mark of the staff
        Default, // the map's default articulation
    };

    muse::String entryId;
    Source source = Source::Default;
};

using ExpressionMapsByTrack = std::unordered_map<InstrumentTrackId, ExpressionMap>;
using ArticulationMarksByChord = std::unordered_map<EID, ArticulationMark>;

//! NOTE: sets (or removes, with nullopt) articulation maps and/or marks, in one undoable step
struct EditArticulationMapChanges {
    std::unordered_map<InstrumentTrackId, std::optional<ExpressionMap> > maps;
    std::unordered_map<EID, std::optional<ArticulationMark> > marks;

    bool empty() const { return maps.empty() && marks.empty(); }
};
}
