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

#include "engraving/articulationmap/articulationmapdata.h"
#include "engraving/articulationmap/articulationmapparser.h"
#include "engraving/articulationmap/articulationmapwriter.h"
#include "engraving/articulationmap/internal/articulationmaprw.h"

using namespace mu::engraving;
using namespace muse;
using MidiMessage = muse::mpe::MidiMessage;

class ArticulationMapParser_Tests : public ::testing::Test
{
};

static MidiMessage note(uint8_t pitch, uint8_t velocity = 100)
{
    return MidiMessage { MidiMessage::Type::Note, pitch, velocity };
}

static MidiMessage cc(uint8_t number, uint8_t value)
{
    return MidiMessage { MidiMessage::Type::ControlChange, number, value };
}

TEST_F(ArticulationMapParser_Tests, Messages_NoteNames)
{
    std::vector<MidiMessage> messages;

    // [WHEN] Middle C is C3 (default sampler convention)
    EXPECT_TRUE(ArticulationMapParser::parseMessages(u"C3", 3, messages));
    EXPECT_EQ(messages, std::vector<MidiMessage>({ note(60) }));

    // [WHEN] Middle C is C4 (MuseScore convention)
    EXPECT_TRUE(ArticulationMapParser::parseMessages(u"C4", 4, messages));
    EXPECT_EQ(messages, std::vector<MidiMessage>({ note(60) }));

    // [THEN] Accidentals, negative octaves and velocities are supported
    EXPECT_TRUE(ArticulationMapParser::parseMessages(u"C#0Bb-1v80C-2", 3, messages));
    EXPECT_EQ(messages, std::vector<MidiMessage>({ note(25), note(22, 80), note(0) }));
}

TEST_F(ArticulationMapParser_Tests, Messages_Chained)
{
    std::vector<MidiMessage> messages;
    EXPECT_TRUE(ArticulationMapParser::parseMessages(u"C0D1cc3=64n12pc5", 3, messages));

    const std::vector<MidiMessage> expected {
        note(24), note(38), cc(3, 64), note(12), MidiMessage { MidiMessage::Type::ProgramChange, 5, 0 },
    };
    EXPECT_EQ(messages, expected);
}

TEST_F(ArticulationMapParser_Tests, Messages_Invalid)
{
    std::vector<MidiMessage> messages;
    EXPECT_FALSE(ArticulationMapParser::parseMessages(u"", 3, messages));
    EXPECT_FALSE(ArticulationMapParser::parseMessages(u"H0", 3, messages));
    EXPECT_FALSE(ArticulationMapParser::parseMessages(u"C", 3, messages));
    EXPECT_FALSE(ArticulationMapParser::parseMessages(u"cc3", 3, messages));
    EXPECT_FALSE(ArticulationMapParser::parseMessages(u"cc128=1", 3, messages));
    EXPECT_FALSE(ArticulationMapParser::parseMessages(u"G9", 3, messages)); // pitch 139
}

TEST_F(ArticulationMapParser_Tests, Parse_HugeNumbers_NoCrash)
{
    // [GIVEN] Numbers far beyond int range, which used to throw from std::stoi
    const String text = u"@offset 99999999999ms\n"
                        u"n99999999999 Foo\n"
                        u"C0 Legato ks=99999999999ms\n";

    const ArticulationMapParser::Result result = ArticulationMapParser::parse(text);

    // [THEN] Each is reported as an error instead
    EXPECT_EQ(result.errors.size(), 3);
    ASSERT_EQ(result.map.entries.size(), 1);
    EXPECT_FALSE(result.map.entries.front().keyswitchOffsetMs.has_value());
}

TEST_F(ArticulationMapParser_Tests, Parse_FullMap)
{
    const String text = u"; Violins\n"
                        u"@name   SSS Violins 1\n"
                        u"@offset -15ms\n"
                        u"*C0     Legato    ks=-30ms  delay=-250ms\n"
                        u"C#0     Long\n"
                        u"D0      Spiccato  = staccato, Staccatissimo\n"
                        u"cc32=10 Pizzicato = pizzicato color=#D03B3B ; comment\n"
                        u"C0D1cc3=64  Legato > Fast\n";

    const ArticulationMapParser::Result result = ArticulationMapParser::parse(text);
    EXPECT_TRUE(result.errors.empty());

    const ExpressionMap& map = result.map;
    EXPECT_EQ(map.name, u"SSS Violins 1");
    EXPECT_EQ(map.keyswitchOffsetMs, -15);
    EXPECT_EQ(map.defaultEntryId, u"Legato");
    EXPECT_EQ(map.sourceText, text);
    ASSERT_EQ(map.entries.size(), 5);

    const ExpressionMapEntry* legato = map.entry(u"Legato");
    ASSERT_TRUE(legato);
    EXPECT_EQ(legato->messages, std::vector<MidiMessage>({ note(24) }));
    EXPECT_EQ(map.keyswitchOffsetMsFor(*legato), -30);
    EXPECT_EQ(legato->notesOffsetMs, -250);

    const ExpressionMapEntry* spiccato = map.entry(u"Spiccato");
    ASSERT_TRUE(spiccato);
    EXPECT_EQ(map.keyswitchOffsetMsFor(*spiccato), -15);
    EXPECT_EQ(spiccato->aliases, std::vector<mpe::ArticulationType>({ mpe::ArticulationType::Staccato,
                                                                      mpe::ArticulationType::Staccatissimo }));

    const ExpressionMapEntry* pizz = map.entry(u"Pizzicato");
    ASSERT_TRUE(pizz);
    EXPECT_EQ(pizz->messages, std::vector<MidiMessage>({ cc(32, 10) }));
    EXPECT_EQ(pizz->color, std::optional<uint32_t>(0xD03B3B));
    EXPECT_FALSE(legato->color.has_value());

    const ExpressionMapEntry* fast = map.entry(u"Legato > Fast");
    ASSERT_TRUE(fast);
    EXPECT_EQ(fast->messages, std::vector<MidiMessage>({ note(24), note(38), cc(3, 64) }));

    // [THEN] Score articulations resolve by entry order
    mpe::ArticulationMap articulations;
    articulations.emplace(mpe::ArticulationType::Staccatissimo, mpe::ArticulationAppliedData());
    EXPECT_EQ(map.entryForArticulations(articulations), spiccato);
}

