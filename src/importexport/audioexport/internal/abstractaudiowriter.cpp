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

#include "abstractaudiowriter.h"

#include <chrono>
#include <functional>

#include <QCoreApplication>
#include <QFile>
#include <QFileInfo>
#include <QThread>

#include "global/containers.h"
#include "log.h"

#include "notation/inotation.h"

using namespace muse;
using namespace muse::audio;
using namespace mu::iex::audioexport;
using namespace mu::project;
using namespace mu::notation;

std::vector<INotationWriter::UnitType> AbstractAudioWriter::supportedUnitTypes() const
{
    return { UnitType::PER_PART };
}

bool AbstractAudioWriter::supportsUnitType(UnitType unitType) const
{
    std::vector<UnitType> unitTypes = supportedUnitTypes();
    return std::find(unitTypes.cbegin(), unitTypes.cend(), unitType) != unitTypes.cend();
}

Ret AbstractAudioWriter::write(INotationPtr, io::IODevice&, const Options& options)
{
    IF_ASSERT_FAILED(unitTypeFromOptions(options) != UnitType::MULTI_PART) {
        return Ret(Ret::Code::NotSupported);
    }

    if (supportsUnitType(muse::value(options, OptionKey::UNIT_TYPE, Val(UnitType::PER_PAGE)).toEnum<UnitType>())) {
        NOT_IMPLEMENTED;
        return Ret(Ret::Code::NotImplemented);
    }

    NOT_SUPPORTED;
    return Ret(Ret::Code::NotSupported);
}

Ret AbstractAudioWriter::writeList(const INotationPtrList&, io::IODevice&, const Options& options)
{
    IF_ASSERT_FAILED(unitTypeFromOptions(options) == UnitType::MULTI_PART) {
        return Ret(Ret::Code::NotSupported);
    }

    if (supportsUnitType(muse::value(options, OptionKey::UNIT_TYPE, Val(UnitType::PER_PAGE)).toEnum<UnitType>())) {
        NOT_IMPLEMENTED;
        return Ret(Ret::Code::NotImplemented);
    }

    NOT_SUPPORTED;
    return Ret(Ret::Code::NotSupported);
}

void AbstractAudioWriter::abort()
{
    //! NOTE Also stops the waits before the rendering starts (see doWriteAndWait())
    m_abortRequested = true;

    muse::ContextInject<muse::audio::IPlayback> playback = { m_iocContext };
    playback()->abortSavingAllSoundTracks();
    m_writeRet = make_ret(Ret::Code::Cancel);
    m_isCompleted = true;
}

muse::Progress* AbstractAudioWriter::progress()
{
    return &m_progress;
}

Ret AbstractAudioWriter::doWriteAndWait(INotationPtr notation,
                                        io::IODevice& dstDevice,
                                        const SoundTrackFormat& format,
                                        const Options& options)
{
    //! NOTE Temporary fix for the context injection
    m_iocContext = notation->iocContext();
    m_abortRequested = false;

    muse::ContextInject<playback::IPlaybackController> playbackController = { m_iocContext };

    //! NOTE Canceled while waiting below: the rendering never starts
    auto canceled = [this]() {
        m_writeRet = make_ret(Ret::Code::Cancel);
        m_isCompleted = true;
        m_progress.finish(m_writeRet);
        return m_writeRet;
    };

    enum class WaitResult {
        Ready,
        Canceled,
        TimedOut,
    };

    //! NOTE Processes events until isReady(), the export is canceled or maxWait (if not zero) has elapsed
    auto waitUntil = [this](const std::function<bool()>& isReady, std::chrono::seconds maxWait = std::chrono::seconds(0)) {
        const auto waitStart = std::chrono::steady_clock::now();
        while (!isReady()) {
            if (m_abortRequested) {
                return WaitResult::Canceled;
            }
            if (maxWait.count() > 0 && std::chrono::steady_clock::now() - waitStart > maxWait) {
                return WaitResult::TimedOut;
            }
            application()->processEvents();
            QThread::yieldCurrentThread();
        }
        return WaitResult::Ready;
    };

    //! NOTE Waiting for the audio system to start if it is not already running
    if (waitUntil([this]() { return startAudioController()->isAudioStarted(); }) == WaitResult::Canceled) {
        return canceled();
    }

    //! NOTE Playback (tracks and duration) loads asynchronously; rendering before it is
    //! ready yields 0 tracks and 0 duration ("No audio to export"). Wait for it first.
    if (waitUntil([&playbackController]() { return playbackController()->isPlaybackInited(); }) == WaitResult::Canceled) {
        return canceled();
    }

    //! NOTE Same for the attached video's track (its audio is decoded and loaded asynchronously too), only when
    //! it's part of this export. When it's what the export is about, it must never be silently missing: a long
    //! wait (a long video's audio being decoded), then an explicit failure; otherwise a shorter one, then without it
    const bool forceSoundTracks = muse::value(options, OptionKey::INCLUDE_SOUND_TRACKS, Val(false)).toBool();
    if (forceSoundTracks || playbackController()->isVideoAudioIncludedInExport()) {
        const auto maxWait = forceSoundTracks ? std::chrono::seconds(300) : std::chrono::seconds(60);
        const WaitResult result = waitUntil([&playbackController]() { return playbackController()->isVideoTrackReady(); }, maxWait);
        if (result == WaitResult::Canceled) {
            return canceled();
        }
        if (result == WaitResult::TimedOut) {
            if (forceSoundTracks) {
                LOGE() << "the attached video's audio isn't ready";
                m_writeRet = make_ret(Ret::Code::UnknownError, std::string("the attached video's audio isn't ready"));
                m_isCompleted = true;
                m_progress.finish(m_writeRet);
                return m_writeRet;
            }
            LOGW() << "the attached video's audio isn't ready, exported without it";
        }
    }

    //! NOTE Same for the Audio sync window's reference audio (decoded, maybe transposed), never what the export is about
    if (forceSoundTracks || playbackController()->isReferenceAudioIncludedInExport()) {
        const WaitResult result = waitUntil([&playbackController]() { return playbackController()->isReferenceAudioTrackReady(); },
                                            std::chrono::seconds(60));
        if (result == WaitResult::Canceled) {
            return canceled();
        }
        if (result == WaitResult::TimedOut) {
            LOGW() << "the reference audio isn't ready, exported without it";
        }
    }

    m_isCompleted = false;
    m_writeRet = muse::Ret();

    playbackController()->setNotation(notation);
    playbackController()->setIsExportingAudio(true);

    SoundTrackFormat actualFormat = format;

    double leadingSilenceSec = muse::value(options, OptionKey::LEADING_SILENCE_SEC, Val(0.0)).toDouble();
    actualFormat.leadingSilenceDuration = std::isfinite(leadingSilenceSec) ? secs_t(leadingSilenceSec) : secs_t(0.0);

    double trailingSilenceSec = muse::value(options, OptionKey::TRAILING_SILENCE_SEC, Val(0.0)).toDouble();
    actualFormat.trailingSilenceDuration = std::isfinite(trailingSilenceSec) ? secs_t(trailingSilenceSec) : secs_t(0.0);

    const double durationSec = muse::value(options, OptionKey::AUDIO_DURATION_SEC, Val(0.0)).toDouble();
    actualFormat.duration = std::isfinite(durationSec) && durationSec > 0.0 ? durationSec : 0.0;

    actualFormat.includeSoundTracks = forceSoundTracks;

    doWrite(dstDevice, actualFormat);

    const bool waitForCompletion = muse::value(options, OptionKey::WAIT_FOR_COMPLETION, Val(true)).toBool();
    if (waitForCompletion) {
        while (!m_isCompleted) {
            application()->processEvents();
            QThread::yieldCurrentThread();
        }
    }

    return m_writeRet;
}

