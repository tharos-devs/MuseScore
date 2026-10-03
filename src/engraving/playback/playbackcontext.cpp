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

#include "playbackcontext.h"

#include <algorithm>
#include <cmath>
#include <limits>

#include "dom/chord.h"
#include "dom/lyrics.h"
#include "dom/masterscore.h"
#include "dom/measure.h"
#include "dom/measurerepeat.h"
#include "dom/part.h"
#include "dom/playtechannotation.h"
#include "dom/repeatlist.h"
#include "dom/score.h"
#include "dom/segment.h"
#include "dom/soundflag.h"
#include "dom/staff.h"
#include "dom/stafftext.h"

#include "engraving/automation/automationdata.h"
#include "engraving/articulationmap/articulationmapdata.h"
#include "engraving/infrastructure/eidregister.h"

#include "utils/arrangementutils.h"
#include "utils/expressionutils.h"
#include "types/constants.h"

#include "global/containers.h"
#include "global/realfn.h"

using namespace mu::engraving;
using namespace muse;
using namespace muse::mpe;

static bool soundFlagPlayable(const SoundFlag* flag)
{
    if (flag && flag->play()) {
        return !flag->soundPresets().empty() || !flag->playingTechnique().empty();
    }

    return false;
}

static dynamic_level_t toDynamicLevel(double normalizedValue)
{
    return static_cast<dynamic_level_t>(std::lround(normalizedValue * MAX_DYNAMIC_LEVEL));
}

template<typename ByTrackMap, typename ResultMap, typename MergeFn>
static void collectByTrackRange(const ByTrackMap& byTrackMap, const track_idx_t trackFrom, const track_idx_t trackTo,
                                const Score* score, const std::set<track_idx_t>& usedTracks, ResultMap& result, MergeFn merge)
{
    for (auto it = byTrackMap.lower_bound(trackFrom); it != byTrackMap.end() && it->first < trackTo; ++it) {
        if (!muse::contains(usedTracks, it->first)) {
            continue;
        }

        for (const auto& pair : it->second) {
            const timestamp_t timestamp = timestampFromTicks(score, pair.first);
            merge(result[timestamp], pair.second);
        }
    }
}

PlaybackContext::PlaybackContext(const Score* score)
    : m_score(score)
{
}

dynamic_level_t PlaybackContext::appliableDynamicLevel(const track_idx_t trackIdx, const int nominalPositionTick) const
{
    TRACEFUNC;

    const AutomationCurve* curve = dynamicsCurve(trackIdx);
    if (!curve) {
        return NATURAL_DYNAMIC_LEVEL;
    }

    const auto it = muse::findLessOrEqual(*curve, nominalPositionTick);
    if (it == curve->cend()) {
        return NATURAL_DYNAMIC_LEVEL;
    }

    const auto next = std::next(it);
    if (next == curve->cend()) {
        return toDynamicLevel(it->second.value.outValue);
    }

    const real_t t = static_cast<real_t>(nominalPositionTick - it->first) / static_cast<real_t>(next->first - it->first);
    return toDynamicLevel(muse::mpe::evaluateAt(next->second.value, it->second.value.outValue, t));
}

std::pair<mpe::timestamp_t, PlayingTechniqueType> PlaybackContext::playingTechnique(const track_idx_t trackIdx,
                                                                                    const int nominalPositionTick) const
{
    auto mapIt = m_playTechniquesByTrack.find(trackIdx);
    if (mapIt == m_playTechniquesByTrack.cend()) {
        return std::make_pair(0, PlayingTechniqueType::Natural);
    }

    const PlayTechniquesMap& playTechniquesMap = mapIt->second;

    auto it = findLessOrEqual(playTechniquesMap, nominalPositionTick);
    if (it == playTechniquesMap.cend()) {
        return std::make_pair(0, PlayingTechniqueType::Natural);
    }

    return std::make_pair(timestampFromTicks(m_score, it->first), it->second);
}

muse::mpe::timestamp_t PlaybackContext::findPlayingTechniqueTimestamp(const track_idx_t trackIdx, PlayingTechniqueType type,
                                                                      const int startFromTick) const
{
    auto mapIt = m_playTechniquesByTrack.find(trackIdx);
    if (mapIt == m_playTechniquesByTrack.cend()) {
        return -1;
    }

    const PlayTechniquesMap& playTechniquesMap = mapIt->second;

    auto it = playTechniquesMap.upper_bound(startFromTick);

    for (; it != playTechniquesMap.end(); ++it) {
        if (it->second == type) {
            return timestampFromTicks(m_score, it->first);
        }
    }

    return -1;
}

