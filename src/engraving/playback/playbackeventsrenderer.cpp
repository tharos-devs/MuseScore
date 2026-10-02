/*
 * SPDX-License-Identifier: GPL-3.0-only
 * MuseScore-Studio-CLA-applies
 *
 * MuseScore Studio
 * Music Composition & Notation
 *
 * Copyright (C) 2025 MuseScore Limited and others
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

#include "playbackeventsrenderer.h"

#include "log.h"

#include "dom/chord.h"
#include "dom/harmony.h"
#include "dom/measure.h"
#include "dom/note.h"
#include "dom/part.h"
#include "dom/masterscore.h"
#include "dom/sig.h"
#include "dom/staff.h"
#include "dom/utils.h"

#include "types/bps.h"

#include "utils/arrangementutils.h"

#include "metaparsers/chordarticulationsparser.h"
#include "metaparsers/notearticulationsparser.h"

#include "renderers/chordarticulationsrenderer.h"

#include "filters/chordfilter.h"

#include "engraving/articulationmap/articulationmaptypes.h"

using namespace mu::engraving;
using namespace muse;
using namespace muse::mpe;

static ArticulationMap makeStandardArticulationMap(const ArticulationsProfilePtr profile, timestamp_t timestamp, duration_t duration)
{
    IF_ASSERT_FAILED(profile) {
        return {};
    }

    ArticulationMeta meta(ArticulationType::Standard,
                          profile->pattern(ArticulationType::Standard),
                          timestamp,
                          duration,
                          0,
                          0);

    ArticulationMap articulations;
    articulations.emplace(ArticulationType::Standard, mpe::ArticulationAppliedData(std::move(meta), 0, mpe::HUNDRED_PERCENT));
    articulations.preCalculateAverageData();

    return articulations;
}

static muse::mpe::NoteEvent buildMetronomeEvent(const TimeSigFrac& timeSig, const double bps,
                                                const BeatType beatType, const muse::mpe::timestamp_t actualTimestamp,
                                                const muse::mpe::ArticulationsProfilePtr profile)
{
    int ticksPerBeat = timeSig.ticks() / timeSig.numerator();
    duration_t duration = durationFromTempoAndTicks(bps, ticksPerBeat);

    pitch_level_t eventPitchLevel = beatType == BeatType::DOWNBEAT
                                    ? pitchLevel(PitchClass::E, 5) // high wood block
                                    : pitchLevel(PitchClass::F, 5); // low wood block

    const ArticulationMap articulations = makeStandardArticulationMap(profile, actualTimestamp, duration);

    return mpe::NoteEvent(actualTimestamp,
                          duration,
                          0,
                          0,
                          eventPitchLevel,
                          dynamicLevelFromType(mpe::DynamicType::mf),
                          articulations,
                          bps);
}

//! NOTE: resolves which articulation of the instrument's articulation map this chord plays, by priority:
//! a mark on the chord itself > a score articulation mapped by alias > the staff's latest latched mark > the map's default,
//! and emits it with the chord's notes. The audio side skips it when it doesn't change from the previous chord
//! A keyswitch switches the whole plugin, so for an instrument on several staves (piano, harp...),
//! only its first staff drives the articulations - the others follow it
static void appendArticulationMapEvent(const Chord* chord, const RenderingContext& ctx, PlaybackEventList& events,
                                       bool recordResolved)
{
    const ExpressionMap* map = ctx.playbackCtx->expressionMap(makeInstrumentTrackId(chord));
    if (!map) {
        return;
    }

    const Part* part = chord->part();
    if (!part || chord->staffIdx() != part->trackRange().startTrack / VOICES) {
        return;
    }

    const ExpressionMapEntry* entry = nullptr;
    ResolvedArticulation::Source source = ResolvedArticulation::Source::Default;
    int tickOffset = 0;

    if (const std::optional<ArticulationMark> ownMark = ctx.playbackCtx->articulationMark(chord)) {
        entry = map->entry(ownMark->entryId);
        tickOffset = entry ? ownMark->tickOffset : 0;
        source = ResolvedArticulation::Source::OwnMark;
    }

    if (!entry) {
        entry = map->entryForArticulations(ctx.commonArticulations);
        source = ResolvedArticulation::Source::Alias;
    }

    if (!entry) {
        if (const std::optional<ArticulationMark> latched = ctx.playbackCtx->latchedArticulationMark(chord->staffIdx(),
                                                                                                     chord->tick().ticks())) {
            entry = map->entry(latched->entryId);
            source = ResolvedArticulation::Source::Latched;
        }
    }

    if (!entry && !map->defaultEntryId.empty()) {
        entry = map->entry(map->defaultEntryId);
        source = ResolvedArticulation::Source::Default;
    }

    //! NOTE: an articulation may only change the channel (e.g. one instrument per channel in Kontakt)
    const bool resolved = entry && (!entry->messages.empty() || entry->channel);

    if (recordResolved) {
        ctx.playbackCtx->setResolvedArticulation(chord->track(), chord->tick().ticks(),
                                                 resolved ? std::optional<ResolvedArticulation>({ entry->id, source }) : std::nullopt);
    }

    if (!resolved) {
        return;
    }

    const int chordTick = chord->tick().ticks() + ctx.positionTickOffset;
    const timestamp_t tickOffsetDuration = tickOffset != 0
                                           ? timestampFromTicks(ctx.score,
                                                                chordTick + tickOffset) - timestampFromTicks(ctx.score, chordTick)
                                           : timestamp_t(0);

    mpe::MidiMessagesEvent event;
    event.messages = entry->messages;
    event.messagesOffset = tickOffsetDuration + timestamp_t(map->keyswitchOffsetMsFor(*entry) * 1000);
    event.notesOffset = timestamp_t(entry->notesOffsetMs * 1000);
    event.channel = entry->channel.value_or(-1);
    event.layerIdx = static_cast<mpe::layer_idx_t>(chord->track());

    events.emplace_back(std::move(event));
}

void PlaybackEventsRenderer::render(const EngravingItem* item, const int tickPositionOffset,
                                    const ArticulationsProfilePtr profile, const PlaybackContextPtr playbackCtx,
                                    PlaybackEventsMap& result) const
{
    IF_ASSERT_FAILED(item->isChord()) {
        return;
    }

    renderNoteEvents(toChord(item), tickPositionOffset, profile, playbackCtx, result);
}

void PlaybackEventsRenderer::render(const EngravingItem* item, const mpe::timestamp_t actualTimestamp,
                                    const mpe::duration_t actualDuration, const mpe::dynamic_level_t actualDynamicLevel,
                                    const PlaybackContextPtr playbackCtx, const ArticulationsProfilePtr profile,
                                    PlaybackEventsMap& result) const
{
    const Chord* chord = nullptr;

    if (item->isChord()) {
        chord = toChord(item);
        mpe::PlaybackEventList& events = result[actualTimestamp];

        for (const Note* note : chord->notes()) {
            renderFixedNoteEvent(note, actualTimestamp, actualDuration,
                                 actualDynamicLevel, playbackCtx, profile, events);
        }
    } else if (item->isNote()) {
        chord = toNote(item)->chord();
        renderFixedNoteEvent(toNote(item), actualTimestamp, actualDuration,
                             actualDynamicLevel, playbackCtx, profile, result[actualTimestamp]);
    } else {
        UNREACHABLE;
    }

    //! NOTE: a note played on a MIDI keyboard (outside note input) is a temporary one, outside the score:
    //! it plays with the articulation of the selected chord of its staff (keyswitch, channel)
    if (chord && chord->track() == muse::nidx && item->isNote()) {
        const ChordRest* selected = chord->score() ? chord->score()->inputState().cr() : nullptr;
        chord = selected && selected->isChord() && selected->staffIdx() == toNote(item)->staffIdx() ? toChord(selected) : nullptr;
    }

    //! NOTE: so that auditioning a note (e.g. clicking it) plays it with its own articulation
    //! Marks and staff indices belong to the master score, while the auditioned item may come from a part
    if (chord && !chord->score()->isMaster()) {
        const EngravingItem* linked = chord->findLinkedInScore(chord->masterScore());
        chord = linked && linked->isChord() ? toChord(linked) : nullptr;
    }

    // Not for a key released (duration 0): its keyswitch would be sent again, and the audio side
    // already knows which channel the key was played on
    if (chord && actualDuration > 0 && playbackCtx->expressionMap(makeInstrumentTrackId(chord))) {
        const Score* score = chord->score();
        const int tick = chord->tick().ticks();
        const int tickOffset = score ? score->repeatList().tick2utick(tick) - tick : 0;

        RenderingContext ctx = engraving::buildRenderingCtx(chord, tickOffset, profile, playbackCtx);
        ChordArticulationsParser::buildChordArticulationMap(chord, ctx, ctx.commonArticulations);
        appendArticulationMapEvent(chord, ctx, result[actualTimestamp], false /*recordResolved*/);
    }
}

