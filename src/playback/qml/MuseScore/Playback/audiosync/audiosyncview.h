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

#pragma once

#include <chrono>
#include <optional>
#include <vector>

#include <QColor>
#include <QQuickPaintedItem>
#include <QTimer>
#include <QUrl>
#include <QStringList>
#include <QVariantMap>
#include <qqmlintegration.h>

#include "async/asyncable.h"
#include "context/iglobalcontext.h"
#include "interactive/iinteractive.h"
#include "modularity/ioc.h"
#include "notation/inotationconfiguration.h"
#include "project/iprojectaudiosyncsettings.h"

#include "notation/inotationautomation.h"
#include "playback/internal/audiosyncedits.h"

#include "iplaybackconfiguration.h"
#include "iplaybackcontroller.h"

namespace mu::engraving {
class Score;
}

namespace mu::playback {
class ReferenceAudioPeaks;

//! NOTE The Audio sync window's main view: the reference audio's waveform under a ruler of the score's
//! measures (numbers, time signatures) and of real time (min:sec), measure lines solid and beats dotted,
//! all placed by the score's current tempo, with the shared playback cursor.
//! Positions are in "audio seconds": the reference audio's own time.
class AudioSyncView : public QQuickPaintedItem, public muse::Contextable, public muse::async::Asyncable
{
    Q_OBJECT

    Q_PROPERTY(bool hasAudio READ hasAudio NOTIFY audioChanged)
    Q_PROPERTY(QString audioFileName READ audioFileName NOTIFY audioChanged)
    Q_PROPERTY(QString audioPath READ audioPath NOTIFY audioChanged)
    Q_PROPERTY(bool audioFileMissing READ audioFileMissing NOTIFY audioChanged)
    Q_PROPERTY(bool decoding READ decoding NOTIFY decodeStateChanged)
    Q_PROPERTY(double decodeProgress READ decodeProgress NOTIFY decodeStateChanged)
    Q_PROPERTY(bool decodeFailed READ decodeFailed NOTIFY decodeStateChanged)
    Q_PROPERTY(bool waveformReady READ waveformReady NOTIFY decodeStateChanged)
    Q_PROPERTY(bool muted READ muted WRITE setMuted NOTIFY audioChanged)
    Q_PROPERTY(bool solo READ solo WRITE setSolo NOTIFY audioChanged)
    Q_PROPERTY(bool showStereo READ showStereo WRITE setShowStereo NOTIFY audioChanged)
    Q_PROPERTY(bool isStereoAudio READ isStereoAudio NOTIFY decodeStateChanged)
    Q_PROPERTY(bool audioSyncEnabled READ audioSyncEnabled WRITE setAudioSyncEnabled NOTIFY syncChanged)
    //! NOTE Points other than the first one (the score's start)
    Q_PROPERTY(bool hasSyncPoints READ hasSyncPoints NOTIFY syncChanged)
    Q_PROPERTY(bool isPlaying READ isPlaying NOTIFY playingChanged)
    Q_PROPERTY(bool metronomeEnabled READ metronomeEnabled NOTIFY metronomeChanged)
    //! NOTE "position / duration", of the reference audio
    Q_PROPERTY(QString timeText READ timeText NOTIFY positionTextChanged)
    //! NOTE The A-B loop (see ReferenceAudioSettings::loopEnabled)
    Q_PROPERTY(bool loopEnabled READ loopEnabled WRITE setLoopEnabled NOTIFY audioChanged)
    Q_PROPERTY(bool hasLoopFrom READ hasLoopFrom NOTIFY audioChanged)
    Q_PROPERTY(bool hasLoopTo READ hasLoopTo NOTIFY audioChanged)
    //! NOTE The playback speed isn't 100%: the reference audio doesn't follow it
    Q_PROPERTY(bool speedWarning READ speedWarning NOTIFY speedWarningChanged)
    Q_PROPERTY(bool detecting READ detecting NOTIFY detectionChanged)
    Q_PROPERTY(double detectionProgress READ detectionProgress NOTIFY detectionChanged)
    //! NOTE What the last detection found (e.g. its tempo), empty before
    Q_PROPERTY(QString detectionSummary READ detectionSummary NOTIFY detectionChanged)

