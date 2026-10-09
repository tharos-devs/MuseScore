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

#include "articulationmapparser.h"

#include <algorithm>
#include <cctype>
#include <charconv>
#include <cstdlib>
#include <regex>

#include "mpe/internal/articulationstringutils.h"

using namespace mu::engraving;
using namespace muse;

static std::string trimmed(const std::string& str)
{
    const size_t first = str.find_first_not_of(" \t\r");
    if (first == std::string::npos) {
        return std::string();
    }

    const size_t last = str.find_last_not_of(" \t\r");
    return str.substr(first, last - first + 1);
}

static std::string lowered(std::string str)
{
    for (char& c : str) {
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    }

    return str;
}

static std::vector<std::string> split(const std::string& str, char separator)
{
    std::vector<std::string> result;
    size_t start = 0;

    while (true) {
        const size_t pos = str.find(separator, start);
        result.push_back(str.substr(start, pos == std::string::npos ? std::string::npos : pos - start));
        if (pos == std::string::npos) {
            break;
        }
        start = pos + 1;
    }

    return result;
}

//! NOTE: never throws, unlike std::stoi - a user-written file can hold any number of digits
static constexpr int MAX_ABS_NUMBER = 100000;

static std::optional<int> toInt(const std::string& str)
{
    int value = 0;
    const char* first = str.data();
    const char* last = str.data() + str.size();
    const std::from_chars_result result = std::from_chars(first, last, value);
    if (result.ec != std::errc() || result.ptr != last || std::abs(value) > MAX_ABS_NUMBER) {
        return std::nullopt;
    }

    return value;
}

static bool parseMs(const std::string& str, int& ms)
{
    static const std::regex MS_REGEX(R"(^\s*(-?\d+)\s*(ms)?\s*$)", std::regex::icase);

    std::smatch match;
    if (!std::regex_match(str, match, MS_REGEX)) {
        return false;
    }

    const std::optional<int> value = toInt(match[1].str());
    if (!value) {
        return false;
    }

    ms = *value;
    return true;
}

static std::optional<mpe::ArticulationType> articulationTypeFromName(const std::string& name)
{
    static const std::unordered_map<std::string, mpe::ArticulationType> TYPES_BY_NAME = []() {
        std::unordered_map<std::string, mpe::ArticulationType> result;
        for (const auto& [type, typeName] : mpe::ARTICULATION_TYPE_NAMES) {
            result.emplace(lowered(typeName.toStdString()), type);
        }
        return result;
    }();

    auto it = TYPES_BY_NAME.find(lowered(name));
    if (it == TYPES_BY_NAME.cend()) {
        return std::nullopt;
    }

    return it->second;
}

static bool readNumber(const std::string& code, size_t& pos, int& value)
{
    const size_t start = pos;
    if (pos < code.size() && code[pos] == '-') {
        ++pos;
    }

    const size_t digitsStart = pos;
    while (pos < code.size() && std::isdigit(static_cast<unsigned char>(code[pos]))) {
        ++pos;
    }

    if (pos == digitsStart) {
        pos = start;
        return false;
    }

    const std::optional<int> number = toInt(code.substr(start, pos - start));
    if (!number) {
        pos = start;
        return false;
    }

    value = *number;
    return true;
}

static bool isMidiValue(int value)
{
    return value >= 0 && value <= 127;
}

static bool readOptionalVelocity(const std::string& code, size_t& pos, uint8_t& velocity)
{
    velocity = ArticulationMapParser::DEFAULT_KEYSWITCH_VELOCITY;

    if (pos >= code.size() || code[pos] != 'v') {
        return true;
    }

    ++pos;
    int value = 0;
    if (!readNumber(code, pos, value) || value < 1 || value > 127) {
        return false;
    }

    velocity = static_cast<uint8_t>(value);
    return true;
}