void PlaybackEventsRenderer::renderChordSymbol(const Harmony* chordSymbol,
                                               const int ticksPositionOffset,
                                               const mpe::ArticulationsProfilePtr profile,
                                               const PlaybackContextPtr playbackCtx,
                                               mpe::PlaybackEventsMap& result) const
{
    if (!chordSymbol->isRealizable()) {
        return;
    }

    const Staff* staff = chordSymbol->staff();
    IF_ASSERT_FAILED(staff) {
        return;
    }

    const RealizedHarmony& realized = chordSymbol->getRealizedHarmony();
    const RealizedHarmony::PitchMap& notes = realized.notes();

    const Score* score = chordSymbol->score();
    int positionTick = chordSymbol->tick().ticks();
    int positionTickWithOffset = positionTick + ticksPositionOffset;

    timestamp_t eventTimestamp = timestampFromTicks(score, positionTickWithOffset);
    PlaybackEventList& events = result[eventTimestamp];

    int durationTicks = realized.getActualDuration(positionTickWithOffset).ticks();
    duration_t duration = timestampFromTicks(score, positionTickWithOffset + durationTicks) - eventTimestamp;

    voice_layer_idx_t voiceIdx = static_cast<voice_layer_idx_t>(chordSymbol->voice());
    staff_layer_idx_t staffIdx = static_cast<staff_layer_idx_t>(chordSymbol->staffIdx());
    Key key = staff->key(chordSymbol->tick());

    ArticulationMap articulations = makeStandardArticulationMap(profile, eventTimestamp, duration);

    double bps = score->multipliedTempoAtUtick(positionTickWithOffset).val;

    for (auto it = notes.cbegin(); it != notes.cend(); ++it) {
        int pitch = it->first;
        int tpc = pitch2tpc(pitch, key, Prefer::NEAREST);
        int octave = playingOctave(pitch, tpc);
        pitch_level_t pitchLevel = notePitchLevel(tpc, octave);

        events.emplace_back(mpe::NoteEvent(eventTimestamp,
                                           duration,
                                           voiceIdx,
                                           staffIdx,
                                           pitchLevel,
                                           playbackCtx->appliableDynamicLevel(chordSymbol->track(), positionTickWithOffset),
                                           articulations,
                                           bps));
    }
}