std::map<timestamp_t, SoundPresetChangeEventList> PlaybackContext::soundPresets(const track_idx_t trackFrom,
                                                                                const track_idx_t trackTo) const
{
    std::map<timestamp_t, SoundPresetChangeEventList> result;

    collectByTrackRange(m_soundPresetsByTrack, trackFrom, trackTo, m_score, m_usedTracks, result,
                        [](SoundPresetChangeEventList& list, const SoundPresetChangeEventList& events) {
        list.insert(list.end(), events.begin(), events.end());
    });

    return result;
}

SoundPresetChangeEventList PlaybackContext::soundPresets(const track_idx_t trackIdx, const int nominalPositionTick) const
{
    auto presetsIt = m_soundPresetsByTrack.find(trackIdx);
    if (presetsIt == m_soundPresetsByTrack.end()) {
        return {};
    }

    const SoundPresetsMap& map = presetsIt->second;
    auto it = muse::findLessOrEqual(map, nominalPositionTick);
    if (it == map.end()) {
        return {};
    }

    return it->second;
}

std::map<timestamp_t, TextArticulationEventList> PlaybackContext::textArticulations(const track_idx_t trackFrom,
                                                                                    const track_idx_t trackTo) const
{
    std::map<timestamp_t, TextArticulationEventList> result;

    collectByTrackRange(m_textArticulationsByTrack, trackFrom, trackTo, m_score, m_usedTracks, result,
                        [](TextArticulationEventList& list, const TextArticulationEvent& event) {
        list.push_back(event);
    });

    return result;
}

TextArticulationEvent PlaybackContext::textArticulation(const track_idx_t trackIdx, const int nominalPositionTick) const
{
    auto articulationsIt = m_textArticulationsByTrack.find(trackIdx);
    if (articulationsIt == m_textArticulationsByTrack.end()) {
        return {};
    }

    const TextArticulationMap& map = articulationsIt->second;
    auto it = muse::findLessOrEqual(map, nominalPositionTick);
    if (it == map.end()) {
        return {};
    }

    TextArticulationEvent result = it->second;
    result.flags.setFlag(TextArticulationEvent::StartsAtPlaybackPosition, it->first == nominalPositionTick);

    return result;
}

std::map<timestamp_t, SyllableEventList> PlaybackContext::syllables(const track_idx_t trackFrom, const track_idx_t trackTo) const
{
    std::map<timestamp_t, SyllableEventList> result;

    collectByTrackRange(m_syllablesByTrack, trackFrom, trackTo, m_score, m_usedTracks, result,
                        [](SyllableEventList& list, const SyllableEvent& event) {
        list.push_back(event);
    });

    return result;
}

SyllableEvent PlaybackContext::syllable(const track_idx_t trackIdx, const int nominalPositionTick) const
{
    auto syllablesIt = m_syllablesByTrack.find(trackIdx);
    if (syllablesIt == m_syllablesByTrack.end()) {
        return {};
    }

    const SyllableMap& map = syllablesIt->second;
    auto it = muse::findLessOrEqual(map, nominalPositionTick);
    if (it == map.end()) {
        return {};
    }

    SyllableEvent result = it->second;
    result.flags.setFlag(SyllableEvent::StartsAtPlaybackPosition, it->first == nominalPositionTick);

    return result;
}

DynamicAutomationLayers PlaybackContext::dynamicLevelLayers(const track_idx_t trackFrom, const track_idx_t trackTo) const
{
    TRACEFUNC;

    if (!m_score || !m_score->automationData()) {
        return {};
    }

    DynamicAutomationLayers result;
    const AutomationCurve* lastCurve = nullptr;
    DynamicAutomationMap lastLevelMap;

    for (track_idx_t trackIdx = trackFrom; trackIdx < trackTo; ++trackIdx) {
        const AutomationCurve* curve = dynamicsCurve(trackIdx);
        if (!curve || curve->empty()) {
            continue;
        }

        if (curve != lastCurve) {
            lastLevelMap.clear();
            lastCurve = curve;

            lastLevelMap = automationTimeCurve(*curve);

            if (curve->cbegin()->first > 0) {
                AutomationPoint naturalPoint;
                naturalPoint.value.outValue = real_t(NATURAL_DYNAMIC_LEVEL) / real_t(MAX_DYNAMIC_LEVEL);
                lastLevelMap.try_emplace(0, naturalPoint.value);
            }
        }

        result[static_cast<layer_idx_t>(trackIdx)] = lastLevelMap;
    }

    return result;
}

