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
#include "articulationmapwriter.h"

#include "mpe/internal/articulationstringutils.h"

using namespace mu::engraving;
using namespace muse;

String ArticulationMapWriter::noteName(int pitch, int middleCOctave)
{
    static const char* NAMES[] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };

    const int pitchClass = ((pitch % 12) + 12) % 12;
    const int octave = (pitch - pitchClass - 60) / 12 + middleCOctave;

    return String::fromAscii(NAMES[pitchClass]) + String::number(octave);
}

String ArticulationMapWriter::messagesCode(const std::vector<mpe::MidiMessage>& messages, int middleCOctave)
{
    String code;

    for (const mpe::MidiMessage& message : messages) {
        switch (message.type) {
        case mpe::MidiMessage::Type::Note:
            code += noteName(message.number, middleCOctave);
            if (message.value != ArticulationMapParser::DEFAULT_KEYSWITCH_VELOCITY) {
                code += u"v" + String::number(message.value);
            }
            break;
        case mpe::MidiMessage::Type::ControlChange:
            code += u"cc" + String::number(message.number) + u"=" + String::number(message.value);
            break;
        case mpe::MidiMessage::Type::ProgramChange:
            code += u"pc" + String::number(message.number);
            break;
        }
    }

    return code;
}

static String entryLine(const ExpressionMapEntry& entry, bool isDefault, int middleCOctave)
{
    //! NOTE: an articulation that only changes the channel has the channel as its code
    const bool channelOnly = entry.messages.empty() && entry.channel;
    const String channel = entry.channel ? u"ch=" + String::number(*entry.channel + 1) : String();
    const String code = channelOnly ? channel : ArticulationMapWriter::messagesCode(entry.messages, middleCOctave);
    String line = String(isDefault ? u"*" : u"") + (entry.disabled ? u"-" : u"") + code + u" " + entry.id;

    if (entry.aliases != ArticulationMapParser::implicitAliases(entry.id)) {
        StringList names;
        for (const mpe::ArticulationType type : entry.aliases) {
            names << String::fromQString(mpe::ARTICULATION_TYPE_NAMES.at(type)).toLower();
        }
        line += u" = " + names.join(u", ");
    }

    if (entry.keyswitchOffsetMs) {
        line += u" ks=" + String::number(*entry.keyswitchOffsetMs) + u"ms";
    }

    if (entry.notesOffsetMs != 0) {
        line += u" delay=" + String::number(entry.notesOffsetMs) + u"ms";
    }

    if (entry.channel && !channelOnly) {
        line += u" " + channel;
    }

    if (entry.color) {
        char hex[8];
        std::snprintf(hex, sizeof(hex), "%06x", *entry.color & 0xFFFFFF);
        line += u" color=#" + String::fromAscii(hex);
    }

    return line;
}

String ArticulationMapWriter::write(const ArticulationMapParser::Result& file)
{
    const ExpressionMap& map = file.map;
    String text;

    if (!map.name.empty()) {
        text += u"@name " + map.name + u"\n";
    }

    text += u"@middlec C" + String::number(file.middleCOctave) + u"\n";

    if (map.keyswitchOffsetMs != 0) {
        text += u"@offset " + String::number(map.keyswitchOffsetMs) + u"ms\n";
    }

    text += u"\n";

    size_t folderIdx = 0;
    auto writeFoldersBefore = [&](size_t entryIdx) {
        while (folderIdx < file.folders.size() && file.folders.at(folderIdx).entryIndex <= entryIdx) {
            text += u"@folder " + file.folders.at(folderIdx).path + u"\n";
            ++folderIdx;
        }
    };

    for (size_t i = 0; i < map.entries.size(); ++i) {
        writeFoldersBefore(i);

        const ExpressionMapEntry& entry = map.entries.at(i);
        text += entryLine(entry, entry.id == map.defaultEntryId, file.middleCOctave) + u"\n";
    }

    writeFoldersBefore(map.entries.size());

    return text;
}