void PlaybackEventsRenderer::renderChordSymbol(const Harmony* chordSymbol, const mpe::timestamp_t actualTimestamp,
                                               const mpe::duration_t actualDuration, const mpe::dynamic_level_t actualDynamicLevel,
                                               const ArticulationsProfilePtr profile, mpe::PlaybackEventsMap& result) const
{
    if (!chordSymbol->isRealizable()) {
        return;
    }

    const Staff* staff = chordSymbol->staff();
    IF_ASSERT_FAILED(staff) {
        return;
    }

    const RealizedHarmony& realized = chordSymbol->getRealizedHarmony();
    const RealizedHarmony::PitchMap& notes = realized.notes();

    PlaybackEventList& events = result[actualTimestamp];

    voice_layer_idx_t voiceIdx = static_cast<voice_layer_idx_t>(chordSymbol->voice());
    staff_layer_idx_t staffIdx = static_cast<staff_layer_idx_t>(chordSymbol->staffIdx());
    Key key = staff->key(chordSymbol->tick());

    ArticulationMap articulations = makeStandardArticulationMap(profile, actualTimestamp, actualDuration);

    for (auto it = notes.cbegin(); it != notes.cend(); ++it) {
        int pitch = it->first;
        int tpc = pitch2tpc(pitch, key, Prefer::NEAREST);
        int octave = playingOctave(pitch, tpc);
        pitch_level_t pitchLevel = notePitchLevel(tpc, octave);

        events.emplace_back(mpe::NoteEvent(actualTimestamp,
                                           actualDuration,
                                           voiceIdx,
                                           staffIdx,
                                           pitchLevel,
                                           actualDynamicLevel,
                                           articulations,
                                           Constants::DEFAULT_TEMPO.val));
    }
}