TEST_F(ArticulationMapParser_Tests, Parse_Errors)
{
    const String text = u"@unknown x\n"
                        u"Z9 Bad code\n"
                        u"C0\n"
                        u"C0 Legato\n"
                        u"D0 Legato\n"
                        u"E0 Spiccato = notAnArticulation\n";

    const ArticulationMapParser::Result result = ArticulationMapParser::parse(text);

    ASSERT_EQ(result.errors.size(), 5);
    EXPECT_EQ(result.errors[0].line, 1);
    EXPECT_EQ(result.errors[1].line, 2);
    EXPECT_EQ(result.errors[2].line, 3);
    EXPECT_EQ(result.errors[3].line, 5); // duplicate
    EXPECT_EQ(result.errors[4].line, 6);

    // [THEN] Valid lines are still kept
    EXPECT_EQ(result.map.entries.size(), 2);
}

TEST_F(ArticulationMapParser_Tests, RW_RoundTrip)
{
    ArticulationMapData data;

    InstrumentTrackId trackId;
    trackId.partId = muse::ID(3);
    trackId.instrumentId = u"violin";
    data.setMap(trackId, ArticulationMapParser::parse(u"@name Violins\n*C0 Legato\nD0 Spiccato = staccato\n").map);

    const EID chord1 = EID::newUnique();
    const EID chord2 = EID::newUnique();
    ASSERT_TRUE(chord1.isValid());
    data.setMark(chord1, ArticulationMark { u"Spiccato", ArticulationMark::Scope::SingleChord, -20 });
    data.setMark(chord2, ArticulationMark { u"Legato", ArticulationMark::Scope::Latched, 0 });

    ArticulationMapData read;
    ArticulationMapRW::read(read, ArticulationMapRW::write(data));

    ASSERT_TRUE(read.map(trackId));
    EXPECT_EQ(*read.map(trackId), *data.map(trackId));
    EXPECT_EQ(read.marks(), data.marks());
}

TEST_F(ArticulationMapParser_Tests, Parse_Channels)
{
    // [GIVEN] Articulations on their own MIDI channel, one with a channel alone
    const String text = u"*C0    Legato\n"
                        u"D0     Spiccato  ch=2 = staccato\n"
                        u"ch=3   Pizzicato\n"
                        u"E0     Tremolo   ch=0\n"
                        u"F0     Trill     ch=17\n";

    const ArticulationMapParser::Result result = ArticulationMapParser::parse(text);

    // [THEN] Channels are read 1-based and stored 0-based, out-of-range ones are errors
    ASSERT_EQ(result.errors.size(), 2);
    EXPECT_EQ(result.errors[0].line, 4);
    EXPECT_EQ(result.errors[1].line, 5);

    const ExpressionMap& map = result.map;
    ASSERT_TRUE(map.entry(u"Legato"));
    EXPECT_FALSE(map.entry(u"Legato")->channel);

    // [THEN] "ch=2" isn't mistaken for the alias list
    const ExpressionMapEntry* spiccato = map.entry(u"Spiccato");
    ASSERT_TRUE(spiccato);
    EXPECT_EQ(spiccato->channel, 1);
    EXPECT_EQ(spiccato->aliases, std::vector<mpe::ArticulationType> { mpe::ArticulationType::Staccato });

    const ExpressionMapEntry* pizzicato = map.entry(u"Pizzicato");
    ASSERT_TRUE(pizzicato);
    EXPECT_TRUE(pizzicato->messages.empty());
    EXPECT_EQ(pizzicato->channel, 2);
}

TEST_F(ArticulationMapParser_Tests, Write_Channels_RoundTrip)
{
    const ArticulationMapParser::Result file = ArticulationMapParser::parse(u"*C0 Legato ch=2\nch=3 Pizzicato\n");
    ASSERT_TRUE(file.errors.empty());

    const ArticulationMapParser::Result reread = ArticulationMapParser::parse(ArticulationMapWriter::write(file));

    EXPECT_TRUE(reread.errors.empty());
    EXPECT_EQ(reread.map.entries, file.map.entries);
}