//! NOTE: with Play Repeats on, notes are timed on the expanded timeline, like the curve's own uticks. With it off,
//! they're timed on the flattened one, each tick played once: a point then plays at its tick on the first pass
//! through it (automation edits are mirrored to every pass, so later passes hold the same points)
muse::mpe::AutomationCurve<timestamp_t> PlaybackContext::automationTimeCurve(const AutomationCurve& curve) const
{
    muse::mpe::AutomationCurve<timestamp_t> result;
    if (!m_score || curve.empty()) {
        return result;
    }

    if (m_score->masterScore()->expandRepeats()) {
        const TempoTimeline& timeline = m_score->tempoTimeline(/*expandRepeats*/ true);
        auto hint = result.end();
        for (const auto& [utick, point] : curve) {
            hint = result.insert(hint, { timeline.utick2utime(utick) * 1000000, point.value });
        }
        return result;
    }

    const RepeatList& repeatList = m_score->expandedRepeatList();
    const TempoTimeline& flattenedTimeline = m_score->tempoTimeline(/*expandRepeats*/ false);

    for (const auto& [utick, point] : curve) {
        auto segmentIt = repeatList.findRepeatSegmentFromUTick(utick);
        if (segmentIt == repeatList.cend()) {
            if (repeatList.empty()) {
                continue;
            }
            segmentIt = std::prev(repeatList.cend()); // e.g. a point right at the end of the score
        }

        const RepeatSegment* segment = *segmentIt;
        const int tick = utick - (segment->utick - segment->tick);

        const auto firstPassIt = std::find_if(repeatList.cbegin(), repeatList.cend(), [tick](const RepeatSegment* s) {
            return tick >= s->tick && tick < s->endTick();
        });
        if (firstPassIt != repeatList.cend() && *firstPassIt != segment) {
            continue;
        }

        result.insert_or_assign(flattenedTimeline.utick2utime(tick) * 1000000, point.value);
    }

    return result;
}

