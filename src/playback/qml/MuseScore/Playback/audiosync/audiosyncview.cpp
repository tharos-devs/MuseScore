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

#include <QFileInfo>
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
#include "project/inotationproject.h"

#include "playback/internal/referenceaudiofile.h"

#include "referenceaudiopeaks.h"

#include "translation.h"

using namespace mu::playback;
using namespace mu::engraving;
using namespace muse;

namespace {
constexpr double RULER_ROW_HEIGHT = 18.0;
constexpr double RULER_HEIGHT = 3 * RULER_ROW_HEIGHT; // measures, time, time signatures
constexpr double OVERVIEW_HEIGHT = 34.0;
constexpr double OVERVIEW_GAP = 4.0;
constexpr double MIN_PIXELS_PER_SECOND = 0.5;
constexpr double MAX_PIXELS_PER_SECOND = 48000.0; // ~1 px per sample
constexpr double ZOOM_STEP = 1.41421356237;
constexpr double MIN_AMPLITUDE_ZOOM = 0.25;
constexpr double MAX_AMPLITUDE_ZOOM = 64.0;
constexpr double LOG_RANGE_DB = 60.0;
constexpr int CURSOR_INTERVAL_MS = 16;

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

    m_peaks = new ReferenceAudioPeaks(this);
    connect(m_peaks, &ReferenceAudioPeaks::readyChanged, this, [this]() {
        if (m_peaks->isReady() && m_scrollSecs < 0) {
            zoomToFit();
        }
        emit decodeStateChanged();
        emit viewChanged();
        update();
    });

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
                }, Asyncable::Mode::SetReplace);
            }
        }
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

void AudioSyncView::chooseAudioFile()
{
    audiosync::chooseReferenceAudio(interactive().get(), audioSyncSettings());
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

    audiosync::setReferenceAudioPath(audioSyncSettings(), urls.first().toLocalFile());
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

bool AudioSyncView::logarithmic() const
{
    return m_logarithmic;
}

void AudioSyncView::setLogarithmic(bool logarithmic)
{
    if (m_logarithmic == logarithmic) {
        return;
    }

    m_logarithmic = logarithmic;
    emit viewChanged();
    update();
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
        zoomToFit();
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
    //! TODO Audio sync points: the audio time of the score's start
    return 0.0;
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
    update();
}

void AudioSyncView::updatePositionText()
{
    ensureGrid();

    const double secs = cursorAudioSecs();
    QString positionText = formatTime(secs, true);
    QString tempoText;

    const Score* sc = score();
    if (sc) {
        const double engineSecs = secs - audioOffsetSecs();
        auto it = std::upper_bound(m_grid.begin(), m_grid.end(), engineSecs, [](double s, const GridMeasure& m) {
            return s < m.startSecs;
        });
        if (it != m_grid.begin()) {
            const GridMeasure& measure = *std::prev(it);
            const int beat = 1 + static_cast<int>(std::upper_bound(measure.beatSecs.begin(), measure.beatSecs.end(), engineSecs)
                                                  - measure.beatSecs.begin());
            //: Position in the score: measure number and beat, e.g. "Measure 12, beat 3"
            positionText = muse::qtrc("playback", "Measure %1, beat %2").arg(measure.number).arg(beat) + "  ·  " + positionText;
        }

        const TempoTimeline& timeline = sc->tempoTimeline(isPlayRepeatsEnabled());
        const int utick = timeline.utime2utick(std::max(0.0, engineSecs));
        //: Tempo in beats (quarter notes) per minute
        tempoText = muse::qtrc("playback", "%1 BPM").arg(timeline.tempo(utick).toBPM().val, 0, 'f', 1);
    }

    if (positionText != m_positionText || tempoText != m_tempoText) {
        m_positionText = positionText;
        m_tempoText = tempoText;
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
        gridMeasure.startSecs = timeline.utick2utime(tickFrom + tickOffset);
        gridMeasure.endSecs = timeline.utick2utime(tickTo + tickOffset);
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
                gridMeasure.beatSecs.push_back(timeline.utick2utime(beatTick + tickOffset));
            }
        }

        m_scoreEndSecs = std::max(m_scoreEndSecs, gridMeasure.endSecs);
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
        return a.startSecs < b.startSecs;
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

    m_dragMode = DragMode::Seek;
    seekToAudioSecs(audioSecsAtX(pos.x()));
    event->accept();
}