void PlaybackEventsRenderer::renderMetronome(const Score* score, const Measure* measure, const int ticksPositionOffset,
                                             const muse::mpe::ArticulationsProfilePtr profile, mpe::PlaybackEventsMap& result) const
{
    IF_ASSERT_FAILED(score) {
        return;
    }

    int measureStartTick = measure->tick().ticks();
    int measureEndTick = measure->endTick().ticks();

    TimeSigFrac timeSignatureFraction = score->sigmap()->timesig(measureStartTick).nominal();

    const BeatsPerSecond measureBps = score->multipliedTempoAtUtick(measureStartTick + ticksPositionOffset);
    const int step = timeSignatureFraction.isBeatedCompound(measureBps.val)
                     ? timeSignatureFraction.beatTicks() : timeSignatureFraction.dUnitTicks();

    int startTick = measureStartTick;
    int rtick = 0;

    if (measure->isAnacrusis()) {
        int remainingTicks = measure->ticks().ticks() % step;
        startTick += remainingTicks;
        rtick = remainingTicks + timeSignatureFraction.ticksPerMeasure() - measure->ticks().ticks();
    }

    // Tempo can change mid-measure (automation point or ramp), so re-read it for every click
    // rather than reusing the tempo from the start of the measure; the click grid itself stays fixed
    for (int tick = startTick; tick < measureEndTick; tick += step, rtick += step) {
        const BeatsPerSecond bps = score->multipliedTempoAtUtick(tick + ticksPositionOffset);

        timestamp_t eventTimestamp = timestampFromTicks(score, tick + ticksPositionOffset);
        BeatType beatType = timeSignatureFraction.rtick2beatType(rtick);
        mpe::NoteEvent event = buildMetronomeEvent(timeSignatureFraction, bps.val, beatType, eventTimestamp, profile);

        result[eventTimestamp].emplace_back(std::move(event));
    }
}

void PlaybackEventsRenderer::renderMetronome(const Score* score, const int tick, const mpe::timestamp_t actualTimestamp,
                                             const muse::mpe::ArticulationsProfilePtr profile, mpe::PlaybackEventsMap& result) const
{
    IF_ASSERT_FAILED(score) {
        return;
    }

    TimeSigFrac timeSignatureFraction = score->sigmap()->timesig(tick).timesig();
    BeatsPerSecond bps = score->multipliedTempo(Fraction::fromTicks(tick));
    BeatType beatType = score->tick2beatType(Fraction::fromTicks(tick));
    mpe::NoteEvent event = buildMetronomeEvent(timeSignatureFraction, bps.val, beatType, actualTimestamp, profile);

    result[actualTimestamp].emplace_back(std::move(event));
}

void PlaybackEventsRenderer::renderCountIn(const Score* score, const int startTick, const muse::mpe::timestamp_t actualTimestamp,
                                           const muse::mpe::ArticulationsProfilePtr profile,
                                           muse::mpe::PlaybackEventsMap& result, muse::mpe::duration_t& countInDuration) const
{
    const Measure* measure = score->tick2measure(Fraction::fromTicks(startTick));
    if (!measure) {
        return;
    }

    int measureStartTick = measure->tick().ticks();
    TimeSigFrac timeSignatureFraction = score->sigmap()->timesig(measureStartTick).nominal();
    BeatsPerSecond bps = score->multipliedTempo(Fraction::fromTicks(measureStartTick));
    int ticksPerMeasure = timeSignatureFraction.ticksPerMeasure();

    int step = timeSignatureFraction.isBeatedCompound(bps.val)
               ? timeSignatureFraction.beatTicks() : timeSignatureFraction.dUnitTicks();

    duration_t stepDuration = durationFromTempoAndTicks(bps.val, step);

    // Add extra clicks if...
    int endTick = ticksPerMeasure + (startTick - measureStartTick); // ... not starting playback at beginning of measure
    int remainingTicks = 0;

    if (measure->isAnacrusis()) { // ... measure is incomplete (anacrusis)
        int measureTicks = measure->ticks().ticks();
        endTick += ticksPerMeasure - measureTicks;
        remainingTicks = measureTicks % step;
    }

    MeasureBeat measureBeat = findBeat(score, startTick);
    int closestMainBeatTick = score->sigmap()->bar2tick(measureBeat.measureIndex, std::ceil(measureBeat.beat));
    remainingTicks += closestMainBeatTick - startTick;

    timestamp_t eventTimestamp = actualTimestamp;

    for (int tick = 0; tick < endTick; tick += step) {
        int rtick = tick % ticksPerMeasure;
        BeatType beatType = timeSignatureFraction.rtick2beatType(rtick);
        mpe::NoteEvent event = buildMetronomeEvent(timeSignatureFraction, bps.val, beatType, eventTimestamp, profile);

        result[eventTimestamp].emplace_back(std::move(event));
        eventTimestamp += stepDuration;
    }

    countInDuration = eventTimestamp - actualTimestamp;
    if (remainingTicks > 0) {
        countInDuration -= durationFromTempoAndTicks(bps.val, remainingTicks);
    }
}