//! NOTE: a controller change is only emitted when the 0..127 MIDI value actually changes,
//! sampling ramps every MIDI_CC_SAMPLE_INTERVAL_US at most (same sampling as muse::mpe::resampleCurve)
std::map<timestamp_t, ControllerChangeEventList> PlaybackContext::midiControllerEvents(const InstrumentTrackId& trackId,
                                                                                       const layer_idx_t layerIdx,
                                                                                       const std::optional<TimestampRanges>& ranges) const
{
    TRACEFUNC;

    constexpr timestamp_t MIDI_CC_SAMPLE_INTERVAL_US = 10000;

    std::map<timestamp_t, ControllerChangeEventList> result;

    const AutomationDataConstPtr automation = m_score ? m_score->automationData() : nullptr;
    if (!automation) {
        return result;
    }

    const auto overlapsRanges = [&ranges](timestamp_t from, timestamp_t to) {
        if (!ranges) {
            return true;
        }
        return std::any_of(ranges->cbegin(), ranges->cend(), [from, to](const auto& range) {
            return from <= range.second && to >= range.first;
        });
    };

    for (const auto& [key, curve] : automation->curves()) {
        if (key.type != AutomationType::MidiCC || curve.empty()) {
            continue;
        }

        const std::optional<InstrumentTrackId> keyTrackId = key.trackId();
        if (!keyTrackId || !(*keyTrackId == trackId)) {
            continue;
        }

        const muse::mpe::AutomationCurve<timestamp_t> timeCurve = automationTimeCurve(curve);
        if (timeCurve.empty()) {
            continue;
        }

        // Samples outside the ranges still update lastMidiValue on purpose: events there are kept from the previous
        // render (not cleared), so their values really were sent - this renders exactly what a full render would
        std::optional<int> lastMidiValue;
        const auto addSample = [&](timestamp_t t, real_t normalized) {
            const int midiValue = std::clamp(static_cast<int>(std::lround(normalized * 127.0)), 0, 127);
            if (lastMidiValue == midiValue) {
                return;
            }
            lastMidiValue = midiValue;

            // The value held before the first point applies from the very start
            const timestamp_t timestamp = t == timeCurve.cbegin()->first ? 0 : t;
            if (!overlapsRanges(timestamp, timestamp)) {
                return;
            }

            ControllerChangeEvent event;
            event.type = ControllerChangeEvent::ControlChange;
            event.controller = key.controller;
            event.val = static_cast<float>(midiValue) / 127.f;
            event.layerIdx = layerIdx;
            result[timestamp].push_back(event);
        };

        for (auto it = timeCurve.cbegin(); it != timeCurve.cend(); ++it) {
            const auto next = std::next(it);
            const timestamp_t segmentFrom = it == timeCurve.cbegin() ? 0 : it->first;
            const timestamp_t segmentTo = next != timeCurve.cend() ? next->first : std::numeric_limits<timestamp_t>::max();

            if (!overlapsRanges(segmentFrom, segmentTo)) {
                // Not sampled at all: the next sample can't be compared with what was sent before
                lastMidiValue.reset();
                continue;
            }

            addSample(it->first, it->second.outValue);

            if (next == timeCurve.cend()) {
                continue;
            }

            const real_t nextArrival = muse::mpe::resolveInValue(next->second, it->second.outValue);
            const std::optional<muse::mpe::AutomationPoint::Ease> nextEase = muse::mpe::ease(next->second);
            const bool isFlat = (!nextEase || nextEase->isNone()) && muse::RealIsEqual(nextArrival, it->second.outValue);
            if (isFlat) {
                continue;
            }

            const timestamp_t intervalDuration = next->first - it->first;
            const size_t steps = std::max(size_t((intervalDuration + MIDI_CC_SAMPLE_INTERVAL_US - 1) / MIDI_CC_SAMPLE_INTERVAL_US),
                                          size_t(1));
            for (size_t j = 1; j < steps; ++j) {
                const timestamp_t t = it->first + intervalDuration * static_cast<timestamp_t>(j) / static_cast<timestamp_t>(steps);
                addSample(t, muse::mpe::evaluateAt(next->second, it->second.outValue, real_t(j) / real_t(steps)));
            }
        }
    }

    return result;
}

void PlaybackContext::update(const track_idx_t trackFrom, const track_idx_t trackTo, const int tickFrom, const int tickTo,
                             bool expandRepeats)
{
    TRACEFUNC;

    IF_ASSERT_FAILED(m_score) {
        return;
    }

    IF_ASSERT_FAILED(trackFrom <= trackTo) {
        return;
    }

    m_dynamicsCurveByTrack.clear();
    updateLatchedArticulationMarks();

    for (const RepeatSegment* repeatSegment : m_score->repeatList(expandRepeats)) {
        const int repeatStartTick = repeatSegment->tick;
        const int repeatEndTick = repeatSegment->endTick();

        if (repeatStartTick > tickTo || repeatEndTick <= tickFrom) {
            continue;
        }

        std::vector<const MeasureRepeat*> measureRepeats;
        int tickPositionOffset = repeatSegment->utick - repeatSegment->tick;

        for (const Measure* measure : repeatSegment->measureList()) {
            const int measureStartTick = measure->tick().ticks();
            const int measureEndTick = measure->endTick().ticks();

            if (measureStartTick > tickTo || measureEndTick <= tickFrom) {
                continue;
            }

            for (const Segment* segment = measure->first(); segment; segment = segment->next()) {
                const int segmentTick = segment->tick().ticks();
                if (segmentTick > tickTo) {
                    break;
                }

                if (segmentTick < tickFrom) {
                    continue;
                }

                int segmentStartTick = segmentTick + tickPositionOffset;

                handleSegmentElements(repeatSegment, segment, segmentStartTick, trackFrom, trackTo, measureRepeats);
                handleSegmentAnnotations(segment, segmentStartTick, trackFrom, trackTo);
            }
        }

        handleMeasureRepeats(measureRepeats, tickPositionOffset);
    }
}

