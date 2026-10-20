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
#include <vector>

#include <QColor>
#include <QQuickPaintedItem>
#include <QTimer>
#include <QUrl>
#include <qqmlintegration.h>

#include "async/asyncable.h"
#include "context/iglobalcontext.h"
#include "interactive/iinteractive.h"
#include "modularity/ioc.h"
#include "notation/inotationconfiguration.h"
#include "project/iprojectaudiosyncsettings.h"

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
    QML_ELEMENT

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

    Q_PROPERTY(double pixelsPerSecond READ pixelsPerSecond WRITE setPixelsPerSecond NOTIFY viewChanged)
    Q_PROPERTY(double scrollSecs READ scrollSecs WRITE setScrollSecs NOTIFY viewChanged)
    Q_PROPERTY(double contentStartSecs READ contentStartSecs NOTIFY viewChanged)
    Q_PROPERTY(double contentEndSecs READ contentEndSecs NOTIFY viewChanged)
    Q_PROPERTY(double visibleSecs READ visibleSecs NOTIFY viewChanged)
    Q_PROPERTY(double amplitudeZoom READ amplitudeZoom WRITE setAmplitudeZoom NOTIFY viewChanged)
    Q_PROPERTY(bool logarithmic READ logarithmic WRITE setLogarithmic NOTIFY viewChanged)
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

    muse::ContextInject<context::IGlobalContext> globalContext = { this }
    muse::ContextInject<IPlaybackController> playbackController = { this };
    muse::ContextInject<muse::IInteractive> interactive = { this };
    muse::GlobalInject<notation::INotationConfiguration> notationConfiguration;

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

    double pixelsPerSecond() const;
    void setPixelsPerSecond(double pps);
    double scrollSecs() const;
    void setScrollSecs(double secs);
    double contentStartSecs() const;
    double contentEndSecs() const;
    double visibleSecs() const;
    double amplitudeZoom() const;
    void setAmplitudeZoom(double zoom);
    bool logarithmic() const;
    void setLogarithmic(bool logarithmic);
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

    Q_INVOKABLE void zoomIn();
    Q_INVOKABLE void zoomOut();
    Q_INVOKABLE void zoomToFit();

    void paint(QPainter* painter) override;

signals:
    void audioChanged();
    void decodeStateChanged();
    void viewChanged();
    void positionTextChanged();
    void colorsChanged();

protected:
    void componentComplete() override;
    void geometryChange(const QRectF& newGeometry, const QRectF& oldGeometry) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void wheelEvent(QWheelEvent* event) override;

private:
    struct GridMeasure {
        double startSecs = 0.0;
        double endSecs = 0.0;
        int tickFrom = 0; // raw score ticks
        int tickTo = 0;
        QString number;
        QString timeSig; // only where it changes
        std::vector<double> beatSecs; // excluding the measure start
    };

    project::IProjectAudioSyncSettingsPtr audioSyncSettings() const;
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
    double audioSecsAtX(double x) const;
    double xForAudioSecs(double secs) const;
    double cursorAudioSecs() const;

    void zoomAround(double pps, double anchorX);
    void clampScroll();
    void updatePlaybackCursor();
    void updatePositionText();
    void seekToAudioSecs(double secs);

    QRectF rulerRect() const;
    QRectF waveformRect() const;

    void paintRuler(QPainter* painter, const QRectF& rect) const;
    void paintGrid(QPainter* painter, const QRectF& rect) const;
    void paintWaveform(QPainter* painter, const QRectF& rect) const;
    void paintChannel(QPainter* painter, const QRectF& rect, int channel) const;
    double amplitudeToUnit(float amplitude) const;

    ReferenceAudioPeaks* m_peaks = nullptr;

    double m_pixelsPerSecond = 50.0;
    double m_scrollSecs = -1.0;
    double m_amplitudeZoom = 1.0;
    bool m_logarithmic = false;
    bool m_followPlayback = true;

    mutable bool m_gridDirty = true;
    mutable std::vector<GridMeasure> m_grid;
    mutable double m_scoreEndSecs = 0.0;

    int m_selectionTickFrom = -1;
    int m_selectionTickTo = -1;

    double m_lastPositionSecs = 0.0; // engine seconds
    std::chrono::steady_clock::time_point m_lastPositionTime;
    bool m_isPlaying = false;
    QTimer m_cursorTimer;

    bool m_isSeekDragging = false;

    QString m_positionText;
    QString m_tempoText;

    QColor m_backgroundColor = QColor(40, 40, 40);
    QColor m_rulerColor = QColor(55, 55, 55);
    QColor m_waveformColor = QColor(80, 160, 230);
    QColor m_gridColor = QColor(120, 120, 120);
    QColor m_textColor = QColor(220, 220, 220);
    QColor m_accentColor = QColor(80, 160, 230);
    QColor m_cursorColor = QColor(255, 120, 60);
};
}
