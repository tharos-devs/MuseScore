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

#include <vector>

#include <QElapsedTimer>
#include <QGraphicsView>
#include <QSplitter>
#include <QTimer>

#include "modularity/ioc.h"
#include "ui/iuiconfiguration.h"
#include "notation/inotation.h"
#include "project/iprojectvideosettings.h"
#include "project/iprojectaudiosettings.h"
#include "async/asyncable.h"
#include "context/iglobalcontext.h"
#include "actions/iactionsdispatcher.h"
#include "interactive/iinteractive.h"
#include "playback/iplaybackcontroller.h"
#include "playback/iplaybackconfiguration.h"
#include "notationscene/inotationsceneconfiguration.h"

#include "timelinevideothumbnails.h"

namespace mu::engraving {
class Measure;
class Page;
class Part;
class Score;
class Staff;
enum class SelState : char;
}

namespace mu::notation {
class Timeline;

class TRowLabels : public QGraphicsView
{
    Q_OBJECT

    friend class TimelineAdapter;

public:
    enum class MouseOverValue {
        NONE,
        MOVE_UP_ARROW,
        MOVE_DOWN_ARROW,
        MOVE_UP_DOWN_ARROW,
        COLLAPSE_UP_ARROW,
        COLLAPSE_DOWN_ARROW,
        OPEN_EYE,
        CLOSED_EYE
    };

private:
    QSplitter* _splitter { nullptr };
    Timeline* _timeline { nullptr };

    QPoint _oldLoc;

    bool _dragging = false;

    std::vector<std::pair<QGraphicsItem*, int> > _metaLabels;
    std::map<MouseOverValue, QPixmap*> _mouseoverMap;
    std::tuple<QGraphicsPixmapItem*, MouseOverValue, unsigned> _oldItemInfo;

    //! NOTE: row = instrument row (part index), labelRow = row among all the labels (metas included)
    void addTrackButtons(int row, unsigned labelRow, int ypos, int height, bool anythingSoloed);

    void resizeEvent(QResizeEvent*) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void contextMenuEvent(QContextMenuEvent*) override;
    void leaveEvent(QEvent*) override;

private slots:
    void restrictScroll(int value);

public slots:
    void mouseOver(QPointF scenePt);

signals:
    void moved(QPointF p);
    void swapMeta(unsigned r, bool up);
    void requestContextMenu(QContextMenuEvent*);

public:
    TRowLabels(QSplitter* splitter, Timeline* time);

    bool handleEvent(QEvent* event);

    void updateLabels(std::vector<std::pair<QString, bool> > labels, int height);
    QString cursorIsOn();

    //! NOTE: the Video row's label, above the other rows (0: hidden), see Timeline::updateVideoBand()
    void setVideoBand(int height);

private:
    QWidget* m_videoLabel = nullptr;
};

struct TimelineTheme {
    QColor backgroundColor, labelsColor1, labelsColor2, labelsColor3, gridColor1, gridColor2;
    QColor measureMetaColor, selectionColor, nonVisiblePenColor, nonVisibleBrushColor, colorBoxColor;
    QColor metaValuePenColor, metaValueBrushColor;
};

class Timeline : public QGraphicsView, public muse::Contextable, public muse::async::Asyncable
{
    Q_OBJECT

    muse::GlobalInject<muse::ui::IUiConfiguration> uiConfiguration;
    muse::GlobalInject<INotationSceneConfiguration> configuration;
    muse::GlobalInject<playback::IPlaybackConfiguration> playbackConfiguration;
    muse::ContextInject<muse::actions::IActionsDispatcher> dispatcher = { this };
    muse::ContextInject<muse::IInteractive> interactive = { this };
    muse::ContextInject<playback::IPlaybackController> playbackController = { this };
    muse::ContextInject<context::IGlobalContext> globalContext = { this };

public:
    enum class ItemType {
        TYPE_UNKNOWN = 0,
        TYPE_MEASURE,
        TYPE_META,
    };
    Q_ENUM(ItemType)

    Timeline(QSplitter* splitter, const muse::modularity::ContextPtr& iocCtx);

    bool handleEvent(QEvent* event);

    void updateGridView() { updateGrid(-1, -1); }
    void updateGridFromCmdState();
    void setNotation(INotationPtr notation);

    TRowLabels* labelsColumn() const;

private:
    friend class TRowLabels;

    enum class ViewState {
        NORMAL,
        LASSO,
        DRAG,
        ZOOM
    };

    ViewState state = ViewState::NORMAL;

    static constexpr int keyItemType = 15;

    int _gridWidth = 20;
    int _gridHeight = 20;
    int _maxZoom = 400; // px per measure: zoomed in far enough for the Video row's pictures to be precise
    int _minZoom = 5;

