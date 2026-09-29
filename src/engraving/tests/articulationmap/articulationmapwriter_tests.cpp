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

#include <gtest/gtest.h>

#include "engraving/articulationmap/articulationmapparser.h"
#include "engraving/articulationmap/articulationmapwriter.h"

using namespace mu::engraving;
using namespace muse;

class ArticulationMapWriter_Tests : public ::testing::Test
{
};

static void expectSameFile(const ArticulationMapParser::Result& a, const ArticulationMapParser::Result& b)
{
    EXPECT_EQ(a.map.name, b.map.name);
    EXPECT_EQ(a.map.keyswitchOffsetMs, b.map.keyswitchOffsetMs);
    EXPECT_EQ(a.map.defaultEntryId, b.map.defaultEntryId);
    EXPECT_EQ(a.map.entries, b.map.entries);
    EXPECT_EQ(a.middleCOctave, b.middleCOctave);

    ASSERT_EQ(a.folders.size(), b.folders.size());
    for (size_t i = 0; i < a.folders.size(); ++i) {
        EXPECT_EQ(a.folders.at(i).path, b.folders.at(i).path);
        EXPECT_EQ(a.folders.at(i).entryIndex, b.folders.at(i).entryIndex);
    }
}

TEST_F(ArticulationMapWriter_Tests, NoteNames)
{
    EXPECT_EQ(ArticulationMapWriter::noteName(60, 3), u"C3");
    EXPECT_EQ(ArticulationMapWriter::noteName(60, 4), u"C4");
    EXPECT_EQ(ArticulationMapWriter::noteName(0, 3), u"C-2");
    EXPECT_EQ(ArticulationMapWriter::noteName(13, 3), u"C#-1");
    EXPECT_EQ(ArticulationMapWriter::noteName(127, 3), u"G8");
}

TEST_F(ArticulationMapWriter_Tests, RoundTrip_AllFeatures)
{
    const String text = u"; comment, not kept\n"
                        u"@name     Woodwinds\n"
                        u"@middlec  C4\n"
                        u"@offset   -15ms\n"
                        u"\n"
                        u"*C0 Stac shot color=#a02828\n"
                        u"C0v80E1 Stac Long ks=-30ms delay=-60ms\n"
                        u"cc20=127cc3=127 Long > Regular\n"
                        u"pc3 Long > Espressivo = tenuto, marcato\n"
                        u"@folder Long > Empty\n"
                        u"D0 Staccatissimo\n"
                        u"D#0 Staccato =\n"
                        u"-E0 Tremolo\n"
                        u"@folder Trills\n";

    const ArticulationMapParser::Result parsed = ArticulationMapParser::parse(text);
    ASSERT_TRUE(parsed.errors.empty());
    ASSERT_EQ(parsed.map.entries.size(), 7);
    ASSERT_EQ(parsed.folders.size(), 2);
    EXPECT_EQ(parsed.middleCOctave, 4);
    EXPECT_EQ(parsed.folders.at(0).entryIndex, 4);
    EXPECT_EQ(parsed.folders.at(1).entryIndex, 7);
    EXPECT_TRUE(parsed.map.entries.at(5).aliases.empty()); // "=" with an empty list: no implicit alias

    EXPECT_TRUE(parsed.map.entries.at(6).disabled);
    EXPECT_EQ(parsed.map.entry(u"Tremolo"), nullptr);

    const String written = ArticulationMapWriter::write(parsed);
    const ArticulationMapParser::Result reparsed = ArticulationMapParser::parse(written);
    ASSERT_TRUE(reparsed.errors.empty()) << written.toStdString();

    expectSameFile(parsed, reparsed);
}

TEST_F(ArticulationMapWriter_Tests, ImplicitAliasesNotWritten)
{
    const ArticulationMapParser::Result parsed = ArticulationMapParser::parse(u"C0 Strings > Pizzicato\n");
    ASSERT_EQ(parsed.map.entries.size(), 1);
    ASSERT_FALSE(parsed.map.entries.at(0).aliases.empty());

    const String written = ArticulationMapWriter::write(parsed);
    EXPECT_FALSE(written.contains(u"=")) << written.toStdString();
}