void PlaybackContext::clear(const track_idx_t trackFrom, const track_idx_t trackTo, const int tickFrom, const int tickTo)
{
    const auto eraseTickRangeByTrack = [trackFrom, trackTo, tickFrom, tickTo](auto& byTrackMap) {
        for (auto it = byTrackMap.lower_bound(trackFrom); it != byTrackMap.end() && it->first < trackTo;) {
            auto& innerMap = it->second;
            innerMap.erase(innerMap.lower_bound(tickFrom), innerMap.upper_bound(tickTo));

            if (innerMap.empty()) {
                it = byTrackMap.erase(it);
            } else {
                ++it;
            }
        }
    };

    eraseTickRangeByTrack(m_soundPresetsByTrack);
    eraseTickRangeByTrack(m_textArticulationsByTrack);
    eraseTickRangeByTrack(m_syllablesByTrack);
    eraseTickRangeByTrack(m_playTechniquesByTrack);
    eraseTickRangeByTrack(m_multiVerseLyricsPositionMap);

    muse::remove_if(m_currentVerseNumByChordRest, [trackFrom, trackTo, tickFrom, tickTo](const auto& pair) {
        const track_idx_t track = pair.first->track();
        const int tick = pair.first->tick().ticks();
        return track >= trackFrom && track < trackTo && tick >= tickFrom && tick <= tickTo;
    });
}

bool PlaybackContext::hasSoundFlags(const track_idx_t trackFrom, const track_idx_t trackTo) const
{
    auto hasEntryInRange = [trackFrom, trackTo](const auto& byTrackMap) {
        auto it = byTrackMap.lower_bound(trackFrom);
        return it != byTrackMap.end() && it->first < trackTo;
    };

    return hasEntryInRange(m_soundPresetsByTrack) || hasEntryInRange(m_textArticulationsByTrack);
}

void PlaybackContext::updatePlayTechMap(const Part* part, const PlayTechAnnotation* annotation, const int segmentPositionTick)
{
    if (!annotation->playPlayTechAnnotation()) {
        return;
    }
    const PlayingTechniqueType type = annotation->techniqueType();
    if (type == PlayingTechniqueType::Undefined) {
        return;
    }

    const bool cancelPlayTechniques = (type == PlayingTechniqueType::Natural || type == PlayingTechniqueType::Open)
                                      && !m_textArticulationsByTrack.empty();

    TextArticulationEvent textArticulation;
    textArticulation.text = mpe::ORDINARY_PLAYING_TECHNIQUE_CODE;

    const TrackRange trackRange = part->trackRange();
    for (track_idx_t idx = trackRange.startTrack; idx < trackRange.endTrack; ++idx) {
        m_playTechniquesByTrack[idx][segmentPositionTick] = type;

        if (cancelPlayTechniques) {
            textArticulation.layerIdx = static_cast<layer_idx_t>(idx);
            m_textArticulationsByTrack[idx][segmentPositionTick] = textArticulation;
        }
    }
}

void PlaybackContext::updateSoundPresetAndTextArticulationMap(const Part* part, const SoundFlagMap& flagsOnSegment,
                                                              const int segmentPositionTick)
{
    auto trackAccepted = [&flagsOnSegment](const SoundFlag* flag, track_idx_t trackIdx) {
        staff_idx_t staffIdx = track2staff(trackIdx);

        if (flag->staffIdx() == staffIdx) {
            return true;
        }

        if (flag->applyToAllStaves()) {
            return !muse::contains(flagsOnSegment, staffIdx);
        }

        return false;
    };

    const TrackRange trackRange = part->trackRange();

    for (const auto& pair : flagsOnSegment) {
        const SoundFlag* flag = pair.second;

        for (track_idx_t trackIdx = trackRange.startTrack; trackIdx < trackRange.endTrack; ++trackIdx) {
            if (!trackAccepted(flag, trackIdx)) {
                continue;
            }

            SoundPresetChangeEventList presets;

            for (const String& soundPreset : flag->soundPresets()) {
                if (soundPreset.empty()) {
                    continue;
                }

                SoundPresetChangeEvent event;
                event.code = soundPreset;
                event.layerIdx = static_cast<layer_idx_t>(trackIdx);
                presets.emplace_back(std::move(event));
            }

            if (!presets.empty()) {
                SoundPresetChangeEventList& target = m_soundPresetsByTrack[trackIdx][segmentPositionTick];
                target.insert(target.end(), std::make_move_iterator(presets.begin()), std::make_move_iterator(presets.end()));
            }

            if (!flag->playingTechnique().empty()) {
                TextArticulationEvent event;
                event.text = flag->playingTechnique();
                event.layerIdx = static_cast<layer_idx_t>(trackIdx);
                m_textArticulationsByTrack[trackIdx][segmentPositionTick] = std::move(event);
            }
        }

        if (flag->playingTechnique() == mpe::ORDINARY_PLAYING_TECHNIQUE_CODE) {
            for (track_idx_t trackIdx = trackRange.startTrack; trackIdx < trackRange.endTrack; ++trackIdx) {
                m_playTechniquesByTrack[trackIdx][segmentPositionTick] = PlayingTechniqueType::Natural;
            }
        }
    }
}