    Q_PROPERTY(double pixelsPerSecond READ pixelsPerSecond WRITE setPixelsPerSecond NOTIFY viewChanged)
    Q_PROPERTY(double scrollSecs READ scrollSecs WRITE setScrollSecs NOTIFY viewChanged)
    Q_PROPERTY(double contentStartSecs READ contentStartSecs NOTIFY viewChanged)
    Q_PROPERTY(double contentEndSecs READ contentEndSecs NOTIFY viewChanged)
    Q_PROPERTY(double visibleSecs READ visibleSecs NOTIFY viewChanged)
    Q_PROPERTY(double amplitudeZoom READ amplitudeZoom WRITE setAmplitudeZoom NOTIFY viewChanged)
    //! NOTE Where the main waveform is, for controls placed along it
    Q_PROPERTY(double waveformTop READ waveformTop NOTIFY viewChanged)
    Q_PROPERTY(double waveformHeight READ waveformHeight NOTIFY viewChanged)
    Q_PROPERTY(bool followPlayback READ followPlayback WRITE setFollowPlayback NOTIFY viewChanged)

    Q_PROPERTY(QString positionText READ positionText NOTIFY positionTextChanged)
    Q_PROPERTY(QString tempoText READ tempoText NOTIFY positionTextChanged)

    Q_PROPERTY(QColor backgroundColor READ backgroundColor WRITE setBackgroundColor NOTIFY colorsChanged)
    Q_PROPERTY(QColor rulerColor READ rulerColor WRITE setRulerColor NOTIFY colorsChanged)
    Q_PROPERTY(QColor waveformColor READ waveformColor WRITE setWaveformColor NOTIFY colorsChanged)
    Q_PROPERTY(QColor gridColor READ gridColor WRITE setGridColor NOTIFY colorsChanged)
    Q_PROPERTY(QColor textColor READ textColor WRITE setTextColor NOTIFY colorsChanged)
    Q_PROPERTY(QColor accentColor READ accentColor WRITE setAccentColor NOTIFY colorsChanged)
    Q_PROPERTY(QColor cursorColor READ cursorColor WRITE setCursorColor NOTIFY colorsChanged)

    QML_ELEMENT

    muse::ContextInject<context::IGlobalContext> globalContext = { this };
    muse::ContextInject<IPlaybackController> playbackController = { this };
    muse::ContextInject<muse::IInteractive> interactive = { this };
    muse::GlobalInject<notation::INotationConfiguration> notationConfiguration;
    muse::GlobalInject<IPlaybackConfiguration> playbackConfiguration;

public:
    explicit AudioSyncView(QQuickItem* parent = nullptr);
    ~AudioSyncView() override;

    bool hasAudio() const;
    QString audioFileName() const;
    QString audioPath() const;
    bool audioFileMissing() const;
    bool decoding() const;
    double decodeProgress() const;
    bool decodeFailed() const;
    bool waveformReady() const;
    bool muted() const;
    void setMuted(bool muted);
    bool solo() const;
    void setSolo(bool solo);
    bool showStereo() const;
    void setShowStereo(bool showStereo);
    bool isStereoAudio() const;
    bool audioSyncEnabled() const;
    bool hasSyncPoints() const;
    void setAudioSyncEnabled(bool enabled);
    bool isPlaying() const;
    bool metronomeEnabled() const;
    QString timeText() const;
    bool loopEnabled() const;
    void setLoopEnabled(bool enabled);
    bool hasLoopFrom() const;
    bool hasLoopTo() const;
    bool speedWarning() const;
    bool detecting() const;
    double detectionProgress() const;
    QString detectionSummary() const;

    double pixelsPerSecond() const;
    void setPixelsPerSecond(double pps);
    double scrollSecs() const;
    void setScrollSecs(double secs);
    double contentStartSecs() const;
    double contentEndSecs() const;
    double visibleSecs() const;
    double amplitudeZoom() const;
    void setAmplitudeZoom(double zoom);
    double waveformTop() const;
    double waveformHeight() const;
    bool followPlayback() const;
    void setFollowPlayback(bool follow);

    QString positionText() const;
    QString tempoText() const;

    QColor backgroundColor() const;
    void setBackgroundColor(const QColor& color);
    QColor rulerColor() const;
    void setRulerColor(const QColor& color);
    QColor waveformColor() const;
    void setWaveformColor(const QColor& color);
    QColor gridColor() const;
    void setGridColor(const QColor& color);
    QColor textColor() const;
    void setTextColor(const QColor& color);
    QColor accentColor() const;
    void setAccentColor(const QColor& color);
    QColor cursorColor() const;
    void setCursorColor(const QColor& color);