    //! NOTE: zoom by dragging vertically on the Measures row, anchored on the clicked point
    bool _measuresZoomPressed { false };
    int _zoomStartY { 0 };
    int _zoomStartGridWidth { 0 };
    int _zoomAnchorViewX { 0 };
    qreal _zoomAnchorMeasures { 0.0 };
    int _wheelZoomDelta { 0 };

    int _spacing = 5;

    TimelineTheme _lightTheme, _darkTheme;

    std::tuple<int, qreal, engraving::EngravingItem*, engraving::EngravingItem*, bool> _repeatInfo;
    std::tuple<QGraphicsItem*, int, QColor> _oldHoverInfo;

    std::map<engraving::BarLineType, QPixmap*> _barlines;
    bool _isBarline { false };

    QSplitter* _splitter { nullptr };
    TRowLabels* _rowNames { nullptr };

    INotationPtr m_notation;

    int gridRows = 0;
    int gridCols = 0;

    QGraphicsPathItem* nonVisiblePathItem = nullptr;
    QGraphicsPathItem* visiblePathItem = nullptr;
    QGraphicsPathItem* selectionItem = nullptr;

    QGraphicsRectItem* _selectionBox { nullptr };
    std::vector<std::pair<QGraphicsItem*, int> > _metaRows;

    QPainterPath _selectionPath;
    QRectF _oldSelectionRect;
    bool _mousePressed { false };
    QPoint _oldLoc;

    bool _collapsedMeta { false };

    std::vector<std::tuple<QString, void (Timeline::*)(engraving::Segment*, int*, int), bool> > _metas;
    void tempoMeta(engraving::Segment* seg, int* stagger, int pos);
    void timeMeta(engraving::Segment* seg, int* stagger, int pos);
    void measureMeta(engraving::Segment*, int*, int pos);
    void rehearsalMeta(engraving::Segment* seg, int* stagger, int pos);
    void keyMeta(engraving::Segment* seg, int* stagger, int pos);
    void barlineMeta(engraving::Segment* seg, int* stagger, int pos);
    void jumpMarkerMeta(engraving::Segment* seg, int* stagger, int pos);
    void hitPointMeta(engraving::Segment* seg, int* stagger, int pos);
    void timecodeMeta(engraving::Segment* seg, int* stagger, int pos);

    bool addMetaValue(int x, int pos, QString metaText, int row, engraving::ElementType elementType, engraving::EngravingItem* element,
                      engraving::Segment* seg, engraving::Measure* measure, QString tooltip = "");
    void setMetaData(QGraphicsItem* gi, int staff, engraving::ElementType et, engraving::Measure* m, bool full_measure,
                     engraving::EngravingItem* e, QGraphicsItem* pairItem = nullptr, engraving::Segment* seg = nullptr);
    unsigned getMetaRow(QString targetText);

    int _globalMeasureNumber { 0 };
    int _globalZValue        { 0 };

    // True if meta value was last clicked
    bool _metaValue = false;

    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent*) override;
    void wheelEvent(QWheelEvent* event) override;
    bool isOnMeasuresRow(const QPointF& scenePt) const;
    void zoomAround(int gridWidth, int anchorViewX, qreal anchorMeasures);
    void leaveEvent(QEvent*) override;
    void showEvent(QShowEvent*) override;
    void changeEvent(QEvent*) override;

    unsigned correctMetaRow(unsigned row);
    engraving::staff_idx_t correctStave(engraving::staff_idx_t stave);

    QList<engraving::Part*> getParts();

    QRectF getMeasureRect(int measureIndex, int row, int numMetas)
    {
        return QRectF(measureIndex * _gridWidth, _gridHeight * (row + numMetas) + 3, _gridWidth, _gridHeight);
    }

    void clearScene();

    void updateGrid(int startMeasure = -1, int endMeasure = -1);

    INotationInteractionPtr interaction() const;
    engraving::Score* score() const;
    project::IProjectVideoSettingsPtr videoSettings() const;
    project::IProjectAudioSettingsPtr audioSettings() const;

    //! NOTE: these take an instrument ROW (= part index, see getParts()), see the .cpp
    QColor trackColor(int row, const engraving::Fraction& tick) const;
    void editTrackColor(int row);

    using SoloMuteState = INotationSoloMuteState::SoloMuteState;
    engraving::InstrumentTrackId staffTrackId(int row) const;
    std::vector<const engraving::Part*> rowAudioParts(int row) const;
    std::vector<engraving::InstrumentTrackId> rowTrackIds(int row) const;
    QColor partTrackColor(const engraving::Part* part, const engraving::Fraction& tick) const;
    SoloMuteState trackSoloMuteState(int row) const;
    bool isTrackMutedBySolo(int row) const;
    bool isTrackMutedBySolo(int row, bool anythingSoloed) const;
    bool isAnythingSoloed() const;
    void toggleTrackMute(int row);
    void toggleTrackSolo(int row);
    QString formatVideoTimecode(int videoPositionMs) const;

private slots:
    void handleScroll(int value);