//! NOTE: "a >b>  c" -> "a > b > c", empty segments dropped
static String joinedLabel(const std::string& str)
{
    std::string label;
    for (const std::string& segment : split(str, '>')) {
        const std::string s = trimmed(segment);
        if (s.empty()) {
            continue;
        }
        label += (label.empty() ? "" : " > ") + s;
    }

    return String::fromStdString(label);
}

std::optional<mpe::ArticulationType> ArticulationMapParser::scoreArticulationType(const String& name)
{
    return articulationTypeFromName(name.toStdString());
}

String ArticulationMapParser::scoreArticulationName(mpe::ArticulationType type)
{
    auto it = mpe::ARTICULATION_TYPE_NAMES.find(type);
    return it == mpe::ARTICULATION_TYPE_NAMES.cend() ? String() : String::fromQString(it->second);
}

std::vector<mpe::ArticulationType> ArticulationMapParser::implicitAliases(const String& labelStr)
{
    //! NOTE: an articulation named like a score articulation (e.g. "Staccatissimo",
    //! "Strings > Pizzicato") is selected by it automatically
    const std::string label = labelStr.toStdString();
    const size_t leafPos = label.rfind('>');
    std::string leaf = trimmed(leafPos == std::string::npos ? label : label.substr(leafPos + 1));
    leaf.erase(std::remove(leaf.begin(), leaf.end(), ' '), leaf.end());

    const std::optional<mpe::ArticulationType> type = articulationTypeFromName(leaf);
    if (type && *type != mpe::ArticulationType::Standard && *type != mpe::ArticulationType::Undefined) {
        return { *type };
    }

    return {};
}

bool ArticulationMapParser::parseMessages(const String& codeStr, int middleCOctave, std::vector<mpe::MidiMessage>& messages)
{
    static const std::unordered_map<char, int> PITCH_CLASSES {
        { 'C', 0 }, { 'D', 2 }, { 'E', 4 }, { 'F', 5 }, { 'G', 7 }, { 'A', 9 }, { 'B', 11 },
    };

    const std::string code = codeStr.toStdString();
    messages.clear();

    size_t pos = 0;
    while (pos < code.size()) {
        const char c = code[pos];
        mpe::MidiMessage message;

        if (PITCH_CLASSES.count(c)) {
            int pitch = PITCH_CLASSES.at(c);
            ++pos;

            if (pos < code.size() && code[pos] == '#') {
                ++pitch;
                ++pos;
            } else if (pos < code.size() && code[pos] == 'b') {
                --pitch;
                ++pos;
            }

            int octave = 0;
            if (!readNumber(code, pos, octave)) {
                return false;
            }

            pitch += 60 + (octave - middleCOctave) * 12;
            if (!isMidiValue(pitch) || !readOptionalVelocity(code, pos, message.value)) {
                return false;
            }

            message.type = mpe::MidiMessage::Type::Note;
            message.number = static_cast<uint8_t>(pitch);
        } else if (c == 'n') {
            ++pos;
            int pitch = 0;
            if (!readNumber(code, pos, pitch) || !isMidiValue(pitch) || !readOptionalVelocity(code, pos, message.value)) {
                return false;
            }

            message.type = mpe::MidiMessage::Type::Note;
            message.number = static_cast<uint8_t>(pitch);
        } else if (code.compare(pos, 2, "cc") == 0) {
            pos += 2;
            int number = 0;
            int value = 0;
            if (!readNumber(code, pos, number) || !isMidiValue(number)
                || pos >= code.size() || code[pos] != '='
                || !readNumber(code, ++pos, value) || !isMidiValue(value)) {
                return false;
            }

            message.type = mpe::MidiMessage::Type::ControlChange;
            message.number = static_cast<uint8_t>(number);
            message.value = static_cast<uint8_t>(value);
        } else if (code.compare(pos, 2, "pc") == 0) {
            pos += 2;
            int number = 0;
            if (!readNumber(code, pos, number) || !isMidiValue(number)) {
                return false;
            }

            message.type = mpe::MidiMessage::Type::ProgramChange;
            message.number = static_cast<uint8_t>(number);
        } else {
            return false;
        }

        messages.push_back(message);
    }

    return !messages.empty();
}