    Q_INVOKABLE void chooseAudioFile();
    Q_INVOKABLE bool canDropUrls(const QList<QUrl>& urls) const;
    Q_INVOKABLE void dropUrls(const QList<QUrl>& urls);
    Q_INVOKABLE void removeAudio();

    //! NOTE At the playback position, or cleared if already set
    Q_INVOKABLE void toggleLoopFrom();
    Q_INVOKABLE void toggleLoopTo();
    Q_INVOKABLE void togglePlay();
    Q_INVOKABLE void stopPlayback();
    Q_INVOKABLE void rewindToStart();
    //! NOTE To the A-B loop's start (A)
    Q_INVOKABLE void goToLoopStart();
    Q_INVOKABLE void toggleMetronome();

    Q_INVOKABLE QStringList recentAudioFiles() const;
    Q_INVOKABLE void clearRecentAudioFiles();
    Q_INVOKABLE void setAudioFile(const QString& path);

    //! NOTE Automatic sync points, from the score's start (the first sync point, or the music's start).
    //! params: sensitivity [0; 1], minBpm, maxBpm, octave (0 auto, 1 none, 2 double, 3 half), everyMeasure, toleranceMs,
    //! ignoreLeadingSilence
    Q_INVOKABLE void detectPoints(const QVariantMap& params);
    Q_INVOKABLE void cancelDetection();
    //! NOTE All the sync points but the score's start (and the locked ones)
    Q_INVOKABLE void clearSyncPoints();

    Q_INVOKABLE void zoomIn();
    Q_INVOKABLE void zoomOut();
    Q_INVOKABLE void zoomToFit();

    void paint(QPainter* painter) override;

signals:
    void audioChanged();
    void decodeStateChanged();
    void viewChanged();
    void positionTextChanged();
    void syncChanged();
    void playingChanged();
    void metronomeChanged();
    void speedWarningChanged();
    void detectionChanged();
    void colorsChanged();

protected:
    void componentComplete() override;
    void geometryChange(const QRectF& newGeometry, const QRectF& oldGeometry) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void mouseDoubleClickEvent(QMouseEvent* event) override;
    void wheelEvent(QWheelEvent* event) override;
    void keyPressEvent(QKeyEvent* event) override;
    bool event(QEvent* event) override;

private:
    //! NOTE A position of the unrolled score, with its playback time
    struct GridTime {
        int utick = 0;
        double secs = 0.0; // score playback seconds
    };

    struct GridMeasure {
        GridTime start;
        GridTime end;
        int tickFrom = 0; // raw score ticks
        int tickTo = 0;
        QString number;
        QString timeSig; // only where it changes
        std::vector<GridTime> beats; // excluding the measure start
    };

    //! NOTE An audio sync point being dragged: the grid follows it before the edit is made
    struct PointDrag {
        size_t index = 0;
        double grabOffsetSecs = 0.0;
        double originalSecs = 0.0;
        double audioSecs = 0.0;
        bool moved = false;
        //! NOTE Stopped by a neighboring segment's tempo limit (MIN/MAX_SYNC_BPM): shown, so the user knows why
        bool atLimit = false;
    };

    project::IProjectAudioSyncSettingsPtr audioSyncSettings() const;
    notation::INotationAutomationPtr automation() const;
    const engraving::Score* score() const;
    bool isPlayRepeatsEnabled() const;

    void onProjectChanged();
    void onNotationChanged();
    void onDecodeStateChanged();
    void listenPlaybackState();

    void invalidateGrid();
    void ensureGrid() const;
    void rebuildGrid() const;

    //! NOTE Audio seconds = engine (score playback) seconds + offset
    double audioOffsetSecs() const;
    //! NOTE Where a score position is in the audio (following a point being dragged)
    double audioSecsOf(const GridTime& time) const;
    double previewAudioSecs(int utick) const;

