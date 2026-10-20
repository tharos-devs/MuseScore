/*
 * SPDX-License-Identifier: GPL-3.0-only
 * MuseScore-CLA-applies
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

#include "audiosyncview.h"

#include <algorithm>
#include <cmath>

#include <thread>

#include <QCoreApplication>
#include <QFileInfo>
#include <QPointer>
#include <QPainter>
#include <QPainterPath>

#include "engraving/dom/masterscore.h"
#include "engraving/dom/measure.h"
#include "engraving/dom/repeatlist.h"
#include "engraving/dom/score.h"
#include "engraving/dom/select.h"
#include "engraving/dom/sig.h"
#include "engraving/dom/tempotimeline.h"
#include "engraving/automation/automationdata.h"
#include "notation/imasternotation.h"
#include "notation/inotation.h"
#include "notation/inotationautomation.h"
#include "notation/inotationinteraction.h"
#include "notation/inotationplayback.h"
#include "project/inotationproject.h"

#include "playback/internal/beatdetection.h"
#include "playback/internal/mappedwav.h"
#include "playback/internal/referenceaudiofile.h"

#include "referenceaudiopeaks.h"

#include "translation.h"
#include "log.h"

using namespace mu::playback;
using namespace mu::engraving;
using namespace muse;

namespace {
constexpr double RULER_ROW_HEIGHT = 18.0;
constexpr double RAIL_HEIGHT = 20.0; // audio sync points
constexpr double RULER_HEIGHT = 2 * RULER_ROW_HEIGHT + RAIL_HEIGHT; // above the waveform: measures, time signatures, points
constexpr double POINT_HIT_DISTANCE = 6.0;
const QColor LIMIT_COLOR(0xE5, 0x48, 0x4D); // a point stopped by a tempo limit
constexpr double LOOP_BAND_HEIGHT = 10.0; // the A-B loop, at the top of the waveform
constexpr double LOOP_HANDLE_SIZE = 8.0;
constexpr double SEEK_DRAG_LOOP_DISTANCE = 4.0; // px: a click moving less only moves the playback position
constexpr double NUDGE_SECS = 0.001;
constexpr double LARGE_NUDGE_SECS = 0.010;
constexpr double OVERVIEW_HEIGHT = 34.0;
constexpr double OVERVIEW_GAP = 8.0;
constexpr double MIN_PIXELS_PER_SECOND = 0.5;
constexpr double MAX_PIXELS_PER_SECOND = 48000.0; // ~1 px per sample
constexpr double ZOOM_STEP = 1.41421356237;
constexpr double MIN_AMPLITUDE_ZOOM = 0.5;
constexpr double MAX_AMPLITUDE_ZOOM = 8.0;
constexpr int CURSOR_INTERVAL_MS = 16;

//! NOTE The transport's clock: mm:ss.cc
QString formatClock(double secs)
{
    const qint64 totalCs = static_cast<qint64>(std::llround(std::max(0.0, secs) * 100.0));
    return QString("%1:%2.%3").arg(totalCs / 6000, 2, 10, QLatin1Char('0'))
           .arg((totalCs / 100) % 60, 2, 10, QLatin1Char('0'))
           .arg(totalCs % 100, 2, 10, QLatin1Char('0'));
}

QString formatTime(double secs, bool withMs)
{
    const bool negative = secs < 0;
    const qint64 totalMs = static_cast<qint64>(std::llround(std::abs(secs) * 1000.0));
    const qint64 minutes = totalMs / 60000;
    const qint64 seconds = (totalMs / 1000) % 60;
    const qint64 ms = totalMs % 1000;

    QString result = QString("%1:%2").arg(minutes).arg(seconds, 2, 10, QLatin1Char('0'));
    if (withMs) {
        result += QString(".%1").arg(ms, 3, 10, QLatin1Char('0'));
    }
    return negative ? "-" + result : result;
}
}

AudioSyncView::AudioSyncView(QQuickItem* parent)
    : QQuickPaintedItem(parent), muse::Contextable(muse::iocCtxForQmlObject(this))
{
    setAcceptedMouseButtons(Qt::LeftButton);
    setAntialiasing(true);
    setFlag(QQuickItem::ItemIsFocusScope);
    setActiveFocusOnTab(true);

    m_peaks = new ReferenceAudioPeaks(this);
    connect(m_peaks, &ReferenceAudioPeaks::readyChanged, this, [this]() {
        if (m_peaks->isReady() && m_scrollSecs < 0) {
            restoreViewState();
        }
        emit decodeStateChanged();
        emit viewChanged();
        updatePositionText();
        update();
    });

    //! NOTE The zoom and position are saved with the project (see IProjectAudioSyncSettings::viewState())
    connect(this, &AudioSyncView::viewChanged, this, &AudioSyncView::storeViewState);

    m_cursorTimer.setInterval(CURSOR_INTERVAL_MS);
    connect(&m_cursorTimer, &QTimer::timeout, this, &AudioSyncView::updatePlaybackCursor);
}

AudioSyncView::~AudioSyncView() = default;

void AudioSyncView::componentComplete()
{
    QQuickPaintedItem::componentComplete();

    globalContext()->currentProjectChanged().onNotify(this, [this]() {
        onProjectChanged();
    });

    globalContext()->currentNotationChanged().onNotify(this, [this]() {
        onNotationChanged();
    });

    playbackController()->referenceAudioDecodeStateChanged().onNotify(this, [this]() {
        onDecodeStateChanged();
    });

    notationConfiguration()->isPlayRepeatsChanged().onNotify(this, [this]() {
        invalidateGrid();
    });

    notationConfiguration()->isMetronomeEnabledChanged().onNotify(this, [this]() {
        emit metronomeChanged();
    });

    listenPlaybackState();
    onProjectChanged();
    onNotationChanged();
    onDecodeStateChanged();
}

// ---- data sources ----

mu::project::IProjectAudioSyncSettingsPtr AudioSyncView::audioSyncSettings() const
{
    project::INotationProjectPtr project = globalContext()->currentProject();
    return project ? project->audioSyncSettings() : nullptr;
}

mu::notation::INotationAutomationPtr AudioSyncView::automation() const
{
    project::INotationProjectPtr project = globalContext()->currentProject();
    return project && project->masterNotation() ? project->masterNotation()->automation() : nullptr;
}

const Score* AudioSyncView::score() const
{
    project::INotationProjectPtr project = globalContext()->currentProject();
    if (!project || !project->masterNotation()) {
        return nullptr;
    }

    return project->masterNotation()->masterScore();
}

bool AudioSyncView::isPlayRepeatsEnabled() const
{
    return notationConfiguration()->isPlayRepeatsEnabled();
}

void AudioSyncView::onProjectChanged()
{
    if (project::IProjectAudioSyncSettingsPtr settings = audioSyncSettings()) {
        settings->settingsChanged().onNotify(this, [this]() {
            emit audioChanged();
            updateSpeedWarning();
            update();
        }, Asyncable::Mode::SetReplace);
    }

    m_scrollSecs = -1.0;
    invalidateGrid();
    emit audioChanged();
}

void AudioSyncView::onNotationChanged()
{
    invalidateGrid();

    project::INotationProjectPtr project = globalContext()->currentProject();
    notation::INotationPtr notation = globalContext()->currentNotation();
    if (!project || !notation) {
        return;
    }

    //! NOTE Any edit can move measures in time (tempo markings, measures added, time signatures...)
    notation->notationChanged().onReceive(this, [this](const muse::RectF&) {
        invalidateGrid();
    }, Asyncable::Mode::SetReplace);

    if (notation::IMasterNotationPtr masterNotation = project->masterNotation()) {
        if (notation::INotationAutomationPtr automation = masterNotation->automation()) {
            if (engraving::AutomationDataConstPtr data = automation->automationData()) {
                data->changed().onReceive(this, [this](const engraving::AutomationChanges&) {
                    invalidateGrid();
                    emit syncChanged();
                }, Asyncable::Mode::SetReplace);
            }
        }
    }

    emit syncChanged();

    if (notation::INotationPlaybackPtr notationPlayback = project->masterNotation() ? project->masterNotation()->playback() : nullptr) {
        notationPlayback->loopBoundariesChanged().onNotify(this, [this]() {
            update();
        }, Asyncable::Mode::SetReplace);
    }

    notation->interaction()->selectionChanged().onNotify(this, [this]() {
        const Score* sc = score();
        int from = -1;
        int to = -1;
        if (sc && sc->selection().isRange()) {
            from = sc->selection().tickStart().ticks();
            to = sc->selection().tickEnd().ticks();
        }

        if (from != m_selectionTickFrom || to != m_selectionTickTo) {
            m_selectionTickFrom = from;
            m_selectionTickTo = to;
            update();
        }
    }, Asyncable::Mode::SetReplace);
}

void AudioSyncView::onDecodeStateChanged()
{
    const IPlaybackController::ReferenceAudioDecodeState state = playbackController()->referenceAudioDecodeState();
    m_peaks->load(state.wavPath.toQString());

    emit decodeStateChanged();
    update();
}

void AudioSyncView::listenPlaybackState()
{
    context::IPlaybackStatePtr playbackState = globalContext()->playbackState();
    if (!playbackState) {
        return;
    }

    m_lastPositionSecs = playbackState->playbackPosition();
    m_lastPositionTime = std::chrono::steady_clock::now();
    m_isPlaying = playbackState->isPlaying();

    playbackState->playbackPositionChanged().onReceive(this, [this](muse::audio::secs_t secs) {
        m_lastPositionSecs = secs;
        m_lastPositionTime = std::chrono::steady_clock::now();
        if (!m_isPlaying) {
            updatePlaybackCursor();
        }
    });

    playbackState->playbackStatusChanged().onReceive(this, [this](muse::audio::PlaybackStatus) {
        context::IPlaybackStatePtr state = globalContext()->playbackState();
        m_isPlaying = state && state->isPlaying();
        emit playingChanged();
        if (m_isPlaying) {
            m_cursorTimer.start();
        } else {
            m_cursorTimer.stop();
        }
        updatePlaybackCursor();
    });

    if (m_isPlaying) {
        m_cursorTimer.start();
    }
}

// ---- audio ----

bool AudioSyncView::hasAudio() const
{
    project::IProjectAudioSyncSettingsPtr settings = audioSyncSettings();
    return settings && settings->referenceAudio().isValid();
}

QString AudioSyncView::audioFileName() const
{
    return hasAudio() ? QFileInfo(audioPath()).fileName() : QString();
}

QString AudioSyncView::audioPath() const
{
    project::IProjectAudioSyncSettingsPtr settings = audioSyncSettings();
    return settings ? settings->referenceAudio().path.toQString() : QString();
}

bool AudioSyncView::audioFileMissing() const
{
    return hasAudio() && !QFileInfo::exists(audioPath());
}

bool AudioSyncView::decoding() const
{
    return playbackController()->referenceAudioDecodeState().decoding;
}

double AudioSyncView::decodeProgress() const
{
    return playbackController()->referenceAudioDecodeState().progress;
}

bool AudioSyncView::decodeFailed() const
{
    return hasAudio() && (playbackController()->referenceAudioDecodeState().failed || m_peaks->failed());
}

bool AudioSyncView::waveformReady() const
{
    return m_peaks->isReady();
}

bool AudioSyncView::muted() const
{
    project::IProjectAudioSyncSettingsPtr settings = audioSyncSettings();
    return settings && settings->referenceAudio().muted;
}

void AudioSyncView::setMuted(bool muted)
{
    project::updateReferenceAudio(audioSyncSettings(), [muted](project::ReferenceAudioSettings& updated) {
        updated.muted = muted;
    });
}

bool AudioSyncView::solo() const
{
    project::IProjectAudioSyncSettingsPtr settings = audioSyncSettings();
    return settings && settings->referenceAudio().solo;
}

void AudioSyncView::setSolo(bool solo)
{
    project::updateReferenceAudio(audioSyncSettings(), [solo](project::ReferenceAudioSettings& updated) {
        updated.solo = solo;
    });
}

bool AudioSyncView::showStereo() const
{
    project::IProjectAudioSyncSettingsPtr settings = audioSyncSettings();
    return settings && settings->referenceAudio().showStereo;
}

void AudioSyncView::setShowStereo(bool showStereo)
{
    project::updateReferenceAudio(audioSyncSettings(), [showStereo](project::ReferenceAudioSettings& updated) {
        updated.showStereo = showStereo;
    });
}

bool AudioSyncView::isStereoAudio() const
{
    return m_peaks->isStereo();
}

bool AudioSyncView::audioSyncEnabled() const
{
    notation::INotationAutomationPtr automationPtr = automation();
    return automationPtr && automationPtr->isAudioSyncEnabled();
}

bool AudioSyncView::hasSyncPoints() const
{
    ensureGrid();
    return m_syncPoints.size() > 1;
}

void AudioSyncView::clearSyncPoints()
{
    if (notation::INotationAutomationPtr automationPtr = automation()) {
        if (audiosync::removeAllPoints(*automationPtr)) {
            m_selectedPointUtick = -1;
        }
    }
}

void AudioSyncView::setAudioSyncEnabled(bool enabled)
{
    if (notation::INotationAutomationPtr automationPtr = automation()) {
        automationPtr->setAudioSyncEnabled(enabled);
    }
}

bool AudioSyncView::isPlaying() const
{
    return m_isPlaying;
}

bool AudioSyncView::metronomeEnabled() const
{
    return notationConfiguration()->isMetronomeEnabled();
}

QString AudioSyncView::timeText() const
{
    return m_timeText;
}

void AudioSyncView::togglePlay()
{
    playbackController()->togglePlay();
}

void AudioSyncView::stopPlayback()
{
    playbackController()->stop();
}

void AudioSyncView::rewindToStart()
{
    playbackController()->rewind(0);
}

void AudioSyncView::goToLoopStart()
{
    project::IProjectAudioSyncSettingsPtr settings = audioSyncSettings();
    if (settings && settings->referenceAudio().loopFromSecs >= 0.0) {
        seekToAudioSecs(settings->referenceAudio().loopFromSecs);
    }
}

void AudioSyncView::toggleMetronome()
{
    playbackController()->toggleMetronome();
}

QStringList AudioSyncView::recentAudioFiles() const
{
    return playbackConfiguration()->recentReferenceAudioFiles();
}

void AudioSyncView::clearRecentAudioFiles()
{
    playbackConfiguration()->clearRecentReferenceAudioFiles();
}

void AudioSyncView::setAudioFile(const QString& path)
{
    audiosync::setReferenceAudioPath(playbackConfiguration().get(), audioSyncSettings(), path);
}

bool AudioSyncView::loopEnabled() const
{
    project::IProjectAudioSyncSettingsPtr settings = audioSyncSettings();
    return settings && settings->referenceAudio().loopEnabled && settings->referenceAudio().hasLoop();
}

void AudioSyncView::setLoopEnabled(bool enabled)
{
    project::updateReferenceAudio(audioSyncSettings(), [enabled](project::ReferenceAudioSettings& updated) {
        updated.loopEnabled = enabled && updated.hasLoop();
    });
}

bool AudioSyncView::hasLoopFrom() const
{
    project::IProjectAudioSyncSettingsPtr settings = audioSyncSettings();
    return settings && settings->referenceAudio().loopFromSecs >= 0.0;
}

bool AudioSyncView::hasLoopTo() const
{
    project::IProjectAudioSyncSettingsPtr settings = audioSyncSettings();
    return settings && settings->referenceAudio().loopToSecs >= 0.0;
}

void AudioSyncView::setLoop(double fromSecs, double toSecs)
{
    // A after B: swapped, so the loop always goes forward
    if (fromSecs >= 0.0 && toSecs >= 0.0 && toSecs < fromSecs) {
        std::swap(fromSecs, toSecs);
    }

    project::updateReferenceAudio(audioSyncSettings(), [fromSecs, toSecs](project::ReferenceAudioSettings& updated) {
        updated.loopFromSecs = fromSecs;
        updated.loopToSecs = toSecs;
        if (!updated.hasLoop()) {
            updated.loopEnabled = false;
        }
    });
}

void AudioSyncView::toggleLoopFrom()
{
    project::IProjectAudioSyncSettingsPtr settings = audioSyncSettings();
    if (!settings) {
        return;
    }

    const project::ReferenceAudioSettings& referenceAudio = settings->referenceAudio();
    const bool wasSet = referenceAudio.loopFromSecs >= 0.0;
    const double toSecs = referenceAudio.loopToSecs;
    setLoop(wasSet ? -1.0 : std::max(0.0, cursorAudioSecs()), toSecs);

    // Both ends set: the loop plays
    if (!wasSet && hasLoopFrom() && hasLoopTo()) {
        setLoopEnabled(true);
    }
}

void AudioSyncView::toggleLoopTo()
{
    project::IProjectAudioSyncSettingsPtr settings = audioSyncSettings();
    if (!settings) {
        return;
    }

    const project::ReferenceAudioSettings& referenceAudio = settings->referenceAudio();
    const bool wasSet = referenceAudio.loopToSecs >= 0.0;
    const double fromSecs = referenceAudio.loopFromSecs;
    setLoop(fromSecs, wasSet ? -1.0 : std::max(0.0, cursorAudioSecs()));

    if (!wasSet && hasLoopFrom() && hasLoopTo()) {
        setLoopEnabled(true);
    }
}

struct AudioSyncView::DetectionJob {
    std::atomic<bool> canceled { false };
    audiosync::DetectionOptions options;
    int firstNoteUtick = -1; // >= 0: put on the music's start (the silence at the beginning ignored)
};

bool AudioSyncView::detecting() const
{
    return m_detectionJob != nullptr;
}

double AudioSyncView::detectionProgress() const
{
    return m_detectionProgress;
}

QString AudioSyncView::detectionSummary() const
{
    return m_detectionSummary;
}

void AudioSyncView::detectPoints(const QVariantMap& params)
{
    ensureGrid();
    const QString wavPath = playbackController()->referenceAudioDecodeState().wavPath.toQString();
    if (m_detectionJob || wavPath.isEmpty() || !m_audioSyncEnabled || m_syncPoints.empty()) {
        return;
    }

    //! NOTE From the score's start (the first sync point): where it is in the audio, or where the music starts
    //! after the silence the audio may begin with (then the score's first note goes there)
    const audiosync::SyncPoint& first = m_syncPoints.front();
    const bool ignoreLeadingSilence = params.value("ignoreLeadingSilence", true).toBool();

    audiosync::BeatDetectionParams detectionParams;
    detectionParams.sensitivity = params.value("sensitivity", 0.5).toDouble();
    detectionParams.minBpm = params.value("minBpm", 50.0).toDouble();
    detectionParams.maxBpm = params.value("maxBpm", 200.0).toDouble();
    detectionParams.fromSecs = std::max(0.0, first.audioSecs - 1.0);

    auto job = std::make_shared<DetectionJob>();
    job->options.anchorUtick = first.utick;
    job->options.anchorSecs = first.audioSecs;
    job->options.octave = static_cast<audiosync::DetectionOptions::Octave>(std::clamp(params.value("octave", 0).toInt(), 0, 3));
    job->options.everyMeasure = params.value("everyMeasure", false).toBool();
    job->options.toleranceSecs = params.value("toleranceMs", 20.0).toDouble() / 1000.0;
    job->options.minBpm = detectionParams.minBpm;
    job->options.maxBpm = detectionParams.maxBpm;
    job->firstNoteUtick = ignoreLeadingSilence ? audiosync::firstNoteUtick(score()) : -1;
    m_detectionJob = job;
    m_detectionProgress = 0.0;
    m_detectionSummary.clear();
    emit detectionChanged();

    QPointer<AudioSyncView> self(this);
    std::thread([self, job, wavPath, detectionParams, ignoreLeadingSilence]() {
        std::vector<audiosync::DetectedBeatTime> beats;
        std::optional<double> musicStartSecs;
        if (std::shared_ptr<MappedWav> wav = MappedWav::open(wavPath)) {
            audiosync::BeatDetectionParams params = detectionParams;
            if (ignoreLeadingSilence) {
                musicStartSecs = audiosync::findMusicStart(*wav, job->canceled);
                if (musicStartSecs) {
                    params.fromSecs = std::max(0.0, *musicStartSecs - 1.0);
                }
            }

            const std::vector<audiosync::DetectedBeat> detected = audiosync::detectBeats(*wav, params, job->canceled,
                                                                                         [self, job](double progress) {
                QMetaObject::invokeMethod(qApp, [self, job, progress]() {
                    if (self && self->m_detectionJob == job) {
                        self->m_detectionProgress = progress;
                        emit self->detectionChanged();
                    }
                }, Qt::QueuedConnection);
            });
            for (const audiosync::DetectedBeat& beat : detected) {
                beats.push_back({ beat.secs, beat.confidence });
            }
        }

        QMetaObject::invokeMethod(qApp, [self, job, beats, musicStartSecs]() {
            if (self && self->m_detectionJob == job) {
                self->onDetectionFinished(job, beats, musicStartSecs);
            }
        }, Qt::QueuedConnection);
    }).detach();
}

void AudioSyncView::cancelDetection()
{
    if (!m_detectionJob) {
        return;
    }

    m_detectionJob->canceled = true;
    m_detectionJob = nullptr;
    emit detectionChanged();
}

void AudioSyncView::onDetectionFinished(const std::shared_ptr<DetectionJob>& job, const std::vector<audiosync::DetectedBeatTime>& beats,
                                        std::optional<double> musicStartSecs)
{
    m_detectionJob = nullptr;
    emit detectionChanged();

    notation::INotationAutomationPtr automationPtr = automation();
    const Score* sc = score();
    if (job->canceled || !automationPtr || !sc) {
        return;
    }

    // The score's first note on the music's start
    audiosync::DetectionOptions options = job->options;
    if (job->firstNoteUtick >= 0 && musicStartSecs) {
        options.anchorUtick = job->firstNoteUtick;
        options.anchorSecs = *musicStartSecs;
    }

    audiosync::MatchInfo info;
    const std::vector<audiosync::MatchedPoint> points = audiosync::matchMeasures(sc, beats, options, &info);
    if (points.size() < 2) {
        interactive()->warning(muse::trc("playback", "No beats could be detected"),
                               muse::trc("playback", "Try a higher sensitivity or another tempo range, or place the points by hand."));
        return;
    }

    std::optional<double> scoreStartSecs;
    if (job->firstNoteUtick >= 0 && musicStartSecs) {
        scoreStartSecs = audiosync::scoreStartSecsForFirstNote(sc, points, job->firstNoteUtick, info.anchorSecs);
    } else if (std::abs(info.anchorSecs - options.anchorSecs) > 1e-6) {
        // The score's start, placed by hand, moved onto the detected beat it's nearest to
        scoreStartSecs = info.anchorSecs;
    }

    //! NOTE Detect is asked for knowingly, and undone in one step: it replaces the points (the locked ones excepted)
    const audiosync::AppliedDetection applied = audiosync::applyDetectedPoints(*automationPtr, sc, points, scoreStartSecs);

    LOGI() << "audio sync detection: pulse " << info.detectedBpm << " BPM, applied " << info.appliedBpm << " BPM, music start "
           << (musicStartSecs ? *musicStartSecs : -1.0) << " s, " << applied.added << " points, " << applied.replacedManual
           << " manual points replaced";

    QStringList parts;
    if (scoreStartSecs) {
        //: Where the score was found to start in the audio (min:sec)
        parts << muse::qtrc("playback", "Score start: %1").arg(formatTime(*scoreStartSecs, true));
    }
    if (info.octave == audiosync::DetectionOptions::Octave::Half) {
        //: %1: tempo detected in the audio, %2: half of it, the tempo used
        parts << muse::qtrc("playback", "Pulse found: %1 BPM, taken at half: %2 BPM")
            .arg(info.detectedBpm, 0, 'f', 1).arg(info.appliedBpm, 0, 'f', 1);
    } else if (info.octave == audiosync::DetectionOptions::Octave::Double) {
        //: %1: tempo detected in the audio, %2: twice it, the tempo used
        parts << muse::qtrc("playback", "Pulse found: %1 BPM, taken at double: %2 BPM")
            .arg(info.detectedBpm, 0, 'f', 1).arg(info.appliedBpm, 0, 'f', 1);
    } else {
        parts << muse::qtrc("playback", "Pulse found: %1 BPM").arg(info.detectedBpm, 0, 'f', 1);
    }
    parts << muse::qtrc("playback", "%n point(s)", nullptr, applied.added);
    if (applied.replacedManual > 0) {
        parts << muse::qtrc("playback", "%n manual point(s) replaced", nullptr, applied.replacedManual);
    }

    m_detectionSummary = parts.join("  ·  ");
    emit detectionChanged();
}

bool AudioSyncView::speedWarning() const
{
    return m_speedWarning;
}

void AudioSyncView::updateSpeedWarning()
{
    const bool warning = hasAudio() && std::abs(playbackController()->tempoMultiplier() - 1.0) > 1e-6;
    if (warning != m_speedWarning) {
        m_speedWarning = warning;
        emit speedWarningChanged();
    }
}

void AudioSyncView::chooseAudioFile()
{
    audiosync::chooseReferenceAudio(interactive().get(), playbackConfiguration().get(), audioSyncSettings());
}

bool AudioSyncView::canDropUrls(const QList<QUrl>& urls) const
{
    return urls.size() == 1 && urls.first().isLocalFile() && audiosync::isSupportedAudioFile(urls.first().toLocalFile())
           && audioSyncSettings() != nullptr;
}

void AudioSyncView::dropUrls(const QList<QUrl>& urls)
{
    if (!canDropUrls(urls)) {
        return;
    }

    audiosync::setReferenceAudioPath(playbackConfiguration().get(), audioSyncSettings(), urls.first().toLocalFile());
}

void AudioSyncView::removeAudio()
{
    if (project::IProjectAudioSyncSettingsPtr settings = audioSyncSettings()) {
        settings->clearReferenceAudio();
    }
}

// ---- view ----

double AudioSyncView::pixelsPerSecond() const
{
    return m_pixelsPerSecond;
}

void AudioSyncView::setPixelsPerSecond(double pps)
{
    zoomAround(pps, width() / 2.0);
}

double AudioSyncView::scrollSecs() const
{
    return std::max(m_scrollSecs, contentStartSecs());
}

void AudioSyncView::setScrollSecs(double secs)
{
    if (qFuzzyCompare(secs, m_scrollSecs)) {
        return;
    }

    m_scrollSecs = secs;
    clampScroll();
    emit viewChanged();
    update();
}

double AudioSyncView::contentStartSecs() const
{
    ensureGrid();
    return std::min(0.0, audioOffsetSecs());
}

double AudioSyncView::contentEndSecs() const
{
    ensureGrid();
    return std::max({ m_peaks->durationSecs(), m_scoreEndSecs + audioOffsetSecs(), contentStartSecs() + 1.0 });
}

double AudioSyncView::visibleSecs() const
{
    return width() / m_pixelsPerSecond;
}

double AudioSyncView::amplitudeZoom() const
{
    return m_amplitudeZoom;
}

void AudioSyncView::setAmplitudeZoom(double zoom)
{
    zoom = std::clamp(zoom, MIN_AMPLITUDE_ZOOM, MAX_AMPLITUDE_ZOOM);
    if (qFuzzyCompare(zoom, m_amplitudeZoom)) {
        return;
    }

    m_amplitudeZoom = zoom;
    emit viewChanged();
    update();
}

double AudioSyncView::waveformTop() const
{
    return waveformRect().top();
}

double AudioSyncView::waveformHeight() const
{
    return waveformRect().height();
}

bool AudioSyncView::followPlayback() const
{
    return m_followPlayback;
}

void AudioSyncView::setFollowPlayback(bool follow)
{
    if (m_followPlayback == follow) {
        return;
    }

    m_followPlayback = follow;
    emit viewChanged();
}

void AudioSyncView::zoomIn()
{
    const double cursorX = xForAudioSecs(cursorAudioSecs());
    zoomAround(m_pixelsPerSecond * ZOOM_STEP, cursorX >= 0 && cursorX <= width() ? cursorX : width() / 2.0);
}

void AudioSyncView::zoomOut()
{
    const double cursorX = xForAudioSecs(cursorAudioSecs());
    zoomAround(m_pixelsPerSecond / ZOOM_STEP, cursorX >= 0 && cursorX <= width() ? cursorX : width() / 2.0);
}

void AudioSyncView::restoreViewState()
{
    project::IProjectAudioSyncSettingsPtr settings = audioSyncSettings();
    if (!settings || !settings->viewState().isValid() || width() <= 0) {
        zoomToFit();
        return;
    }

    const project::AudioSyncViewState& state = settings->viewState();
    m_pixelsPerSecond = std::clamp(state.pixelsPerSecond, MIN_PIXELS_PER_SECOND, MAX_PIXELS_PER_SECOND);
    m_amplitudeZoom = std::clamp(state.amplitudeZoom, MIN_AMPLITUDE_ZOOM, MAX_AMPLITUDE_ZOOM);
    m_scrollSecs = state.scrollSecs;
    clampScroll();
    emit viewChanged();
    update();
}

void AudioSyncView::storeViewState()
{
    //! NOTE Not before it was shown (restored or fitted): the saved one would be lost
    project::IProjectAudioSyncSettingsPtr settings = audioSyncSettings();
    if (!settings || m_scrollSecs < 0 || !m_peaks->isReady()) {
        return;
    }

    project::AudioSyncViewState state;
    state.pixelsPerSecond = m_pixelsPerSecond;
    state.scrollSecs = m_scrollSecs;
    state.amplitudeZoom = m_amplitudeZoom;
    settings->setViewState(state);
}

void AudioSyncView::zoomToFit()
{
    if (width() <= 0) {
        return;
    }

    const double duration = contentEndSecs() - contentStartSecs();
    m_pixelsPerSecond = std::clamp(width() / std::max(duration, 0.001), MIN_PIXELS_PER_SECOND, MAX_PIXELS_PER_SECOND);
    m_scrollSecs = contentStartSecs();
    clampScroll();
    emit viewChanged();
    update();
}

void AudioSyncView::zoomAround(double pps, double anchorX)
{
    pps = std::clamp(pps, MIN_PIXELS_PER_SECOND, MAX_PIXELS_PER_SECOND);
    if (qFuzzyCompare(pps, m_pixelsPerSecond)) {
        return;
    }

    const double anchorSecs = audioSecsAtX(anchorX);
    m_pixelsPerSecond = pps;
    m_scrollSecs = anchorSecs - anchorX / m_pixelsPerSecond;
    clampScroll();
    emit viewChanged();
    update();
}

void AudioSyncView::clampScroll()
{
    const double start = contentStartSecs();
    const double maxScroll = std::max(start, contentEndSecs() - visibleSecs());
    m_scrollSecs = std::clamp(m_scrollSecs, start, maxScroll);
}

void AudioSyncView::geometryChange(const QRectF& newGeometry, const QRectF& oldGeometry)
{
    QQuickPaintedItem::geometryChange(newGeometry, oldGeometry);

    if (newGeometry.width() > 0 && m_scrollSecs < 0 && m_peaks->isReady()) {
        restoreViewState();
        return;
    }

    if (newGeometry.size() != oldGeometry.size()) {
        clampScroll();
        emit viewChanged();
    }
}

// ---- time mapping ----

double AudioSyncView::audioOffsetSecs() const
{
    ensureGrid();
    return m_audioOffsetSecs;
}

double AudioSyncView::audioSecsOf(const GridTime& time) const
{
    if (m_pointDrag) {
        return previewAudioSecs(time.utick);
    }

    return time.secs + audioOffsetSecs();
}

double AudioSyncView::previewAudioSecs(int utick) const
{
    //! NOTE While audio sync is on, the tempo is constant between two sync points: audio time is linear in
    //! ticks there, so the grid can follow a dragged point without any engraving edit
    std::vector<std::pair<int, double> > points;
    points.reserve(m_syncPoints.size());
    for (size_t i = 0; i < m_syncPoints.size(); ++i) {
        const double secs = m_pointDrag && m_pointDrag->index == i ? m_pointDrag->audioSecs : m_syncPoints[i].audioSecs;
        points.emplace_back(m_syncPoints[i].utick, secs);
    }

    if (points.size() < 2) {
        const double shift = points.empty() ? 0.0 : points.front().second - m_syncPoints.front().audioSecs;
        return audioOffsetSecs() + shift + (score() ? score()->tempoTimeline(true).utick2utime(utick) : 0.0);
    }

    // The segment containing utick (the first/last one beyond the points)
    size_t i = 0;
    while (i + 2 < points.size() && utick >= points[i + 1].first) {
        ++i;
    }

    const auto& [u1, s1] = points[i];
    const auto& [u2, s2] = points[i + 1];
    if (u2 == u1) {
        return s1;
    }
    return s1 + (s2 - s1) * static_cast<double>(utick - u1) / static_cast<double>(u2 - u1);
}

double AudioSyncView::audioSecsAtX(double x) const
{
    return scrollSecs() + x / m_pixelsPerSecond;
}

double AudioSyncView::xForAudioSecs(double secs) const
{
    return (secs - scrollSecs()) * m_pixelsPerSecond;
}

double AudioSyncView::cursorAudioSecs() const
{
    double secs = m_lastPositionSecs;
    if (m_isPlaying) {
        secs += std::chrono::duration<double>(std::chrono::steady_clock::now() - m_lastPositionTime).count();
    }

    return secs + audioOffsetSecs();
}

void AudioSyncView::seekToAudioSecs(double secs)
{
    ensureGrid();
    const double engineSecs = std::clamp(secs - audioOffsetSecs(), 0.0, m_scoreEndSecs);
    playbackController()->rewind(engineSecs);
}

void AudioSyncView::updatePlaybackCursor()
{
    if (m_isPlaying && m_followPlayback && width() > 0) {
        const double x = xForAudioSecs(cursorAudioSecs());
        if (x < 0 || x > width() * 0.9) {
            m_scrollSecs = cursorAudioSecs() - visibleSecs() * 0.1;
            clampScroll();
            emit viewChanged();
        }
    }

    updatePositionText();
    updateSpeedWarning();
    update();
}

void AudioSyncView::updatePositionText()
{
    ensureGrid();

    const double secs = cursorAudioSecs();
    QString positionText;
    QString tempoText;

    const Score* sc = score();
    if (sc) {
        const double engineSecs = secs - audioOffsetSecs();
        auto it = std::upper_bound(m_grid.begin(), m_grid.end(), engineSecs, [](double s, const GridMeasure& m) {
            return s < m.start.secs;
        });
        if (it != m_grid.begin()) {
            const GridMeasure& measure = *std::prev(it);
            const int beat = 1
                             + static_cast<int>(std::count_if(measure.beats.begin(), measure.beats.end(), [engineSecs](const GridTime& t) {
                return t.secs <= engineSecs;
            }));
            //: Position in the score: measure number and beat, e.g. "Measure 12, beat 3"
            positionText = muse::qtrc("playback", "Measure %1, beat %2").arg(measure.number).arg(beat);
        }

        const TempoTimeline& timeline = sc->tempoTimeline(isPlayRepeatsEnabled());
        const int utick = timeline.utime2utick(std::max(0.0, engineSecs));
        //: Tempo in beats (quarter notes) per minute
        tempoText = muse::qtrc("playback", "%1 BPM").arg(timeline.tempo(utick).toBPM().val, 0, 'f', 1);
    }

    const QString timeText = formatClock(secs) + " / " + formatClock(m_peaks->durationSecs());

    if (positionText != m_positionText || tempoText != m_tempoText || timeText != m_timeText) {
        m_positionText = positionText;
        m_tempoText = tempoText;
        m_timeText = timeText;
        emit positionTextChanged();
    }
}

QString AudioSyncView::positionText() const
{
    return m_positionText;
}

QString AudioSyncView::tempoText() const
{
    return m_tempoText;
}

// ---- grid ----

void AudioSyncView::invalidateGrid()
{
    m_gridDirty = true;
    update();
}

void AudioSyncView::ensureGrid() const
{
    if (m_gridDirty) {
        rebuildGrid();
    }
}

void AudioSyncView::rebuildGrid() const
{
    m_gridDirty = false;
    m_grid.clear();
    m_scoreEndSecs = 0.0;
    m_scoreEnd = GridTime();
    m_syncPoints.clear();
    m_audioSyncEnabled = false;
    m_audioOffsetSecs = 0.0;

    if (notation::INotationAutomationPtr automationPtr = automation()) {
        m_syncPoints = audiosync::syncPoints(*automationPtr);
        m_audioSyncEnabled = automationPtr->isAudioSyncEnabled();
        m_audioOffsetSecs = automationPtr->audioSyncOffsetSecs();
    }

    const Score* sc = score();
    if (!sc || !sc->firstMeasure()) {
        return;
    }

    const bool expandRepeats = isPlayRepeatsEnabled();
    const TempoTimeline& timeline = sc->tempoTimeline(expandRepeats);

    TimeSigFrac prevTimeSig;

    auto addMeasure = [&](const Measure* measure, int tickOffset) {
        const int tickFrom = measure->tick().ticks();
        const int tickTo = measure->endTick().ticks();

        GridMeasure gridMeasure;
        gridMeasure.tickFrom = tickFrom;
        gridMeasure.tickTo = tickTo;
        gridMeasure.start = { tickFrom + tickOffset, timeline.utick2utime(tickFrom + tickOffset) };
        gridMeasure.end = { tickTo + tickOffset, timeline.utick2utime(tickTo + tickOffset) };
        gridMeasure.number = measure->excludeFromNumbering() ? QStringLiteral("( )") : QString::number(measure->measureNumber() + 1);

        const TimeSigFrac timeSig = sc->sigmap()->timesig(tickFrom).nominal();
        if (m_grid.empty() || timeSig.identical(prevTimeSig) == false) {
            gridMeasure.timeSig = QString("%1/%2").arg(timeSig.numerator()).arg(timeSig.denominator());
        }
        prevTimeSig = timeSig;

        // Same beats as the metronome (see PlaybackEventsRenderer::renderMetronome())
        const double bps = timeline.tempo(tickFrom + tickOffset).val;
        const int step = std::max(1, timeSig.isBeatedCompound(bps) ? timeSig.beatTicks() : timeSig.dUnitTicks());
        int beatTick = tickFrom;
        if (measure->isAnacrusis()) {
            beatTick += measure->ticks().ticks() % step;
        }
        for (; beatTick < tickTo; beatTick += step) {
            if (beatTick > tickFrom) {
                gridMeasure.beats.push_back({ beatTick + tickOffset, timeline.utick2utime(beatTick + tickOffset) });
            }
        }

        if (gridMeasure.end.secs >= m_scoreEndSecs) {
            m_scoreEndSecs = gridMeasure.end.secs;
            m_scoreEnd = gridMeasure.end;
        }
        m_grid.push_back(std::move(gridMeasure));
    };

    if (expandRepeats) {
        for (const RepeatSegment* seg : sc->expandedRepeatList()) {
            const int tickOffset = seg->utick - seg->tick;
            for (const Measure* measure : seg->measureList()) {
                addMeasure(measure, tickOffset);
            }
        }
    } else {
        for (const Measure* measure = sc->firstMeasure(); measure; measure = measure->nextMeasure()) {
            addMeasure(measure, 0);
        }
    }

    std::stable_sort(m_grid.begin(), m_grid.end(), [](const GridMeasure& a, const GridMeasure& b) {
        return a.start.secs < b.start.secs;
    });
}

// ---- mouse ----

void AudioSyncView::mousePressEvent(QMouseEvent* event)
{
    if (event->button() != Qt::LeftButton) {
        event->ignore();
        return;
    }

    forceActiveFocus();

    const QPointF pos = event->position();
    if (overviewRect().contains(pos)) {
        const double secs = overviewSecsAtX(pos.x());
        const double windowStart = scrollSecs();
        if (secs < windowStart || secs > windowStart + visibleSecs()) {
            // Outside the visible part: centered there
            setScrollSecs(secs - visibleSecs() / 2.0);
        }
        m_overviewGrabOffsetSecs = secs - scrollSecs();
        m_dragMode = DragMode::Overview;
        event->accept();
        return;
    }

    if (loopBandRect().contains(pos)) {
        const auto [fromSecs, toSecs] = displayedLoop();
        const double secs = audioSecsAtX(pos.x());
        const auto nearHandle = [&](double handleSecs) {
            return handleSecs >= 0.0 && std::abs(xForAudioSecs(handleSecs) - pos.x()) <= LOOP_HANDLE_SIZE;
        };

        std::optional<DragMode> mode;
        if (nearHandle(toSecs)) {
            mode = DragMode::LoopTo;
        } else if (nearHandle(fromSecs)) {
            mode = DragMode::LoopFrom;
        } else if (fromSecs >= 0.0 && toSecs > fromSecs && secs > fromSecs && secs < toSecs) {
            mode = DragMode::LoopBoth;
        }

        if (mode) {
            m_loopDrag = LoopDrag { fromSecs, toSecs, secs, fromSecs, toSecs };
            m_dragMode = *mode;
            event->accept();
            return;
        }
    }

    if (const std::optional<size_t> index = pointAt(pos)) {
        const audiosync::SyncPoint& point = m_syncPoints[*index];
        m_selectedPointUtick = point.utick;
        if (m_audioSyncEnabled && !point.locked) {
            PointDrag drag;
            drag.index = *index;
            drag.originalSecs = point.audioSecs;
            drag.audioSecs = point.audioSecs;
            drag.grabOffsetSecs = audioSecsAtX(pos.x()) - point.audioSecs;
            m_pointDrag = drag;
            m_dragMode = DragMode::Point;
        }
        update();
        event->accept();
        return;
    }

    if (railRect().contains(pos)) {
        m_selectedPointUtick = -1;
    }

    m_dragMode = DragMode::Seek;
    m_seekPressX = pos.x();
    seekToAudioSecs(audioSecsAtX(pos.x()));
    event->accept();
}

void AudioSyncView::mouseMoveEvent(QMouseEvent* event)
{
    //! NOTE A release can be lost (e.g. outside the window): the buttons tell
    if (m_dragMode == DragMode::None || !(event->buttons() & Qt::LeftButton)) {
        if (m_dragMode == DragMode::Point) {
            m_pointDrag.reset();
            update();
        }
        m_loopDrag.reset();
        m_dragMode = DragMode::None;
        return;
    }

    if (m_loopDrag && (m_dragMode == DragMode::LoopFrom || m_dragMode == DragMode::LoopTo || m_dragMode == DragMode::LoopBoth)) {
        const double secs = std::max(0.0, audioSecsAtX(event->position().x()));
        if (m_dragMode == DragMode::LoopFrom) {
            m_loopDrag->fromSecs = secs;
        } else if (m_dragMode == DragMode::LoopTo) {
            m_loopDrag->toSecs = secs;
        } else {
            const double delta = std::max(secs - m_loopDrag->grabSecs, -m_loopDrag->originalFromSecs);
            m_loopDrag->fromSecs = m_loopDrag->originalFromSecs + delta;
            m_loopDrag->toSecs = m_loopDrag->originalToSecs + delta;
        }
        update();
        return;
    }

    if (m_dragMode == DragMode::Point && m_pointDrag) {
        // Kept where both neighboring segments stay within the allowed tempos
        const audiosync::SyncPoint& point = m_syncPoints[m_pointDrag->index];
        const double wantedSecs = audioSecsAtX(event->position().x()) - m_pointDrag->grabOffsetSecs;
        double secs = wantedSecs;
        if (const auto range = audiosync::allowedAudioRange(m_syncPoints, point.utick)) {
            secs = std::clamp(secs, range->first, range->second);
        }
        m_pointDrag->audioSecs = secs;
        m_pointDrag->atLimit = std::abs(wantedSecs - secs) > 1e-6;
        m_pointDrag->moved = m_pointDrag->moved || std::abs(secs - m_pointDrag->originalSecs) > 1e-6;
        update();
        return;
    }

    if (m_dragMode == DragMode::Overview) {
        setScrollSecs(overviewSecsAtX(event->position().x()) - m_overviewGrabOffsetSecs);
        return;
    }

    //! NOTE Dragging the playback position marks the A-B loop over the dragged range (set on release)
    const double x = event->position().x();
    if (m_loopDrag || std::abs(x - m_seekPressX) >= SEEK_DRAG_LOOP_DISTANCE) {
        const double pressSecs = std::max(0.0, audioSecsAtX(m_seekPressX));
        const double secs = std::max(0.0, audioSecsAtX(x));
        if (!m_loopDrag) {
            m_loopDrag = LoopDrag { pressSecs, secs, pressSecs, -1.0, -1.0, true };
        }
        m_loopDrag->fromSecs = std::min(pressSecs, secs);
        m_loopDrag->toSecs = std::max(pressSecs, secs);
        update();
    }

    seekToAudioSecs(audioSecsAtX(x));
}

void AudioSyncView::mouseReleaseEvent(QMouseEvent*)
{
    if (m_loopDrag) {
        const LoopDrag drag = *m_loopDrag;
        m_loopDrag.reset();
        m_dragMode = DragMode::None;
        setLoop(drag.fromSecs, drag.toSecs);
        if (drag.enableOnRelease) {
            setLoopEnabled(true);
        }
        update();
        return;
    }

    if (m_dragMode == DragMode::Point && m_pointDrag) {
        const PointDrag drag = *m_pointDrag;
        const int utick = m_syncPoints[drag.index].utick;
        m_pointDrag.reset();
        m_dragMode = DragMode::None;

        notation::INotationAutomationPtr automationPtr = automation();
        if (drag.moved && automationPtr) {
            audiosync::movePoint(*automationPtr, utick, drag.audioSecs);
        }
        update();
        return;
    }

    m_dragMode = DragMode::None;
}

void AudioSyncView::mouseDoubleClickEvent(QMouseEvent* event)
{
    const QPointF pos = event->position();
    if (event->button() != Qt::LeftButton || overviewRect().contains(pos)) {
        event->ignore();
        return;
    }

    //! NOTE On a point: deletes it (not the first one, the score's start)
    if (const std::optional<size_t> index = pointAt(pos)) {
        m_pointDrag.reset();
        m_dragMode = DragMode::None;
        if (notation::INotationAutomationPtr automationPtr = automation()) {
            if (audiosync::removePoint(*automationPtr, m_syncPoints[*index].utick)) {
                m_selectedPointUtick = -1;
            }
        }
        event->accept();
        return;
    }

    //! NOTE On the nearest beat, unless Shift is held (anywhere)
    addPointAt(audioSecsAtX(pos.x()), !(event->modifiers() & Qt::ShiftModifier));
    event->accept();
}

bool AudioSyncView::event(QEvent* event)
{
    //! NOTE The keys editing the selected sync point win over the app's shortcuts while this view has the focus
    if (event->type() == QEvent::ShortcutOverride && selectedPointIndex()) {
        const QKeyEvent* keyEvent = static_cast<QKeyEvent*>(event);
        switch (keyEvent->key()) {
        case Qt::Key_Delete:
        case Qt::Key_Backspace:
        case Qt::Key_Left:
        case Qt::Key_Right:
        case Qt::Key_Escape:
            event->accept();
            return true;
        default:
            break;
        }
    }

    return QQuickPaintedItem::event(event);
}

void AudioSyncView::keyPressEvent(QKeyEvent* event)
{
    switch (event->key()) {
    case Qt::Key_Delete:
    case Qt::Key_Backspace:
        deleteSelectedPoint();
        break;
    case Qt::Key_Left:
        nudgeSelectedPoint(event->modifiers() & Qt::ShiftModifier ? -LARGE_NUDGE_SECS : -NUDGE_SECS);
        break;
    case Qt::Key_Right:
        nudgeSelectedPoint(event->modifiers() & Qt::ShiftModifier ? LARGE_NUDGE_SECS : NUDGE_SECS);
        break;
    case Qt::Key_Escape:
        if (m_pointDrag) {
            // Back where it was
            m_pointDrag.reset();
            m_dragMode = DragMode::None;
        } else {
            m_selectedPointUtick = -1;
        }
        update();
        break;
    default:
        QQuickPaintedItem::keyPressEvent(event);
        return;
    }

    event->accept();
}

// ---- audio sync points ----

std::pair<double, double> AudioSyncView::displayedLoop() const
{
    if (m_loopDrag) {
        return { m_loopDrag->fromSecs, m_loopDrag->toSecs };
    }

    project::IProjectAudioSyncSettingsPtr settings = audioSyncSettings();
    if (!settings) {
        return { -1.0, -1.0 };
    }
    return { settings->referenceAudio().loopFromSecs, settings->referenceAudio().loopToSecs };
}

QRectF AudioSyncView::loopBandRect() const
{
    const QRectF waveRect = waveformRect();
    return QRectF(waveRect.left(), waveRect.top(), waveRect.width(), LOOP_BAND_HEIGHT);
}

void AudioSyncView::paintLoop(QPainter* painter, const QRectF& waveRect) const
{
    const auto [fromSecs, toSecs] = displayedLoop();
    if (fromSecs < 0.0 && toSecs < 0.0) {
        return;
    }

    const QRectF band = loopBandRect();
    const bool enabled = loopEnabled() || (m_loopDrag && fromSecs >= 0.0 && toSecs > fromSecs);
    static const QColor HANDLE_COLOR(0xFF, 0x7A, 0x4D);

    if (fromSecs >= 0.0 && toSecs > fromSecs) {
        const double x1 = xForAudioSecs(fromSecs);
        const double x2 = xForAudioSecs(toSecs);
        if (x2 >= 0 && x1 <= waveRect.width()) {
            QColor regionColor = m_accentColor;
            regionColor.setAlphaF(enabled ? 0.14 : 0.06);
            painter->fillRect(QRectF(x1, waveRect.top(), x2 - x1, waveRect.height()), regionColor);

            QColor barColor = m_accentColor;
            barColor.setAlphaF(enabled ? 1.0 : 0.4);
            painter->fillRect(QRectF(x1, band.top() + 1, x2 - x1, band.height() - 2), barColor);
        }
    }

    // The handles (and their lines)
    for (double secs : { fromSecs, toSecs }) {
        if (secs < 0.0) {
            continue;
        }

        const double x = xForAudioSecs(secs);
        if (x < -LOOP_HANDLE_SIZE || x > waveRect.width() + LOOP_HANDLE_SIZE) {
            continue;
        }

        QColor lineColor = HANDLE_COLOR;
        lineColor.setAlphaF(0.6);
        painter->setPen(QPen(lineColor, 1));
        painter->drawLine(QPointF(x, band.bottom()), QPointF(x, waveRect.bottom()));
        painter->fillRect(QRectF(x - LOOP_HANDLE_SIZE / 2, band.top(), LOOP_HANDLE_SIZE, band.height()), HANDLE_COLOR);
    }
}

bool AudioSyncView::isUncertainPoint(const audiosync::SyncPoint& point)
{
    return point.origin == engraving::AudioSyncInfo::Origin::Detected && point.confidence < 0.5f;
}

std::vector<double> AudioSyncView::displayedPointSecs() const
{
    std::vector<double> result;
    result.reserve(m_syncPoints.size());
    for (size_t i = 0; i < m_syncPoints.size(); ++i) {
        result.push_back(m_pointDrag && m_pointDrag->index == i ? m_pointDrag->audioSecs : m_syncPoints[i].audioSecs);
    }
    return result;
}

std::optional<size_t> AudioSyncView::pointAt(const QPointF& pos) const
{
    ensureGrid();

    const bool inRail = railRect().contains(pos);
    const bool inWaveform = waveformRect().contains(pos);
    if (!inRail && !inWaveform) {
        return std::nullopt;
    }

    const std::vector<double> secs = displayedPointSecs();
    std::optional<size_t> nearest;
    double nearestDistance = inRail ? POINT_HIT_DISTANCE : POINT_HIT_DISTANCE / 2.0;
    for (size_t i = 0; i < secs.size(); ++i) {
        const double distance = std::abs(xForAudioSecs(secs[i]) - pos.x());
        if (distance <= nearestDistance) {
            nearestDistance = distance;
            nearest = i;
        }
    }

    return nearest;
}

std::optional<size_t> AudioSyncView::selectedPointIndex() const
{
    ensureGrid();
    for (size_t i = 0; i < m_syncPoints.size(); ++i) {
        if (m_syncPoints[i].utick == m_selectedPointUtick) {
            return i;
        }
    }
    return std::nullopt;
}

void AudioSyncView::addPointAt(double audioSecs, bool snapToBeat)
{
    notation::INotationAutomationPtr automationPtr = automation();
    const Score* sc = score();
    ensureGrid();
    if (!automationPtr || !sc || !m_audioSyncEnabled) {
        return;
    }

    // The score position there by the current tempo; the new point stays where that position already is
    const TempoTimeline& timeline = sc->tempoTimeline(true);
    int utick = timeline.utime2utick(std::max(0.0, audioSecs - audioOffsetSecs()));
    if (snapToBeat) {
        if (const std::optional<int> beat = audiosync::nearestBeatUtick(sc, utick)) {
            utick = *beat;
        }
    }

    if (audiosync::addPoint(*automationPtr, sc, utick, automationPtr->audioSecsAtUtick(utick))) {
        m_selectedPointUtick = utick;
    }
}

void AudioSyncView::nudgeSelectedPoint(double deltaSecs)
{
    const std::optional<size_t> index = selectedPointIndex();
    notation::INotationAutomationPtr automationPtr = automation();
    if (!index || !automationPtr) {
        return;
    }

    audiosync::movePoint(*automationPtr, m_syncPoints[*index].utick, m_syncPoints[*index].audioSecs + deltaSecs);
}

void AudioSyncView::deleteSelectedPoint()
{
    const std::optional<size_t> index = selectedPointIndex();
    notation::INotationAutomationPtr automationPtr = automation();
    if (!index || !automationPtr) {
        return;
    }

    if (audiosync::removePoint(*automationPtr, m_syncPoints[*index].utick)) {
        m_selectedPointUtick = -1;
    }
}

void AudioSyncView::paintSyncPoints(QPainter* painter, const QRectF& railRect, const QRectF& waveRect) const
{
    if (m_syncPoints.empty()) {
        return;
    }

    QFont font = painter->font();
    font.setPixelSize(10);
    painter->setFont(font);
    const QFontMetricsF metrics(font);

    const std::vector<double> secs = displayedPointSecs();
    std::vector<audiosync::SyncPoint> displayed = m_syncPoints;
    for (size_t i = 0; i < displayed.size(); ++i) {
        displayed[i].audioSecs = secs[i];
    }

    // Off: shown (they're kept), but they don't drive the tempo
    const double opacity = m_audioSyncEnabled ? 1.0 : 0.35;

    // The tempo of each segment, where it fits
    for (size_t i = 0; i + 1 < displayed.size(); ++i) {
        const std::optional<double> bpm = audiosync::segmentBpm(displayed, i);
        if (!bpm) {
            continue;
        }

        const double x1 = xForAudioSecs(displayed[i].audioSecs);
        const double x2 = xForAudioSecs(displayed[i + 1].audioSecs);
        if (x2 < 0 || x1 > railRect.width()) {
            continue;
        }

        const QString text = QString::number(*bpm, 'f', 1);
        if (metrics.horizontalAdvance(text) + 16 <= x2 - x1) {
            QColor textColor = m_textColor;
            textColor.setAlphaF(0.75 * opacity);
            painter->setPen(textColor);
            painter->drawText(QRectF(x1, railRect.top(), x2 - x1, railRect.height()), Qt::AlignCenter, text);
        }
    }

    const std::optional<size_t> selected = selectedPointIndex();

    for (size_t i = 0; i < displayed.size(); ++i) {
        const audiosync::SyncPoint& point = displayed[i];
        const double x = xForAudioSecs(point.audioSecs);
        if (x < -POINT_HIT_DISTANCE || x > railRect.width() + POINT_HIT_DISTANCE) {
            continue;
        }

        //! NOTE All the points look the same, whatever placed them; only an uncertain detected one has a hollow handle
        QColor color = m_accentColor;
        color.setAlphaF(opacity);
        const bool uncertain = isUncertainPoint(point);

        const bool isSelected = selected && *selected == i;
        if (m_pointDrag && m_pointDrag->index == i && m_pointDrag->atLimit) {
            color = LIMIT_COLOR;
        }

        // Through the waveform
        QColor lineColor = color;
        lineColor.setAlphaF(lineColor.alphaF() * (isSelected ? 1.0 : 0.7));
        painter->setPen(QPen(lineColor, isSelected ? 2.0 : 1.2));
        painter->drawLine(QPointF(x, railRect.top()), QPointF(x, waveRect.bottom()));

        // The handle, in the rail: a downward triangle (a square when locked)
        const double size = isSelected ? 7.0 : 6.0;
        QPainterPath handle;
        if (point.locked) {
            handle.addRect(QRectF(x - size * 0.7, railRect.bottom() - size * 1.4, size * 1.4, size * 1.4));
        } else {
            handle.moveTo(x - size, railRect.bottom() - size * 1.6);
            handle.lineTo(x + size, railRect.bottom() - size * 1.6);
            handle.lineTo(x, railRect.bottom());
            handle.closeSubpath();
        }

        if (uncertain) {
            painter->setPen(QPen(isSelected ? m_textColor : color, 1.5));
            painter->setBrush(m_rulerColor);
        } else {
            painter->setPen(isSelected ? QPen(m_textColor, 1.2) : Qt::NoPen);
            painter->setBrush(color);
        }
        painter->drawPath(handle);
        painter->setBrush(Qt::NoBrush);
    }

    // While dragging: the tempos of both segments and the position
    if (m_pointDrag) {
        const size_t i = m_pointDrag->index;
        const std::optional<double> before = i > 0 ? audiosync::segmentBpm(displayed, i - 1) : std::nullopt;
        const std::optional<double> after = audiosync::segmentBpm(displayed, i);

        QStringList parts;
        if (before) {
            parts << QString::fromUtf8("← %1").arg(*before, 0, 'f', 1);
        }
        if (after) {
            parts << QString::fromUtf8("%1 →").arg(*after, 0, 'f', 1);
        }
        //: Tempo in beats (quarter notes) per minute
        QString text = parts.join("   ") + "  " + muse::qtrc("playback", "BPM") + "   " + formatTime(m_pointDrag->audioSecs, true);

        // Stopped by a tempo limit: which one
        if (m_pointDrag->atLimit) {
            const bool atMax = (before && *before >= audiosync::MAX_SYNC_BPM - 0.5) || (after && *after >= audiosync::MAX_SYNC_BPM - 0.5);
            text += "   "
                    + (atMax ? muse::qtrc("playback", "Max %1 BPM").arg(audiosync::MAX_SYNC_BPM, 0, 'f', 0)
                       : muse::qtrc("playback", "Min %1 BPM").arg(audiosync::MIN_SYNC_BPM, 0, 'f', 0));
        }

        const double x = xForAudioSecs(m_pointDrag->audioSecs);
        const double textWidth = metrics.horizontalAdvance(text) + 12;
        const QRectF box(std::clamp(x + 8, 0.0, std::max(0.0, width() - textWidth)), waveRect.top() + 4, textWidth, 18);
        painter->setPen(QPen(m_pointDrag->atLimit ? LIMIT_COLOR : m_gridColor, 1));
        painter->setBrush(m_rulerColor);
        painter->drawRoundedRect(box, 3, 3);
        painter->setBrush(Qt::NoBrush);
        painter->setPen(m_pointDrag->atLimit ? LIMIT_COLOR : m_textColor);
        painter->drawText(box, Qt::AlignCenter, text);
    }
}

void AudioSyncView::wheelEvent(QWheelEvent* event)
{
    const QPoint pixelDelta = event->pixelDelta();
    const QPoint angleDelta = event->angleDelta();
    const Qt::KeyboardModifiers modifiers = event->modifiers();

    if (modifiers & (Qt::ControlModifier | Qt::MetaModifier)) {
        const double steps = angleDelta.y() / 120.0;
        zoomAround(m_pixelsPerSecond * std::pow(ZOOM_STEP, steps), event->position().x());
        event->accept();
        return;
    }

    if (modifiers & Qt::AltModifier) {
        //! NOTE Alt turns vertical wheel deltas into horizontal ones on some platforms
        const int delta = angleDelta.y() != 0 ? angleDelta.y() : angleDelta.x();
        setAmplitudeZoom(m_amplitudeZoom * std::pow(ZOOM_STEP, delta / 120.0));
        event->accept();
        return;
    }

    // Horizontal scroll, from either axis (a plain mouse wheel only has the vertical one)
    double dx = 0.0;
    if (!pixelDelta.isNull()) {
        dx = std::abs(pixelDelta.x()) > std::abs(pixelDelta.y()) ? pixelDelta.x() : pixelDelta.y();
    } else {
        const int delta = std::abs(angleDelta.x()) > std::abs(angleDelta.y()) ? angleDelta.x() : angleDelta.y();
        dx = delta / 120.0 * width() / 8.0;
    }

    setScrollSecs(scrollSecs() - dx / m_pixelsPerSecond);
    event->accept();
}

// ---- painting ----

QRectF AudioSyncView::rulerRect() const
{
    return QRectF(0, 0, width(), RULER_HEIGHT);
}

QRectF AudioSyncView::railRect() const
{
    return QRectF(0, 2 * RULER_ROW_HEIGHT, width(), RAIL_HEIGHT);
}

QRectF AudioSyncView::waveformRect() const
{
    return QRectF(0, RULER_HEIGHT, width(),
                  std::max(0.0, height() - RULER_HEIGHT - RULER_ROW_HEIGHT - OVERVIEW_HEIGHT - OVERVIEW_GAP));
}

QRectF AudioSyncView::timeRulerRect() const
{
    return QRectF(0, waveformRect().bottom(), width(), RULER_ROW_HEIGHT);
}

QRectF AudioSyncView::overviewRect() const
{
    return QRectF(0, std::max(RULER_HEIGHT, height() - OVERVIEW_HEIGHT), width(), std::min(OVERVIEW_HEIGHT, height() - RULER_HEIGHT));
}

double AudioSyncView::overviewSecsAtX(double x) const
{
    const double start = contentStartSecs();
    return start + x / std::max(1.0, width()) * (contentEndSecs() - start);
}

double AudioSyncView::overviewXForSecs(double secs) const
{
    const double start = contentStartSecs();
    return (secs - start) / std::max(0.001, contentEndSecs() - start) * width();
}

void AudioSyncView::paintOverview(QPainter* painter, const QRectF& rect) const
{
    if (rect.height() <= 0) {
        return;
    }

    painter->fillRect(rect, m_backgroundColor);

    // The whole audio, both channels mixed
    if (m_peaks->isReady()) {
        const double sampleRate = m_peaks->sampleRate();
        const double centerY = rect.center().y();
        const double halfHeight = rect.height() / 2.0 - 2.0;
        const int xFrom = std::max(0, static_cast<int>(std::floor(overviewXForSecs(0.0))));
        const int xTo = std::min(static_cast<int>(std::ceil(rect.width())),
                                 static_cast<int>(std::ceil(overviewXForSecs(m_peaks->durationSecs()))));

        QColor color = m_waveformColor;
        color.setAlphaF(0.8);
        painter->setPen(QPen(color, 1));
        for (int x = xFrom; x < xTo; ++x) {
            const int64_t frameFrom = static_cast<int64_t>(std::floor(overviewSecsAtX(x) * sampleRate));
            const int64_t frameTo = std::max(frameFrom + 1, static_cast<int64_t>(std::floor(overviewSecsAtX(x + 1) * sampleRate)));
            const ReferenceAudioPeaks::MinMax mm = m_peaks->range(-1, frameFrom, frameTo);
            if (!mm.valid) {
                continue;
            }

            const double yTop = centerY - std::min(1.0, std::abs(static_cast<double>(mm.max))) * halfHeight;
            const double yBottom = centerY + std::min(1.0, std::abs(static_cast<double>(mm.min))) * halfHeight;
            painter->drawLine(QPointF(x + 0.5, rect.top() + (yTop - rect.top())),
                              QPointF(x + 0.5, std::max(yBottom, yTop + 1.0)));
        }
    }

    // The visible part as is, the rest darkened
    const double x1 = std::clamp(overviewXForSecs(scrollSecs()), 0.0, rect.width());
    const double x2 = std::clamp(overviewXForSecs(scrollSecs() + visibleSecs()), x1, rect.width());
    const QColor outsideColor(0, 0, 0, 90);
    painter->fillRect(QRectF(rect.left(), rect.top(), x1, rect.height()), outsideColor);
    painter->fillRect(QRectF(x2, rect.top(), rect.width() - x2, rect.height()), outsideColor);

    QColor windowBorder = m_textColor;
    windowBorder.setAlphaF(0.45);
    painter->setPen(QPen(windowBorder, 1));
    painter->setBrush(Qt::NoBrush);
    painter->drawRect(QRectF(x1, rect.top(), std::max(2.0, x2 - x1), rect.height()).adjusted(0.5, 0.5, -0.5, -0.5));

    // Its frame
    painter->setPen(QPen(m_gridColor, 1));
    painter->drawRect(rect.adjusted(0.5, 0.5, -0.5, -0.5));

    // Playback cursor
    const double cursorX = overviewXForSecs(cursorAudioSecs());
    if (cursorX >= 0 && cursorX <= rect.width()) {
        painter->setPen(QPen(m_cursorColor, 1));
        painter->drawLine(QPointF(cursorX, rect.top()), QPointF(cursorX, rect.bottom()));
    }
}

void AudioSyncView::paint(QPainter* painter)
{
    ensureGrid();

    //! NOTE Only the parts: the gap above the overview shows the panel's background
    const QRectF waveRect = waveformRect();
    painter->fillRect(waveRect, m_backgroundColor);

    // The score's part lighter, the rest (before/after the score) stays dark
    const double scoreStartX = std::max(0.0, xForAudioSecs(audioSecsOf(GridTime())));
    const double scoreEndX = std::min(width(), xForAudioSecs(audioSecsOf(m_scoreEnd)));
    if (!m_grid.empty() && scoreEndX > scoreStartX) {
        QColor scoreColor = m_textColor;
        scoreColor.setAlphaF(0.06);
        painter->fillRect(QRectF(scoreStartX, waveRect.top(), scoreEndX - scoreStartX, waveRect.height()), scoreColor);
    }

    // Range selection of the score
    if (m_selectionTickFrom >= 0) {
        QColor selectionColor = m_accentColor;
        selectionColor.setAlphaF(0.18);
        for (const GridMeasure& measure : m_grid) {
            if (measure.tickTo <= m_selectionTickFrom || measure.tickFrom >= m_selectionTickTo) {
                continue;
            }
            const double x1 = xForAudioSecs(audioSecsOf(measure.start));
            const double x2 = xForAudioSecs(audioSecsOf(measure.end));
            if (x2 >= 0 && x1 <= width()) {
                painter->fillRect(QRectF(x1, waveRect.top(), x2 - x1, waveRect.height()), selectionColor);
            }
        }
    }

    paintWaveform(painter, waveRect);
    paintGrid(painter, waveRect);
    paintLoop(painter, waveRect);
    paintRuler(painter, rulerRect());
    paintTimeRuler(painter, timeRulerRect());
    paintSyncPoints(painter, railRect(), waveRect);
    paintFileName(painter, waveRect);
    paintOverview(painter, overviewRect());

    // Playback cursor
    const double cursorX = xForAudioSecs(cursorAudioSecs());
    if (cursorX >= 0 && cursorX <= width()) {
        painter->setPen(QPen(m_cursorColor, 1.5));
        painter->drawLine(QPointF(cursorX, 0), QPointF(cursorX, timeRulerRect().bottom()));
    }
}

void AudioSyncView::paintRuler(QPainter* painter, const QRectF& rect) const
{
    painter->fillRect(rect, m_rulerColor);

    QFont font = painter->font();
    font.setPixelSize(10);
    painter->setFont(font);
    const QFontMetricsF metrics(font);

    const QRectF measuresRow(rect.left(), rect.top(), rect.width(), RULER_ROW_HEIGHT);
    const QRectF timeSigRow(rect.left(), measuresRow.bottom(), rect.width(), RULER_ROW_HEIGHT);
    const double textBaselineOffset = RULER_ROW_HEIGHT - 5;

    // Measure numbers (every Nth one when they're too close)
    double lastLabelEnd = -1e9;
    for (const GridMeasure& measure : m_grid) {
        const double x = xForAudioSecs(audioSecsOf(measure.start));
        const double endX = xForAudioSecs(audioSecsOf(measure.end));
        if (endX < 0) {
            continue;
        }
        if (x > rect.width()) {
            break;
        }

        painter->setPen(QPen(m_gridColor, 1));
        painter->drawLine(QPointF(x, measuresRow.top()), QPointF(x, measuresRow.bottom()));

        // Its beats: short ticks at the bottom, when they aren't crowded (as in the waveform)
        const double beatSpacing = (endX - x) / static_cast<double>(measure.beats.size() + 1);
        if (beatSpacing >= 6.0) {
            //! NOTE The measure numbers' color
            painter->setPen(QPen(m_textColor, 1));
            for (const GridTime& beat : measure.beats) {
                const double bx = xForAudioSecs(audioSecsOf(beat));
                painter->drawLine(QPointF(bx, measuresRow.bottom() - 5), QPointF(bx, measuresRow.bottom()));
            }
        }

        if (x + 3 > lastLabelEnd + 4) {
            painter->setPen(m_textColor);
            painter->drawText(QPointF(x + 3, measuresRow.top() + textBaselineOffset), measure.number);
            lastLabelEnd = x + 3 + metrics.horizontalAdvance(measure.number);
        }
    }

    // Time signatures, where they change: each one clipped before the next one
    for (size_t i = 0; i < m_grid.size(); ++i) {
        const GridMeasure& measure = m_grid[i];
        if (measure.timeSig.isEmpty()) {
            continue;
        }

        const double x = xForAudioSecs(audioSecsOf(measure.start));
        if (x > rect.width()) {
            break;
        }

        double nextX = rect.width();
        for (size_t j = i + 1; j < m_grid.size(); ++j) {
            if (!m_grid[j].timeSig.isEmpty()) {
                nextX = xForAudioSecs(audioSecsOf(m_grid[j].start));
                break;
            }
        }
        if (nextX < 0) {
            continue;
        }

        painter->setPen(QPen(m_accentColor, 1));
        painter->drawLine(QPointF(x, timeSigRow.top()), QPointF(x, timeSigRow.bottom()));

        const QRectF textRect(x + 3, timeSigRow.top(), std::max(0.0, nextX - x - 6), timeSigRow.height());
        if (metrics.horizontalAdvance(measure.timeSig) <= textRect.width()) {
            painter->setPen(m_accentColor);
            painter->drawText(textRect, Qt::AlignLeft | Qt::AlignVCenter, measure.timeSig);
        }
    }

    //! NOTE The measure numbers' row underlined in their color (the theme's text color: follows light/dark)
    painter->setPen(QPen(m_textColor, 1));
    painter->drawLine(QPointF(0, measuresRow.bottom()), QPointF(rect.width(), measuresRow.bottom()));
    painter->setPen(QPen(m_gridColor, 1));
    painter->drawLine(QPointF(0, timeSigRow.bottom()), QPointF(rect.width(), timeSigRow.bottom()));
    painter->drawLine(QPointF(0, rect.bottom()), QPointF(rect.width(), rect.bottom()));
}

void AudioSyncView::paintTimeRuler(QPainter* painter, const QRectF& rect) const
{
    painter->fillRect(rect, m_rulerColor);

    QFont font = painter->font();
    font.setPixelSize(10);
    painter->setFont(font);

    static const double STEPS[] = { 0.001, 0.002, 0.005, 0.01, 0.02, 0.05, 0.1, 0.2, 0.5, 1, 2, 5, 10, 15, 30, 60, 120, 300, 600 };
    const double minLabelSpacing = 70.0;
    double step = STEPS[std::size(STEPS) - 1];
    for (double s : STEPS) {
        if (s * m_pixelsPerSecond >= minLabelSpacing) {
            step = s;
            break;
        }
    }

    const bool withMs = step < 1.0;
    const double firstSecs = std::floor(audioSecsAtX(0) / step) * step;
    const double lastSecs = audioSecsAtX(rect.width());
    for (double secs = firstSecs; secs <= lastSecs + step; secs += step) {
        const double x = xForAudioSecs(secs);
        painter->setPen(QPen(m_gridColor, 1));
        painter->drawLine(QPointF(x, rect.top()), QPointF(x, rect.top() + 5));
        painter->setPen(m_textColor);
        painter->drawText(QPointF(x + 3, rect.bottom() - 4), formatTime(std::abs(secs) < step / 2 ? 0.0 : secs, withMs));
    }
}

void AudioSyncView::paintFileName(QPainter* painter, const QRectF& rect) const
{
    if (!hasAudio() || rect.height() < 24) {
        return;
    }

    QFont font = painter->font();
    font.setPixelSize(11);
    painter->setFont(font);

    QColor color = m_textColor;
    color.setAlphaF(0.55);
    painter->setPen(color);
    painter->drawText(rect.adjusted(8, 0, -8, -6), Qt::AlignLeft | Qt::AlignBottom, audioFileName());
}

void AudioSyncView::paintGrid(QPainter* painter, const QRectF& rect) const
{
    QPen measurePen(m_gridColor, 1);
    QColor beatColor = m_gridColor;
    beatColor.setAlphaF(0.6);
    QPen beatPen(beatColor, 1, Qt::DotLine);

    for (const GridMeasure& measure : m_grid) {
        const double x = xForAudioSecs(audioSecsOf(measure.start));
        const double endX = xForAudioSecs(audioSecsOf(measure.end));
        if (endX < 0) {
            continue;
        }
        if (x > rect.width()) {
            break;
        }

        painter->setPen(measurePen);
        painter->drawLine(QPointF(x, rect.top()), QPointF(x, rect.bottom()));

        // Beats only when they aren't crowded
        const double beatSpacing = (endX - x) / static_cast<double>(measure.beats.size() + 1);
        if (beatSpacing >= 6.0) {
            painter->setPen(beatPen);
            for (const GridTime& beat : measure.beats) {
                const double bx = xForAudioSecs(audioSecsOf(beat));
                painter->drawLine(QPointF(bx, rect.top()), QPointF(bx, rect.bottom()));
            }
        }
    }

    // The score's end
    const double endX = xForAudioSecs(audioSecsOf(m_scoreEnd));
    if (!m_grid.empty() && endX >= 0 && endX <= rect.width()) {
        painter->setPen(QPen(m_gridColor, 2));
        painter->drawLine(QPointF(endX, rect.top()), QPointF(endX, rect.bottom()));
    }
}

void AudioSyncView::paintWaveform(QPainter* painter, const QRectF& rect) const
{
    if (!m_peaks->isReady() || rect.height() <= 0) {
        return;
    }

    if (showStereo() && m_peaks->isStereo()) {
        const double laneHeight = rect.height() / 2.0;
        paintChannel(painter, QRectF(rect.left(), rect.top(), rect.width(), laneHeight), 0);
        paintChannel(painter, QRectF(rect.left(), rect.top() + laneHeight, rect.width(), laneHeight), 1);

        QColor separatorColor = m_gridColor;
        separatorColor.setAlphaF(0.5);
        painter->setPen(QPen(separatorColor, 1));
        painter->drawLine(QPointF(rect.left(), rect.top() + laneHeight), QPointF(rect.right(), rect.top() + laneHeight));
    } else {
        paintChannel(painter, rect, -1);
    }
}

double AudioSyncView::amplitudeToUnit(float amplitude) const
{
    const double zoomed = std::abs(static_cast<double>(amplitude)) * m_amplitudeZoom;
    return std::copysign(std::min(zoomed, 1.0), static_cast<double>(amplitude));
}

void AudioSyncView::paintChannel(QPainter* painter, const QRectF& rect, int channel) const
{
    const double centerY = rect.center().y();
    const double halfHeight = rect.height() / 2.0 - 1.0;
    const double sampleRate = m_peaks->sampleRate();
    const double framesPerPixel = sampleRate / m_pixelsPerSecond;

    QColor centerColor = m_gridColor;
    centerColor.setAlphaF(0.35);
    painter->setPen(QPen(centerColor, 1));
    painter->drawLine(QPointF(rect.left(), centerY), QPointF(rect.right(), centerY));

    const int xFrom = std::max(0, static_cast<int>(std::floor(xForAudioSecs(0.0))));
    const int xTo
        = std::min(static_cast<int>(std::ceil(rect.width())), static_cast<int>(std::ceil(xForAudioSecs(m_peaks->durationSecs()))));
    if (xTo <= xFrom) {
        return;
    }

    if (framesPerPixel < 1.0) {
        // Few samples on screen: a line through them
        const int64_t frameFrom = std::max<int64_t>(0, static_cast<int64_t>(std::floor(audioSecsAtX(xFrom) * sampleRate)));
        const int64_t frameTo = std::min(m_peaks->frames(), static_cast<int64_t>(std::ceil(audioSecsAtX(xTo) * sampleRate)) + 1);

        QPainterPath path;
        for (int64_t f = frameFrom; f < frameTo; ++f) {
            const double x = xForAudioSecs(f / sampleRate);
            const double y = centerY - amplitudeToUnit(m_peaks->sample(channel, f)) * halfHeight;
            if (f == frameFrom) {
                path.moveTo(x, y);
            } else {
                path.lineTo(x, y);
            }
        }

        painter->setPen(QPen(m_waveformColor, 1.2));
        painter->drawPath(path);
        return;
    }

    painter->setPen(QPen(m_waveformColor, 1));
    for (int x = xFrom; x < xTo; ++x) {
        const int64_t frameFrom = static_cast<int64_t>(std::floor(audioSecsAtX(x) * sampleRate));
        const int64_t frameTo = std::max(frameFrom + 1, static_cast<int64_t>(std::floor(audioSecsAtX(x + 1) * sampleRate)));
        const ReferenceAudioPeaks::MinMax mm = m_peaks->range(channel, frameFrom, frameTo);
        if (!mm.valid) {
            continue;
        }

        const double yTop = centerY - amplitudeToUnit(mm.max) * halfHeight;
        const double yBottom = centerY - amplitudeToUnit(mm.min) * halfHeight;
        painter->drawLine(QPointF(x + 0.5, yTop), QPointF(x + 0.5, std::max(yBottom, yTop + 1.0)));
    }
}

QColor AudioSyncView::backgroundColor() const { return m_backgroundColor; }
QColor AudioSyncView::rulerColor() const { return m_rulerColor; }
QColor AudioSyncView::waveformColor() const { return m_waveformColor; }
QColor AudioSyncView::gridColor() const { return m_gridColor; }
QColor AudioSyncView::textColor() const { return m_textColor; }
QColor AudioSyncView::accentColor() const { return m_accentColor; }
QColor AudioSyncView::cursorColor() const { return m_cursorColor; }

#define AUDIOSYNC_COLOR_SETTER(setter, member) \
    void AudioSyncView::setter(const QColor& color) \
    { \
        if (member == color) { \
            return; \
        } \
        member = color; \
        emit colorsChanged(); \
        update(); \
    }

AUDIOSYNC_COLOR_SETTER(setBackgroundColor, m_backgroundColor)
AUDIOSYNC_COLOR_SETTER(setRulerColor, m_rulerColor)
AUDIOSYNC_COLOR_SETTER(setWaveformColor, m_waveformColor)
AUDIOSYNC_COLOR_SETTER(setGridColor, m_gridColor)
AUDIOSYNC_COLOR_SETTER(setTextColor, m_textColor)
AUDIOSYNC_COLOR_SETTER(setAccentColor, m_accentColor)
AUDIOSYNC_COLOR_SETTER(setCursorColor, m_cursorColor)