void PlaybackContext::updateSyllableMap(const TextBase* text, const int segmentPositionTick)
{
    IF_ASSERT_FAILED(text->isLyrics() || text->isSticking()) {
        return;
    }

    if (text->empty()) {
        return;
    }

    SyllableEvent syllable;
    syllable.text = text->plainText();

    if (text->isLyrics()) {
        const Lyrics* lyrics = toLyrics(text);

        switch (lyrics->syllabic()) {
        case LyricsSyllabic::BEGIN:
        case LyricsSyllabic::MIDDLE:
            syllable.flags.setFlag(SyllableEvent::HyphenedToNext);
            break;
        case LyricsSyllabic::SINGLE:
        case LyricsSyllabic::END:
            break;
        }
    }

    const staff_idx_t staffIdx = text->staffIdx();

    for (voice_idx_t voiceIdx = 0; voiceIdx < VOICES; ++voiceIdx) {
        track_idx_t trackIdx = staff2track(staffIdx, voiceIdx);
        syllable.layerIdx = static_cast<layer_idx_t>(trackIdx);
        m_syllablesByTrack[trackIdx][segmentPositionTick] = syllable;
    }
}

void PlaybackContext::handleSegmentAnnotations(const Segment* segment, const int segmentPositionTick,
                                               const track_idx_t trackFrom, const track_idx_t trackTo)
{
    std::unordered_map<const Part*, SoundFlagMap> soundFlagsByPart;

    for (const EngravingItem* annotation : segment->annotations()) {
        if (!annotation || !annotation->part()) {
            continue;
        }

        const Part* annotationPart = annotation->part();
        const TrackRange annotationPartTrackRange = annotationPart->trackRange();
        if (annotationPartTrackRange.startTrack >= trackTo || annotationPartTrackRange.endTrack <= trackFrom) {
            continue;
        }

        if (annotation->isPlayTechAnnotation()) {
            updatePlayTechMap(annotationPart, toPlayTechAnnotation(annotation), segmentPositionTick);
            continue;
        }

        if (annotation->isSticking()) {
            updateSyllableMap(toTextBase(annotation), segmentPositionTick);
            continue;
        }

        if (annotation->isStaffText()) {
            if (const SoundFlag* flag = toStaffText(annotation)->soundFlag()) {
                if (soundFlagPlayable(flag)) {
                    soundFlagsByPart[annotationPart].emplace(flag->staffIdx(), flag);
                }
            }
        }
    }

    for (const auto& pair : soundFlagsByPart) {
        const Part* part = pair.first;
        const SoundFlagMap& flagsOnSegment = pair.second;

        if (!flagsOnSegment.empty()) {
            updateSoundPresetAndTextArticulationMap(part, flagsOnSegment, segmentPositionTick);
        }
    }
}

void PlaybackContext::handleSegmentElements(const RepeatSegment* repeat, const Segment* segment,
                                            const int segmentPositionTick,
                                            const track_idx_t trackFrom, const track_idx_t trackTo,
                                            std::vector<const MeasureRepeat*>& foundMeasureRepeats)
{
    for (track_idx_t track = trackFrom; track < trackTo; ++track) {
        const EngravingItem* item = segment->element(track);
        if (!item) {
            continue;
        }

        if (item->isMeasureRepeat()) {
            foundMeasureRepeats.push_back(toMeasureRepeat(item));
            continue;
        }

        if (item->isChordRest()) {
            m_usedTracks.insert(track);

            const ChordRest* chordRest = toChordRest(item);
            if (chordRest->lyrics().empty()) {
                continue;
            }

            const Lyrics* lyrics = nullptr;

            auto verseNumIt = m_currentVerseNumByChordRest.find(chordRest);
            if (verseNumIt == m_currentVerseNumByChordRest.end()) {
                m_currentVerseNumByChordRest[chordRest] = 0;
                lyrics = chordRest->lyrics(0);
                if (chordRest->lyrics().size() > 1) {
                    m_multiVerseLyricsPositionMap[track].insert(chordRest->tick().ticks());
                }
            } else if (hasOnlyOneLyricsVerse(repeat, track)) {
                lyrics = chordRest->lyrics(0);
            } else {
                verseNumIt->second++;
                lyrics = chordRest->lyrics(verseNumIt->second);
            }

            if (lyrics) {
                updateSyllableMap(lyrics, segmentPositionTick);
            }
        }
    }
}

