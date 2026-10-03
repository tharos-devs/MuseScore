/*
 * SPDX-License-Identifier: GPL-3.0-only
 * MuseScore-Studio-CLA-applies
 *
 * MuseScore Studio
 * Music Composition & Notation
 *
 * Copyright (C) 2021 MuseScore Limited and others
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

#include "mpe/mpetypes.h"
#include "mpe/events.h"

#include "engraving/automation/automationtypes.h"
#include "engraving/articulationmap/articulationmaptypes.h"

#include "../types/types.h"

namespace mu::engraving {
class Segment;
class PlayTechAnnotation;
class SoundFlag;
class Score;
class Part;
class MeasureRepeat;
class TextBase;
class ChordRest;
class RepeatSegment;
class Chord;

class PlaybackContext
{
public:
    explicit PlaybackContext(const Score* score);

    muse::mpe::dynamic_level_t appliableDynamicLevel(const track_idx_t trackIdx, const int nominalPositionTick) const;

    std::pair<muse::mpe::timestamp_t, PlayingTechniqueType> playingTechnique(const track_idx_t trackIdx,
                                                                             const int nominalPositionTick) const;
    muse::mpe::timestamp_t findPlayingTechniqueTimestamp(const track_idx_t trackIdx, PlayingTechniqueType type,
                                                         const int startFromTick) const;

    std::map<muse::mpe::timestamp_t, muse::mpe::SoundPresetChangeEventList> soundPresets(
        const track_idx_t trackFrom, const track_idx_t trackTo) const;
    muse::mpe::SoundPresetChangeEventList soundPresets(const track_idx_t trackIdx, const int nominalPositionTick) const;

    std::map<muse::mpe::timestamp_t, muse::mpe::TextArticulationEventList> textArticulations(
        const track_idx_t trackFrom, const track_idx_t trackTo) const;
    muse::mpe::TextArticulationEvent textArticulation(const track_idx_t trackIdx, const int nominalPositionTick) const;

    std::map<muse::mpe::timestamp_t, muse::mpe::SyllableEventList> syllables(
        const track_idx_t trackFrom, const track_idx_t trackTo) const;
    muse::mpe::SyllableEvent syllable(const track_idx_t trackIdx, const int nominalPositionTick) const;

    muse::mpe::DynamicAutomationLayers dynamicLevelLayers(const track_idx_t trackFrom, const track_idx_t trackTo) const;

    //! NOTE: [from; to] timestamp ranges
    using TimestampRanges = std::vector<std::pair<muse::mpe::timestamp_t, muse::mpe::timestamp_t> >;

    //! NOTE: the MIDI CC automation curves of the instrument, sampled into discrete controller changes,
    //! only within the given ranges (the ones whose events are being re-rendered; nullopt for the whole score)
    std::map<muse::mpe::timestamp_t, muse::mpe::ControllerChangeEventList> midiControllerEvents(const InstrumentTrackId& trackId,
                                                                                                const muse::mpe::layer_idx_t layerIdx,
                                                                                                const std::optional<TimestampRanges>& ranges)
    const;

    bool hasSoundFlags(const track_idx_t trackFrom, const track_idx_t trackTo) const;

    const ExpressionMap* expressionMap(const InstrumentTrackId& trackId) const;
    std::optional<ArticulationMark> articulationMark(const Chord* chord) const;
    //! NOTE: the latest latched mark of the staff at or before the given tick
    std::optional<ArticulationMark> latchedArticulationMark(const staff_idx_t staffIdx, const int tick) const;
    //! NOTE: the articulation a note at this position would play without anything of its own (mark, score articulation):
    //! the latest latched one of the instrument's first staff, else the map's default
    const ExpressionMapEntry* articulationInEffect(const InstrumentTrackId& trackId, const staff_idx_t firstStaffIdx, const int tick) const;

    //! NOTE: recorded while rendering, for the UI to show exactly what playback resolved - per track (voice),
    //! so simultaneous chords of different voices keep their own result
    void setResolvedArticulation(const track_idx_t trackIdx, const int tick, const std::optional<ResolvedArticulation>& articulation);
    std::optional<ResolvedArticulation> resolvedArticulation(const track_idx_t trackIdx, const int tick) const;

    void update(const track_idx_t trackFrom, const track_idx_t trackTo, const int tickFrom, const int tickTo, bool expandRepeats = true);
    void clear(const track_idx_t trackFrom, const track_idx_t trackTo, const int tickFrom, const int tickTo);

private:
    using SoundPresetsMap = std::map<int /*nominalPositionTick*/, muse::mpe::SoundPresetChangeEventList>;
    using SoundPresetsByTrack = std::map<track_idx_t, SoundPresetsMap>;

    using TextArticulationMap = std::map<int /*nominalPositionTick*/, muse::mpe::TextArticulationEvent>;
    using TextArticulationsByTrack = std::map<track_idx_t, TextArticulationMap>;

    using SyllableMap = std::map<int /*nominalPositionTick*/, muse::mpe::SyllableEvent>;
    using SyllablesByTrack = std::map<track_idx_t, SyllableMap>;

    using PlayTechniquesMap = std::map<int /*nominalPositionTick*/, mu::engraving::PlayingTechniqueType>;
    using PlayTechniquesByTrack = std::map<track_idx_t, PlayTechniquesMap>;

    using SoundFlagMap = std::unordered_map<staff_idx_t, const SoundFlag*>;

    void updatePlayTechMap(const Part* part, const PlayTechAnnotation* annotation, const int segmentPositionTick);
    void updateSoundPresetAndTextArticulationMap(const Part* part, const SoundFlagMap& flagsOnSegment, const int segmentPositionTick);
    void updateSyllableMap(const TextBase* text, const int segmentPositionTick);

    void handleSegmentAnnotations(const Segment* segment, const int segmentPositionTick, const track_idx_t trackFrom,
                                  const track_idx_t trackTo);
    void handleSegmentElements(const RepeatSegment* repeat, const Segment* segment, const int segmentPositionTick,
                               const track_idx_t trackFrom, const track_idx_t trackTo,
                               std::vector<const MeasureRepeat*>& foundMeasureRepeats);
    void handleMeasureRepeats(const std::vector<const MeasureRepeat*>& measureRepeats, const int tickPositionOffset);

    const AutomationCurve* dynamicsCurve(const track_idx_t trackIdx) const;

    //! NOTE: an automation curve's points (stored in expanded utick space, whatever the Play Repeats setting)
    //! timed the same way as the notes they apply to
    muse::mpe::AutomationCurve<muse::mpe::timestamp_t> automationTimeCurve(const AutomationCurve& curve) const;

    void updateLatchedArticulationMarks();

    bool hasOnlyOneLyricsVerse(const RepeatSegment* repeat, const track_idx_t track) const;

    const Score* m_score = nullptr;

    std::set<track_idx_t> m_usedTracks;
    mutable std::unordered_map<track_idx_t, const AutomationCurve*> m_dynamicsCurveByTrack;
    SoundPresetsByTrack m_soundPresetsByTrack;
    TextArticulationsByTrack m_textArticulationsByTrack;
    SyllablesByTrack m_syllablesByTrack;
    PlayTechniquesByTrack m_playTechniquesByTrack;
    std::map<staff_idx_t, std::map<int /*chordTick*/, ArticulationMark> > m_latchedArticulationMarksByStaff;
    std::map<track_idx_t, std::map<int /*chordTick*/, ResolvedArticulation> > m_resolvedArticulationsByTrack;

    std::unordered_map<const ChordRest*, int> m_currentVerseNumByChordRest;
    std::map<track_idx_t, std::set<int /*tick*/> > m_multiVerseLyricsPositionMap;
};

using PlaybackContextPtr = std::shared_ptr<PlaybackContext>;
}