ArticulationMapParser::Result ArticulationMapParser::parse(const String& text)
{
    static const std::regex OPTION_REGEX(R"((^|\s)(ks|delay)\s*=\s*(-?\d+)\s*ms\b)", std::regex::icase);
    static const std::regex COLOR_REGEX(R"((^|\s)color\s*=\s*#([0-9a-f]{6}|[0-9a-f]{3})\b)", std::regex::icase);
    static const std::regex CHANNEL_REGEX(R"((^|\s)ch\s*=\s*(-?\d+)\b)", std::regex::icase);
    static const std::regex CHANNEL_CODE_REGEX(R"(^ch=(-?\d+)$)", std::regex::icase);

    Result result;
    ExpressionMap& map = result.map;
    map.sourceText = text;

    int& middleCOctave = result.middleCOctave;
    const std::vector<std::string> lines = split(text.toStdString(), '\n');

    auto addError = [&result](size_t lineIdx, const std::string& message) {
        result.errors.push_back({ lineIdx + 1, String::fromStdString(message) });
    };

    for (size_t lineIdx = 0; lineIdx < lines.size(); ++lineIdx) {
        std::string line = lines[lineIdx];

        const size_t commentPos = line.find(';');
        if (commentPos != std::string::npos) {
            line.erase(commentPos);
        }

        line = trimmed(line);
        if (line.empty()) {
            continue;
        }

        // "ch = 3 Name": the channel-only code may be written with spaces too
        static const std::regex CHANNEL_CODE_SPACES_REGEX(R"(^([*\-]*)ch\s*=\s*)", std::regex::icase);
        line = std::regex_replace(line, CHANNEL_CODE_SPACES_REGEX, "$1ch=", std::regex_constants::format_first_only);

        const size_t firstSpace = line.find_first_of(" \t");
        std::string first = line.substr(0, firstSpace);
        std::string rest = firstSpace == std::string::npos ? std::string() : trimmed(line.substr(firstSpace));

        if (first.front() == '@') {
            const std::string directive = lowered(first.substr(1));

            if (directive == "name") {
                map.name = String::fromStdString(rest);
            } else if (directive == "middlec") {
                int octave = 0;
                size_t pos = 1;
                if (rest.size() < 2 || std::toupper(static_cast<unsigned char>(rest[0])) != 'C' || !readNumber(rest, pos, octave)
                    || pos != rest.size()) {
                    addError(lineIdx, "@middlec expects C3 or C4, got \"" + rest + "\"");
                } else {
                    middleCOctave = octave;
                }
            } else if (directive == "folder") {
                const String path = joinedLabel(rest);
                if (path.empty()) {
                    addError(lineIdx, "@folder expects a folder name, e.g. Legato > Fast");
                } else {
                    result.folders.push_back({ path, map.entries.size() });
                }
            } else if (directive == "offset") {
                if (!parseMs(rest, map.keyswitchOffsetMs)) {
                    addError(lineIdx, "@offset expects a duration in ms, e.g. -15ms");
                }
            } else {
                addError(lineIdx, "unknown directive @" + directive);
            }

            continue;
        }

        bool isDefault = false;
        bool isDisabled = false;
        while (!first.empty() && (first.front() == '*' || first.front() == '-')) {
            (first.front() == '*' ? isDefault : isDisabled) = true;
            first.erase(0, 1);
        }

        ExpressionMapEntry entry;
        entry.disabled = isDisabled;

        //! NOTE: 1-16 in the file, 0-based in the entry
        auto readChannel = [&](const std::string& number) {
            const std::optional<int> channel = toInt(number);
            if (!channel || *channel < 1 || *channel > MIDI_CHANNEL_COUNT) {
                addError(lineIdx, "ch= expects a MIDI channel between 1 and " + std::to_string(MIDI_CHANNEL_COUNT));
                return;
            }
            entry.channel = *channel - 1;
        };

        std::smatch match;

        if (std::regex_match(first, match, CHANNEL_CODE_REGEX)) {
            readChannel(match[1].str());
            if (!entry.channel) {
                continue;
            }
        } else if (!parseMessages(String::fromStdString(first), middleCOctave, entry.messages)) {
            addError(lineIdx, "invalid MIDI code \"" + first + "\"");
            continue;
        }

        // Before the aliases are looked for: "ch=2" isn't an alias list
        if (std::regex_search(rest, match, CHANNEL_REGEX)) {
            if (entry.channel) {
                addError(lineIdx, "several ch= on the line, keeping the first one");
            } else {
                readChannel(match[2].str());
            }
            rest = match.prefix().str() + " " + match.suffix().str();

            if (std::regex_search(rest, match, CHANNEL_REGEX)) {
                addError(lineIdx, "several ch= on the line, keeping the first one");
                rest = match.prefix().str() + " " + match.suffix().str();
            }
        }

        std::string remaining;
        std::string searched = rest;
        while (std::regex_search(searched, match, OPTION_REGEX)) {
            remaining += match.prefix().str() + " ";

            const std::string option = lowered(match[2].str());
            const std::optional<int> ms = toInt(match[3].str());
            if (!ms) {
                addError(lineIdx, option + "= expects a duration between -" + std::to_string(MAX_ABS_NUMBER)
                         + " and " + std::to_string(MAX_ABS_NUMBER) + " ms");
            } else if (option == "ks") {
                entry.keyswitchOffsetMs = *ms;
            } else {
                entry.notesOffsetMs = *ms;
            }

            searched = match.suffix().str();
        }
        remaining += searched;

        if (std::regex_search(remaining, match, COLOR_REGEX)) {
            std::string hex = match[2].str();
            if (hex.size() == 3) {
                hex = { hex[0], hex[0], hex[1], hex[1], hex[2], hex[2] };
            }
            uint32_t color = 0;
            std::from_chars(hex.data(), hex.data() + hex.size(), color, 16); // exactly 6 hex digits, can't fail
            entry.color = color;
            remaining = match.prefix().str() + " " + match.suffix().str();
        }

        const size_t aliasPos = remaining.find('=');
        const std::string labelPart = aliasPos == std::string::npos ? remaining : remaining.substr(0, aliasPos);

        const std::string label = joinedLabel(labelPart).toStdString();

        if (label.empty()) {
            addError(lineIdx, "missing articulation name after \"" + first + "\"");
            continue;
        }

        entry.id = String::fromStdString(label);

        const bool isDuplicate = std::any_of(map.entries.cbegin(), map.entries.cend(), [&entry](const ExpressionMapEntry& e) {
            return e.id == entry.id;
        });
        if (isDuplicate) {
            addError(lineIdx, "duplicate articulation \"" + label + "\"");
            continue;
        }

        if (aliasPos == std::string::npos) {
            entry.aliases = implicitAliases(entry.id);
        } else {
            for (const std::string& aliasName : split(remaining.substr(aliasPos + 1), ',')) {
                const std::string alias = trimmed(aliasName);
                if (alias.empty()) {
                    continue;
                }

                if (const std::optional<mpe::ArticulationType> type = articulationTypeFromName(alias)) {
                    entry.aliases.push_back(*type);
                } else {
                    addError(lineIdx, "unknown score articulation \"" + alias + "\"");
                }
            }
        }

        if (isDefault) {
            if (map.defaultEntryId.empty()) {
                map.defaultEntryId = entry.id;
            } else {
                addError(lineIdx, "several default articulations, keeping \"" + map.defaultEntryId.toStdString() + "\"");
            }
        }

        map.entries.push_back(std::move(entry));
    }

    return result;
}