void AudioSyncView::mouseMoveEvent(QMouseEvent* event)
{
    //! NOTE A release can be lost (e.g. outside the window): the buttons tell
    if (m_dragMode == DragMode::None || !(event->buttons() & Qt::LeftButton)) {
        m_dragMode = DragMode::None;
        return;
    }

    if (m_dragMode == DragMode::Overview) {
        setScrollSecs(overviewSecsAtX(event->position().x()) - m_overviewGrabOffsetSecs);
        return;
    }

    seekToAudioSecs(audioSecsAtX(event->position().x()));
}

void AudioSyncView::mouseReleaseEvent(QMouseEvent*)
{
    m_dragMode = DragMode::None;
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

QRectF AudioSyncView::waveformRect() const
{
    return QRectF(0, RULER_HEIGHT, width(), std::max(0.0, height() - RULER_HEIGHT - OVERVIEW_HEIGHT - OVERVIEW_GAP));
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

    painter->fillRect(rect, m_rulerColor);

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

    // The visible part
    const double x1 = std::max(0.0, overviewXForSecs(scrollSecs()));
    const double x2 = std::min(rect.width(), overviewXForSecs(scrollSecs() + visibleSecs()));
    QColor windowColor = m_textColor;
    windowColor.setAlphaF(0.14);
    const QRectF windowRect(x1, rect.top(), std::max(2.0, x2 - x1), rect.height());
    painter->fillRect(windowRect, windowColor);
    QColor windowBorder = m_textColor;
    windowBorder.setAlphaF(0.35);
    painter->setPen(QPen(windowBorder, 1));
    painter->drawRect(windowRect.adjusted(0.5, 0.5, -0.5, -0.5));

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

    painter->fillRect(boundingRect(), m_backgroundColor);

    const QRectF waveRect = waveformRect();

    // Outside the score: dimmed
    const double scoreStartX = xForAudioSecs(audioOffsetSecs());
    const double scoreEndX = xForAudioSecs(audioOffsetSecs() + m_scoreEndSecs);
    QColor outsideColor = m_textColor;
    outsideColor.setAlphaF(0.06);
    if (scoreStartX > 0) {
        painter->fillRect(QRectF(0, waveRect.top(), scoreStartX, waveRect.height()), outsideColor);
    }
    if (scoreEndX < width()) {
        painter->fillRect(QRectF(scoreEndX, waveRect.top(), width() - scoreEndX, waveRect.height()), outsideColor);
    }

    // Range selection of the score
    if (m_selectionTickFrom >= 0) {
        QColor selectionColor = m_accentColor;
        selectionColor.setAlphaF(0.18);
        for (const GridMeasure& measure : m_grid) {
            if (measure.tickTo <= m_selectionTickFrom || measure.tickFrom >= m_selectionTickTo) {
                continue;
            }
            const double x1 = xForAudioSecs(measure.startSecs + audioOffsetSecs());
            const double x2 = xForAudioSecs(measure.endSecs + audioOffsetSecs());
            if (x2 >= 0 && x1 <= width()) {
                painter->fillRect(QRectF(x1, waveRect.top(), x2 - x1, waveRect.height()), selectionColor);
            }
        }
    }

    paintWaveform(painter, waveRect);
    paintGrid(painter, waveRect);
    paintRuler(painter, rulerRect());
    paintOverview(painter, overviewRect());

    // Playback cursor
    const double cursorX = xForAudioSecs(cursorAudioSecs());
    if (cursorX >= 0 && cursorX <= width()) {
        painter->setPen(QPen(m_cursorColor, 1.5));
        painter->drawLine(QPointF(cursorX, 0), QPointF(cursorX, waveRect.bottom()));
    }
}

void AudioSyncView::paintRuler(QPainter* painter, const QRectF& rect) const
{
    painter->fillRect(rect, m_rulerColor);

    QFont font = painter->font();
    font.setPixelSize(10);
    painter->setFont(font);
    const QFontMetricsF metrics(font);

    const double offset = audioOffsetSecs();
    const QRectF measuresRow(rect.left(), rect.top(), rect.width(), RULER_ROW_HEIGHT);
    const QRectF timeRow(rect.left(), measuresRow.bottom(), rect.width(), RULER_ROW_HEIGHT);
    const QRectF timeSigRow(rect.left(), timeRow.bottom(), rect.width(), RULER_ROW_HEIGHT);
    const double textBaselineOffset = RULER_ROW_HEIGHT - 5;

    // Measure numbers (every Nth one when they're too close)
    double lastLabelEnd = -1e9;
    for (const GridMeasure& measure : m_grid) {
        const double x = xForAudioSecs(measure.startSecs + offset);
        const double endX = xForAudioSecs(measure.endSecs + offset);
        if (endX < 0) {
            continue;
        }
        if (x > rect.width()) {
            break;
        }

        painter->setPen(QPen(m_gridColor, 1));
        painter->drawLine(QPointF(x, measuresRow.top()), QPointF(x, measuresRow.bottom()));

        if (x + 3 > lastLabelEnd + 4) {
            painter->setPen(m_textColor);
            painter->drawText(QPointF(x + 3, measuresRow.top() + textBaselineOffset), measure.number);
            lastLabelEnd = x + 3 + metrics.horizontalAdvance(measure.number);
        }
    }

    // Real time
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
        painter->drawLine(QPointF(x, timeRow.bottom() - 5), QPointF(x, timeRow.bottom()));
        painter->setPen(m_textColor);
        painter->drawText(QPointF(x + 3, timeRow.top() + textBaselineOffset), formatTime(std::abs(secs) < step / 2 ? 0.0 : secs, withMs));
    }

    // Time signatures, where they change: each one clipped before the next one
    for (size_t i = 0; i < m_grid.size(); ++i) {
        const GridMeasure& measure = m_grid[i];
        if (measure.timeSig.isEmpty()) {
            continue;
        }

        const double x = xForAudioSecs(measure.startSecs + offset);
        if (x > rect.width()) {
            break;
        }

        double nextX = rect.width();
        for (size_t j = i + 1; j < m_grid.size(); ++j) {
            if (!m_grid[j].timeSig.isEmpty()) {
                nextX = xForAudioSecs(m_grid[j].startSecs + offset);
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

    painter->setPen(QPen(m_gridColor, 1));
    painter->drawLine(QPointF(0, measuresRow.bottom()), QPointF(rect.width(), measuresRow.bottom()));
    painter->drawLine(QPointF(0, timeRow.bottom()), QPointF(rect.width(), timeRow.bottom()));
    painter->drawLine(QPointF(0, rect.bottom()), QPointF(rect.width(), rect.bottom()));
}

void AudioSyncView::paintGrid(QPainter* painter, const QRectF& rect) const
{
    const double offset = audioOffsetSecs();

    QPen measurePen(m_gridColor, 1);
    QColor beatColor = m_gridColor;
    beatColor.setAlphaF(0.6);
    QPen beatPen(beatColor, 1, Qt::DotLine);

    for (const GridMeasure& measure : m_grid) {
        const double x = xForAudioSecs(measure.startSecs + offset);
        const double endX = xForAudioSecs(measure.endSecs + offset);
        if (endX < 0) {
            continue;
        }
        if (x > rect.width()) {
            break;
        }

        painter->setPen(measurePen);
        painter->drawLine(QPointF(x, rect.top()), QPointF(x, rect.bottom()));

        // Beats only when they aren't crowded
        const double beatSpacing = (endX - x) / static_cast<double>(measure.beatSecs.size() + 1);
        if (beatSpacing >= 6.0) {
            painter->setPen(beatPen);
            for (double beatSecs : measure.beatSecs) {
                const double bx = xForAudioSecs(beatSecs + offset);
                painter->drawLine(QPointF(bx, rect.top()), QPointF(bx, rect.bottom()));
            }
        }
    }

    // The score's end
    const double endX = xForAudioSecs(m_scoreEndSecs + offset);
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
    double unit = zoomed;
    if (m_logarithmic) {
        unit = zoomed <= 0.0 ? 0.0 : std::max(0.0, 1.0 + 20.0 * std::log10(zoomed) / LOG_RANGE_DB);
    }

    return std::copysign(std::min(unit, 1.0), static_cast<double>(amplitude));
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