void PlaybackEventsRenderer::renderNoteEvents(const Chord* chord, const int tickPositionOffset,
                                              const mpe::ArticulationsProfilePtr profile, const PlaybackContextPtr playbackCtx,
                                              PlaybackEventsMap& result) const
{
    IF_ASSERT_FAILED(chord) {
        return;
    }

    RenderingContext ctx = engraving::buildRenderingCtx(chord, tickPositionOffset, profile, playbackCtx);

    if (!ChordFilter::isItemPlayable(chord, ctx)) {
        return;
    }

    ChordArticulationsParser::buildChordArticulationMap(chord, ctx, ctx.commonArticulations);

    PlaybackEventList newEvents;
    ChordArticulationsRenderer::render(chord, ArticulationType::Last, ctx, newEvents);

    if (!newEvents.empty()) {
        PlaybackEventList& list = result[ctx.nominalTimestamp];
        list.insert(list.end(), std::make_move_iterator(newEvents.begin()), std::make_move_iterator(newEvents.end()));
        appendArticulationMapEvent(chord, ctx, list, true /*recordResolved*/);
    }
}

void PlaybackEventsRenderer::renderFixedNoteEvent(const Note* note, const mpe::timestamp_t actualTimestamp,
                                                  const mpe::duration_t actualDuration,
                                                  const mpe::dynamic_level_t actualDynamicLevel,
                                                  const PlaybackContextPtr playbackCtx,
                                                  const mpe::ArticulationsProfilePtr profile, mpe::PlaybackEventList& result) const
{
    static const ArticulationMap articulations;

    const Score* score = note->score();
    const int durationTicks = ticksFromTempoAndDuration(Constants::DEFAULT_TEMPO.val, actualDuration);
    const int tick = note->tick().ticks();
    const int utick = score ? score->repeatList().tick2utick(tick) : tick;
    const int tickOffset = utick - tick;

    RenderingContext ctx{ actualTimestamp,
                          actualDuration,
                          actualDynamicLevel,
                          tick, /*nominalPositionStartTick*/
                          durationTicks, /*nominalPositionEndTick*/
                          durationTicks, /*nominalDurationTicks*/
                          tickOffset,
                          Constants::DEFAULT_TEMPO,
                          TimeSigMap::DEFAULT_TIME_SIGNATURE,
                          articulations,
                          score,
                          profile,
                          playbackCtx };

    NoteArticulationsParser::parsePlayingTechnique(ctx, note->track(), ctx.commonArticulations, false /*sustainAllowed*/);
    NoteArticulationsParser::parseGhostNote(note, ctx, ctx.commonArticulations);
    NoteArticulationsParser::parseNoteHead(note, ctx, ctx.commonArticulations);
    NoteArticulationsParser::parseSymbols(note, ctx, ctx.commonArticulations);

    if (ctx.commonArticulations.empty()) {
        ctx.commonArticulations = makeStandardArticulationMap(profile, actualTimestamp, actualDuration);
    } else {
        ctx.commonArticulations.preCalculateAverageData();
    }

    NominalNoteCtx noteCtx(note, ctx);
    result.emplace_back(buildNoteEvent(noteCtx));
}