    // Audio sync points
    std::vector<double> displayedPointSecs() const;
    static bool isUncertainPoint(const audiosync::SyncPoint& point);
    std::optional<size_t> pointAt(const QPointF& pos) const;
    std::optional<size_t> selectedPointIndex() const;
    void addPointAt(double audioSecs, bool snapToBeat);
    void nudgeSelectedPoint(double deltaSecs);
    void deleteSelectedPoint();
    void paintSyncPoints(QPainter* painter, const QRectF& railRect, const QRectF& waveRect) const;
    void paintLoop(QPainter* painter, const QRectF& waveRect) const;
    QRectF loopBandRect() const;
    //! NOTE The A-B loop as shown (following a drag)
    std::pair<double, double> displayedLoop() const;
    void setLoop(double fromSecs, double toSecs);
    void updateSpeedWarning();
    double audioSecsAtX(double x) const;
    double xForAudioSecs(double secs) const;
    double cursorAudioSecs() const;

    void zoomAround(double pps, double anchorX);
    void restoreViewState();
    void storeViewState();
    void clampScroll();
    void updatePlaybackCursor();
    void updatePositionText();
    void seekToAudioSecs(double secs);

    QRectF rulerRect() const;
    QRectF railRect() const;
    QRectF timeRulerRect() const;
    QRectF waveformRect() const;
    //! NOTE The whole audio in small, below the waveform, with the visible part highlighted
    QRectF overviewRect() const;
    double overviewSecsAtX(double x) const;
    double overviewXForSecs(double secs) const;
    void paintOverview(QPainter* painter, const QRectF& rect) const;

    void paintRuler(QPainter* painter, const QRectF& rect) const;
    void paintTimeRuler(QPainter* painter, const QRectF& rect) const;
    void paintFileName(QPainter* painter, const QRectF& rect) const;
    void paintGrid(QPainter* painter, const QRectF& rect) const;
    void paintWaveform(QPainter* painter, const QRectF& rect) const;
    void paintChannel(QPainter* painter, const QRectF& rect, int channel) const;
    double amplitudeToUnit(float amplitude) const;

    ReferenceAudioPeaks* m_peaks = nullptr;

    double m_pixelsPerSecond = 50.0;
    double m_scrollSecs = -1.0;
    double m_amplitudeZoom = 1.0;
    bool m_followPlayback = true;

    mutable bool m_gridDirty = true;
    mutable std::vector<GridMeasure> m_grid;
    mutable double m_scoreEndSecs = 0.0;
    mutable GridTime m_scoreEnd;
    mutable std::vector<audiosync::SyncPoint> m_syncPoints;
    mutable bool m_audioSyncEnabled = false;
    mutable double m_audioOffsetSecs = 0.0;

    int m_selectedPointUtick = -1;

    struct DetectionJob;
    std::shared_ptr<DetectionJob> m_detectionJob;
    double m_detectionProgress = 0.0;
    QString m_detectionSummary;
    void onDetectionFinished(const std::shared_ptr<DetectionJob>& job, const std::vector<audiosync::DetectedBeatTime>& beats,
                             std::optional<double> musicStartSecs);
    bool m_speedWarning = false;
    std::optional<PointDrag> m_pointDrag;

    int m_selectionTickFrom = -1;
    int m_selectionTickTo = -1;

    double m_lastPositionSecs = 0.0; // engine seconds
    std::chrono::steady_clock::time_point m_lastPositionTime;
    bool m_isPlaying = false;
    QTimer m_cursorTimer;

    enum class DragMode {
        None,
        Seek,
        Overview, // moving the visible window in the overview
        Point,
        LoopFrom,
        LoopTo,
        LoopBoth,
    };

    //! NOTE The A-B loop being dragged, written when released
    struct LoopDrag {
        double fromSecs = -1.0;
        double toSecs = -1.0;
        double grabSecs = 0.0;
        double originalFromSecs = -1.0;
        double originalToSecs = -1.0;
        bool enableOnRelease = false; // a new loop, marked by dragging the playback position
    };
    double m_seekPressX = 0.0;
    std::optional<LoopDrag> m_loopDrag;

    DragMode m_dragMode = DragMode::None;
    double m_overviewGrabOffsetSecs = 0.0; // where the visible window was grabbed, from its start

    QString m_positionText;
    QString m_tempoText;
    QString m_timeText;

    QColor m_backgroundColor = QColor(40, 40, 40);
    QColor m_rulerColor = QColor(55, 55, 55);
    QColor m_waveformColor = QColor(80, 160, 230);
    QColor m_gridColor = QColor(120, 120, 120);
    QColor m_textColor = QColor(220, 220, 220);
    QColor m_accentColor = QColor(80, 160, 230);
    QColor m_cursorColor = QColor(255, 120, 60);
};
}