template<typename ItemsMap>
static void copyItemsInRange(ItemsMap& source, const int rangeStartTick, const int rangeEndTick, const int newItemsOffsetTick)
{
    auto startIt = source.lower_bound(rangeStartTick);
    if (startIt == source.end()) {
        return;
    }

    auto endIt = source.lower_bound(rangeEndTick);

    ItemsMap newItems;
    for (auto it = startIt; it != endIt; ++it) {
        int tick = it->first + newItemsOffsetTick;
        newItems.insert_or_assign(tick, it->second);
    }

    source.merge(std::move(newItems));
}

template<typename ItemsMap>
static void copyItemsInRange(std::map<track_idx_t, ItemsMap>& source, const track_idx_t trackFrom, const track_idx_t trackTo,
                             const int rangeStartTick, const int rangeEndTick, const int newItemsOffsetTick)
{
    for (auto it = source.lower_bound(trackFrom); it != source.end() && it->first < trackTo; ++it) {
        copyItemsInRange(it->second, rangeStartTick, rangeEndTick, newItemsOffsetTick);
    }
}

void PlaybackContext::handleMeasureRepeats(const std::vector<const MeasureRepeat*>& measureRepeats, const int tickPositionOffset)
{
    for (const MeasureRepeat* mr : measureRepeats) {
        const Part* part = mr->part();
        if (!part) {
            continue;
        }

        const Measure* currMeasure = mr->firstMeasureOfGroup();
        if (!currMeasure) {
            continue;
        }

        const Measure* referringMeasure = mr->referringMeasure(currMeasure);
        if (!referringMeasure) {
            continue;
        }

        int currentMeasureTick = currMeasure->tick().ticks();
        int referringMeasureTick = referringMeasure->tick().ticks();
        int newItemsOffsetTick = currentMeasureTick - referringMeasureTick;

        const TrackRange trackRange = part->trackRange();

        for (int num = 0; num < mr->numMeasures(); ++num) {
            int startTick = referringMeasure->tick().ticks() + tickPositionOffset;
            int endTick = referringMeasure->endTick().ticks() + tickPositionOffset;

            copyItemsInRange(m_soundPresetsByTrack, trackRange.startTrack, trackRange.endTrack, startTick, endTick, newItemsOffsetTick);
            copyItemsInRange(m_textArticulationsByTrack, trackRange.startTrack, trackRange.endTrack, startTick, endTick,
                             newItemsOffsetTick);
            copyItemsInRange(m_syllablesByTrack, trackRange.startTrack, trackRange.endTrack, startTick, endTick, newItemsOffsetTick);
            copyItemsInRange(m_playTechniquesByTrack, trackRange.startTrack, trackRange.endTrack, startTick, endTick,
                             newItemsOffsetTick);

            currMeasure = currMeasure->nextMeasure();
            if (!currMeasure) {
                break;
            }

            referringMeasure = mr->referringMeasure(currMeasure);
            if (!referringMeasure) {
                break;
            }
        }
    }
}

const mu::engraving::AutomationCurve* PlaybackContext::dynamicsCurve(const track_idx_t trackIdx) const
{
    auto cacheIt = m_dynamicsCurveByTrack.find(trackIdx);
    if (cacheIt != m_dynamicsCurveByTrack.end()) {
        return cacheIt->second;
    }

    const AutomationDataConstPtr automation = m_score ? m_score->automationData() : nullptr;
    const Staff* staff = automation ? m_score->staff(track2staff(trackIdx)) : nullptr;

    const AutomationCurve* curve = nullptr;
    if (staff) {
        AutomationCurveKey key = AutomationCurveKey::staff(AutomationType::Dynamics, staff->id(), track2voice(trackIdx));
        curve = &automation->curve(key);

        if (curve->empty()) {
            key = key.withoutVoice();
            curve = &automation->curve(key);
        }
    }

    m_dynamicsCurveByTrack.emplace(trackIdx, curve);
    return curve;
}