void AbstractAudioWriter::doWrite(io::IODevice& dstDevice, const SoundTrackFormat& format)
{
    muse::ContextInject<muse::audio::IPlayback> playbackInj = { m_iocContext };

    const std::string processingOnlineSoundsMsg = trc("iex_audio", "Processing online sounds…");

    muse::ContextInject<project::IProjectSession> projectSession = { m_iocContext };
    m_notationForRestore = projectSession()->currentNotation();

    auto restorePlaybackState = [this]() {
        muse::ContextInject<playback::IPlaybackController> playbackController = { m_iocContext };
        playbackController()->setIsExportingAudio(false);
        playbackController()->setNotation(m_notationForRestore);
    };

    auto sendProgress = [this, processingOnlineSoundsMsg](int64_t current, int64_t total, SaveSoundTrackStage stage) {
        switch (stage) {
        case SaveSoundTrackStage::ProcessingOnlineSounds:
            m_progress.progress(current, total, processingOnlineSoundsMsg);
            break;
        case SaveSoundTrackStage::WritingSoundTrack:
        case SaveSoundTrackStage::Unknown:
            m_progress.progress(current, total);
            break;
        }
    };

    m_progress.start();

    auto playback = playbackInj();

    playback->saveSoundTrackProgressChanged()
    .onReceive(this, [sendProgress](int64_t current, int64_t total, SaveSoundTrackStage stage) {
        sendProgress(current, total, stage);
    });

    playback->saveSoundTrack(std::move(format), dstDevice)
    .onResolve(this, [this, playback, restorePlaybackState](const bool /*result*/) {
        LOGI() << "Successfully saved sound track";

        restorePlaybackState();

        m_writeRet = muse::make_ok();
        m_isCompleted = true;
        m_progress.finish(muse::make_ok());
        playback->saveSoundTrackProgressChanged().disconnect(this);
    })
    .onReject(this, [this, playback, restorePlaybackState](int errorCode, const std::string& msg) {
        restorePlaybackState();

        m_writeRet = Ret(errorCode, msg);
        m_isCompleted = true;
        m_progress.finish(make_ret(errorCode, msg));
        playback->saveSoundTrackProgressChanged().disconnect(this);
    });
}

INotationWriter::UnitType AbstractAudioWriter::unitTypeFromOptions(const Options& options) const
{
    std::vector<UnitType> supported = supportedUnitTypes();
    IF_ASSERT_FAILED(!supported.empty()) {
        return UnitType::PER_PART;
    }

    UnitType defaultUnitType = supported.front();
    UnitType unitType = muse::value(options, OptionKey::UNIT_TYPE, Val(defaultUnitType)).toEnum<UnitType>();
    if (!supportsUnitType(unitType)) {
        return defaultUnitType;
    }

    return unitType;
}