    void changeSelection(engraving::SelState);
    void mouseOver(QPointF pos);
    void swapMeta(unsigned row, bool switchUp);
    void requestInstrumentDialog();
    void toggleMetaRow();
    void updateTimelineTheme();

    void contextMenuEvent(QContextMenuEvent* event) override;

signals:
    void moved(QPointF);

private:
    int correctPart(engraving::staff_idx_t stave);

    void updateView();
    void drawSelection();
    void drawGrid(int globalRows, int globalCols, int startMeasure = 0, int endMeasure = -1);

    int nstaves() const;

    int getWidth() const;
    int getHeight() const;
    const TimelineTheme& activeTheme() const;

    void updateGridFull() { updateGrid(0, -1); }

    QColor colorBox(QGraphicsRectItem* item);

    std::vector<std::pair<QString, bool> > getLabels();

    unsigned nmetas() const;

    //! NOTE: whether the last meta row is the Measures row -- most of the meta row
    //! layout (collapse arrow, reorder range, measure box clicks) assumes it is, which
    //! no longer holds once it's hidden from the View menu.
    bool measuresRowVisible() const;
    //! NOTE: number of meta rows that can be reordered (every visible meta row but Measures)
    int nswappableMetas() const;

    static const std::string& metaRowId(void (Timeline::* func)(engraving::Segment*, int*, int));
    void initMetas();

    //! NOTE: playback cursor, see the .cpp
    void initPlaybackCursor();
    void onPlaybackStatusChanged();
    void updatePlaybackCursor();
    qreal playbackCursorX(int tick) const;
    void ensurePlaybackCursorVisible(qreal x);

    QGraphicsLineItem* m_playbackCursorItem = nullptr;
    QTimer m_playbackCursorTimer;
    QElapsedTimer m_elapsedTimer;
    muse::audio::secs_t m_lastPlaybackPosition = 0;
    qint64 m_lastPlaybackPositionUpdateTimeNs = 0;
    std::vector<int> m_measureStartTicks;

    std::vector<QColor> trackColorsSnapshot() const;
    void updateDefaultTrackColor();
    void scheduleLabelsUpdate();

    bool m_labelsUpdateScheduled = false;

    project::IProjectAudioSettingsPtr m_audioSettings;
    std::vector<QColor> m_trackColors;
    QColor m_defaultTrackColor;

    //! NOTE: the Video row, see the .cpp
    friend class TimelineVideoBand;
    bool hasVideo() const;
    void toggleVideoMute();
    void toggleVideoSolo();
    void chooseVideoFile();
    void onVideoSettingsChanged();
    int videoBandHeight() const;
    void updateVideoBand();
    void setVideoRowHeight(int height, bool save);
    void paintVideoRow(QPainter* painter, int height);
    double videoSecsAtX(qreal x, int offsetMs) const;
    void resizeEvent(QResizeEvent* event) override;

    TimelineVideoThumbnails* m_videoThumbnails = nullptr;
    QWidget* m_videoBand = nullptr;
    int m_videoRowHeight = 60;
    project::IProjectVideoSettingsPtr m_videoSettings;
    muse::io::path_t m_videoPath;

    void applyMetaRowsVisibility();

    bool collapsed() const { return _collapsedMeta; }
    void setCollapsed(bool st) { _collapsedMeta = st; }

    engraving::Staff* numToStaff(int staff);

    const std::vector<engraving::Part*>& timelineParts() const;

    struct RowsCache {
        const engraving::Score* score = nullptr;
        bool staveSharing = false;
        std::vector<engraving::Part*> sourceParts;
        std::vector<engraving::Part*> parts;
    };
    mutable RowsCache m_rowsCache;
    engraving::Part* rowPart(int row) const;
    QString partLabel(engraving::Part* part) const;
    int staffRow(engraving::staff_idx_t staffIdx) const;
    engraving::staff_idx_t rowFirstStaff(int row) const;
    engraving::staff_idx_t rowLastStaff(int row) const;
    void toggleShow(int staff);
    QString cursorIsOn(const QPoint& cursorPos);

    void seekSelection();
    engraving::EngravingItem* firstElementInRow(engraving::Measure* measure, int row) const;
    engraving::EngravingItem* firstElementInParts(engraving::Measure* measure, const std::vector<const engraving::Part*>& parts) const;
};
}