bool PlaybackContext::hasOnlyOneLyricsVerse(const RepeatSegment* repeat, const track_idx_t track) const
{
    if (m_multiVerseLyricsPositionMap.empty()) {
        return true;
    }

    const auto trackIt = m_multiVerseLyricsPositionMap.find(track);
    if (trackIt == m_multiVerseLyricsPositionMap.cend()) {
        return true;
    }

    const int startTick = repeat->tick;
    const int endTick = repeat->endTick();
    const auto start = trackIt->second.lower_bound(startTick);
    const auto end = trackIt->second.lower_bound(endTick);

    return start == end;
}

const ExpressionMap* PlaybackContext::expressionMap(const InstrumentTrackId& trackId) const
{
    const ArticulationMapDataConstPtr data = m_score->articulationMapData();
    return data ? data->map(trackId) : nullptr;
}

std::optional<ArticulationMark> PlaybackContext::articulationMark(const Chord* chord) const
{
    const ArticulationMapDataConstPtr data = m_score->articulationMapData();
    if (!data || data->marks().empty()) {
        return std::nullopt;
    }

    const EID chordId = chord->eid();
    return chordId.isValid() ? data->mark(chordId) : std::nullopt;
}

std::optional<ArticulationMark> PlaybackContext::latchedArticulationMark(const staff_idx_t staffIdx, const int tick) const
{
    auto staffIt = m_latchedArticulationMarksByStaff.find(staffIdx);
    if (staffIt == m_latchedArticulationMarksByStaff.cend()) {
        return std::nullopt;
    }

    auto it = findLessOrEqual(staffIt->second, tick);
    if (it == staffIt->second.cend()) {
        return std::nullopt;
    }

    return it->second;
}

const ExpressionMapEntry* PlaybackContext::articulationInEffect(const InstrumentTrackId& trackId, const staff_idx_t firstStaffIdx,
                                                                const int tick) const
{
    const ExpressionMap* map = expressionMap(trackId);
    if (!map) {
        return nullptr;
    }

    if (const std::optional<ArticulationMark> latched = latchedArticulationMark(firstStaffIdx, tick)) {
        if (const ExpressionMapEntry* entry = map->entry(latched->entryId)) {
            return entry;
        }
    }

    return map->defaultEntryId.empty() ? nullptr : map->entry(map->defaultEntryId);
}

//! NOTE: marks are few, so the whole index is rebuilt on each update: a latched mark affects every following chord
void PlaybackContext::updateLatchedArticulationMarks()
{
    m_latchedArticulationMarksByStaff.clear();

    const ArticulationMapDataConstPtr data = m_score->articulationMapData();
    if (!data) {
        return;
    }

    for (const auto& [chordId, mark] : data->marks()) {
        if (mark.scope != ArticulationMark::Scope::Latched) {
            continue;
        }

        const ChordRest* chord = ArticulationMapData::chordOfMark(m_score->masterScore(), chordId);
        if (!chord) {
            continue;
        }

        m_latchedArticulationMarksByStaff[chord->staffIdx()][chord->tick().ticks()] = mark;
    }
}

//! NOTE: nullopt erases, so that a chord which stopped resolving anything isn't shown with its former articulation
void PlaybackContext::setResolvedArticulation(const track_idx_t trackIdx, const int tick,
                                              const std::optional<ResolvedArticulation>& articulation)
{
    if (articulation) {
        m_resolvedArticulationsByTrack[trackIdx][tick] = *articulation;
        return;
    }

    auto trackIt = m_resolvedArticulationsByTrack.find(trackIdx);
    if (trackIt != m_resolvedArticulationsByTrack.end()) {
        trackIt->second.erase(tick);
    }
}

std::optional<ResolvedArticulation> PlaybackContext::resolvedArticulation(const track_idx_t trackIdx, const int tick) const
{
    auto trackIt = m_resolvedArticulationsByTrack.find(trackIdx);
    if (trackIt == m_resolvedArticulationsByTrack.cend()) {
        return std::nullopt;
    }

    auto it = trackIt->second.find(tick);
    if (it == trackIt->second.cend()) {
        return std::nullopt;
    }

    return it->second;
}
