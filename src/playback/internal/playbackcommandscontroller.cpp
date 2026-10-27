/*
 * SPDX-License-Identifier: GPL-3.0-only
 * MuseScore-Studio-CLA-applies
 *
 * MuseScore Studio
 * Music Composition & Notation
 *
 * Copyright (C) MuseScore Limited and others
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

#include "playbackcommandscontroller.h"

#include <vector>

#include "rcommand/commandtypes.h"
#include "audio/common/audiotypes.h"

#include "notation/imasternotation.h"
#include "notation/inotationautomation.h"
#include "engraving/dom/masterscore.h"
#include "engraving/dom/tempotimeline.h"
#include "project/inotationproject.h"
#include "project/iprojectaudiosyncsettings.h"

#include "audiosyncedits.h"
#include "pitchshifter.h"
#include "referenceaudiofile.h"

#include "../playbackcommands.h"

using namespace muse;
using namespace muse::rcommand;
using namespace muse::audio;
using namespace mu::playback;

void PlaybackCommandsController::init()
{
    registerCommand(PLAY_TOGGLE_COMMAND, &IPlaybackController::togglePlay);
    registerCommand(PLAY_COMMAND, &IPlaybackController::play, false);
    registerCommand(PLAY_SELECTION_COMMAND, &IPlaybackController::playFromSelection, false);
    registerCommand(PAUSE_COMMAND, &IPlaybackController::pause, false);
    registerCommand(PAUSE_AND_SELECT_COMMAND, &IPlaybackController::pause, true);
    registerCommand(STOP_COMMAND, &IPlaybackController::stop);
    registerCommand(REWIND_COMMAND, [this](const rcommand::Params& params) { return rewind(params); });
    registerCommand(LOOP_TOGGLE_COMMAND, &IPlaybackController::toggleLoopPlayback);
    registerCommand(LOOP_IN_COMMAND, &IPlaybackController::addLoopBoundary, LoopBoundaryType::LoopIn);
    registerCommand(LOOP_OUT_COMMAND, &IPlaybackController::addLoopBoundary, LoopBoundaryType::LoopOut);
    registerCommand(METRONOME_TOGGLE_COMMAND, &IPlaybackController::toggleMetronome);
    registerCommand(MIDI_TOGGLE_COMMAND, &IPlaybackController::toggleMidiInput);
    registerCommand(MIDI_START_NOTE_INPUT_TOGGLE_COMMAND, [this]() {
        const bool start = notationConfiguration()->startNoteInputAtSelectedNoteRestWhenPressingMidiKey();
        notationConfiguration()->setStartNoteInputAtSelectedNoteRestWhenPressingMidiKey(!start);
        return muse::make_ok();
    });
    registerCommand(MIDI_INPUT_WRITTEN_PITCH_COMMAND, &IPlaybackController::setMidiUseWrittenPitch, true);
    registerCommand(MIDI_INPUT_SOUNDING_PITCH_COMMAND, &IPlaybackController::setMidiUseWrittenPitch, false);
    registerCommand(REPEATS_TOGGLE_COMMAND, &IPlaybackController::togglePlayRepeats);
    registerCommand(CHORDSYMBOLS_TOGGLE_COMMAND, &IPlaybackController::togglePlayChordSymbols);
    registerCommand(HEAR_PLAYBACK_WHEN_EDITING_TOGGLE_COMMAND, &IPlaybackController::toggleHearPlaybackWhenEditing);
    registerCommand(PAN_TOGGLE_COMMAND, &IPlaybackController::toggleAutomaticallyPan);
    registerCommand(COUNTIN_TOGGLE_COMMAND, &IPlaybackController::toggleCountIn);
    registerCommand(STOP_STAYS_AT_POSITION_COMMAND, [this]() {
        configuration()->setReturnToStartOnStop(false);
        return muse::make_ok();
    });
    registerCommand(STOP_RETURNS_TO_START_COMMAND, [this]() {
        configuration()->setReturnToStartOnStop(true);
        return muse::make_ok();
    });
    registerCommand(OPEN_PLAYBACK_SETUP_COMMAND, [this]() { return showPlaybackSetup(); });
    registerCommand(LOAD_REFERENCE_AUDIO_COMMAND, [this]() { return loadReferenceAudio(); });
    registerCommand(REMOVE_REFERENCE_AUDIO_COMMAND, [this]() { return removeReferenceAudio(); });
    registerCommand(AUDIO_SYNC_TOGGLE_COMMAND, [this]() { return toggleAudioSync(); });
    registerCommand(AUDIO_SYNC_ADD_POINT_COMMAND, [this]() { return addAudioSyncPointAtPlayback(); });
    registerCommand(RELOAD_PLAYBACK_CACHE_COMMAND, &IPlaybackController::reloadPlaybackCache);

    registerCommand(TOGGLE_MIXER_SECTION_COMMAND, [this](const rcommand::Params& params) { return toggleMixerSection(params); });
    registerCommand(TOGGLE_CHANNEL_CATEGORY_COMMAND, [this](const rcommand::Params& params) { return toggleChannelCategory(params); });
    registerCommand(TOGGLE_MIXER_CONDENSED_VIEW_COMMAND, [this]() { return toggleMixerCondensedView(); });
    registerCommand(MIDI_CC_RECORD_TOGGLE_COMMAND, [this]() {
        midiCcRecorder()->toggleArmed();
        return muse::make_ok();
    });
}

muse::Ret PlaybackCommandsController::rewind(const muse::rcommand::Params& params)
{
    double secs = params.at("position").toDouble();
    return playbackController()->rewind(secs_t(secs));
}

muse::Ret PlaybackCommandsController::loadReferenceAudio()
{
    project::INotationProjectPtr project = projectSession()->project();
    if (!project) {
        return muse::make_ret(muse::Ret::Code::NotSupported);
    }

    audiosync::chooseReferenceAudio(interactive().get(), configuration().get(), playbackController().get(), project);
    return muse::make_ok();
}

muse::Ret PlaybackCommandsController::removeReferenceAudio()
{
    project::INotationProjectPtr project = projectSession()->project();
    if (!project || !project->audioSyncSettings()) {
        return muse::make_ret(muse::Ret::Code::NotSupported);
    }

    project->audioSyncSettings()->clearReferenceAudio();
    return muse::make_ok();
}

muse::Ret PlaybackCommandsController::toggleAudioSync()
{
    project::INotationProjectPtr project = projectSession()->project();
    notation::INotationAutomationPtr automation = project && project->masterNotation() ? project->masterNotation()->automation() : nullptr;
    if (!automation) {
        return muse::make_ret(muse::Ret::Code::NotSupported);
    }

    automation->setAudioSyncEnabled(!automation->isAudioSyncEnabled());
    return muse::make_ok();
}

muse::Ret PlaybackCommandsController::addAudioSyncPointAtPlayback()
{
    project::INotationProjectPtr project = projectSession()->project();
    notation::IMasterNotationPtr masterNotation = project ? project->masterNotation() : nullptr;
    notation::INotationAutomationPtr automation = masterNotation ? masterNotation->automation() : nullptr;
    auto playbackState = playbackController();
    const engraving::Score* score = masterNotation ? masterNotation->masterScore() : nullptr;
    project::IProjectAudioSyncSettingsPtr audioSyncSettings = project ? project->audioSyncSettings() : nullptr;
    if (!automation || !automation->isAudioSyncEnabled() || !playbackState || !score || !audioSyncSettings
        || !audioSyncSettings->referenceAudio().isValid()) {
        return muse::make_ret(muse::Ret::Code::NotSupported);
    }

    //! NOTE Audio time heard = (playback time - the track delay) * its played speed + offset: the delay is real time (it
    //! plays the audio later); the beat is the score's one playing now
    const double playbackSecs = playbackState->playbackPosition();
    const engraving::TempoTimeline& timeline = score->tempoTimeline(true);
    const std::optional<int> beat = audiosync::nearestBeatUtick(score, timeline.utime2utick(playbackSecs));
    if (!beat) {
        return muse::make_ret(muse::Ret::Code::NotSupported);
    }

    const double delaySecs = audioSyncSettings->referenceAudio().delayMs / 1000.0;
    const double speed = audiosync::clampTransformSpeed(timeline.tempoMultiplier().val);
    audiosync::addPoint(*automation, score, *beat, automation->audioSyncOffsetSecs() + (playbackSecs - delaySecs) * speed);
    return muse::make_ok();
}

muse::Ret PlaybackCommandsController::showPlaybackSetup()
{
    interactive()->open("musescore://playback/soundprofiles");
    return make_ok();
}

muse::Ret PlaybackCommandsController::toggleMixerSection(const muse::rcommand::Params& params)
{
    MixerSectionType section = str_conv(params.at("section").toString(), MixerSectionType::Unknown);
    if (section == MixerSectionType::Unknown) {
        return make_ret(Ret::Code::BadArgs);
    }

    bool visible = configuration()->isMixerSectionVisible(section);
    configuration()->setMixerSectionVisible(section, !visible);
    return make_ok();
}

muse::Ret PlaybackCommandsController::toggleChannelCategory(const muse::rcommand::Params& params)
{
    const std::optional<ChannelListPanel> panel = channelListPanelFromString(params.at("panel").toString());
    const std::optional<ChannelCategory> category = channelCategoryFromString(params.at("category").toString());
    if (!panel || !category) {
        return make_ret(Ret::Code::BadArgs);
    }

    bool visible = configuration()->isChannelCategoryVisible(*panel, *category);
    configuration()->setChannelCategoryVisible(*panel, *category, !visible);
    return make_ok();
}

muse::Ret PlaybackCommandsController::toggleMixerCondensedView()
{
    bool enabled = configuration()->isMixerCondensedViewEnabled();
    configuration()->setMixerCondensedViewEnabled(!enabled);
    return make_ok();
}

void PlaybackCommandsController::registerCommand(const muse::rcommand::Command& command, const std::function<Ret()>& handler)
{
    dispatcher()->onRequest(this, command, [this, command, handler]() {
        if (!commandsState()->commandState(command).enabled) {
            return muse::make_ret(Ret::Code::NotSupported);
        }

        return handler();
    });
}

void PlaybackCommandsController::registerCommand(const muse::rcommand::Command& command,
                                                 const std::function<muse::Ret(const muse::rcommand::Params&)>& handler)
{
    dispatcher()->onRequest(this, command, [this, command, handler](const muse::rcommand::Params& params) {
        if (!commandsState()->commandState(command).enabled) {
            return muse::make_ret(Ret::Code::NotSupported);
        }

        return handler(params);
    });
}

void PlaybackCommandsController::registerCommand(const muse::rcommand::Command& command, Ret (IPlaybackController::* handler)())
{
    registerCommand(command, [this, handler]()
    {
        return (playbackController().get()->*handler)();
    });
}

template<typename P1>
void PlaybackCommandsController::registerCommand(const muse::rcommand::Command& command, Ret (IPlaybackController::* handler)(P1), P1 p1)
{
    registerCommand(command, [this, handler, p1]()
    {
        return (playbackController().get()->*handler)(p1);
    });
}
