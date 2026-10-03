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

#include "timeline.h"

#include <QApplication>
#include <QGraphicsTextItem>
#include <QGuiApplication>
#include <QPainter>
#include <QMenu>
#include <QMouseEvent>
#include <QScrollBar>
#include <QTextDocument>

#include <algorithm>
#include <cmath>

#include "translation.h"
#include "ui/view/iconcodes.h"

#include "engraving/dom/barline.h"
#include "engraving/dom/jump.h"
#include "engraving/dom/key.h"
#include "engraving/dom/keysig.h"
#include "engraving/dom/marker.h"
#include "engraving/dom/measure.h"
#include "engraving/dom/measurebase.h"
#include "engraving/dom/mscore.h"
#include "engraving/dom/page.h"
#include "engraving/dom/part.h"
#include "engraving/dom/rehearsalmark.h"
#include "engraving/dom/score.h"
#include "engraving/dom/sharedpart.h"
#include "engraving/dom/staff.h"
#include "engraving/dom/system.h"
#include "engraving/dom/tempotext.h"
#include "engraving/dom/timesig.h"
#include "engraving/types/typesconv.h"
#include "project/inotationproject.h"

#include "notation/inotationelements.h" // IWYU pragma: keep
#include "notation/imasternotation.h"
#include "notation/inotationinteraction.h"
#include "notation/inotationplayback.h"
#include "notation/inotationselection.h"
#include "notation/inotationundostack.h" // IWYU pragma: keep

#include "log.h"

using namespace mu::notation;
using namespace mu::engraving;

using namespace Qt::StringLiterals;

//---------------------------------------------------------
//   TRowLabels
//---------------------------------------------------------

TRowLabels::TRowLabels(QSplitter* splitter, Timeline* time)
    : QGraphicsView(splitter)
{
    TRACEFUNC;

    setFocusPolicy(Qt::NoFocus);
    setObjectName("TRowLabels");

    _splitter = splitter;
    _timeline = time;
    setScene(new QGraphicsScene);
    scene()->setBackgroundBrush(time->activeTheme().backgroundColor);
    setSceneRect(0, 0, 50, time->height());

    setMinimumWidth(0);

    QSplitter* split = _splitter;
    QList<int> sizes;
    // TODO: Replace 70 with hard coded value
    sizes << 70 << 10000;
    split->setSizes(sizes);

    setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    setContentsMargins(0, 0, 0, 0);
    setAlignment(Qt::Alignment((Qt::AlignLeft | Qt::AlignTop)));

    connect(verticalScrollBar(), &QScrollBar::valueChanged, time->verticalScrollBar(), &QScrollBar::setValue);
    connect(verticalScrollBar(), &QScrollBar::valueChanged, this, &TRowLabels::restrictScroll);
    connect(this, &TRowLabels::moved, time, &Timeline::mouseOver);

    static const char* udArrow[] = {
        "10 18 2 1",
        "# c #000000",
        ". c #d3d3d3",
        "..........",
        "..........",
        "....##....",
        "...####...",
        "..##..##..",
        "..#....#..",
        "..........",
        "..........",
        "..........",
        "..........",
        "..........",
        "..........",
        "..#....#..",
        "..##..##..",
        "...####...",
        "....##....",
        "..........",
        ".........."
    };

    static const char* uArrow[] = {
        "10 18 2 1",
        "# c #000000",
        ". c #d3d3d3",
        "..........",
        "..........",
        "....##....",
        "...####...",
        "..##..##..",
        "..#....#..",
        "..........",
        "..........",
        "..........",
        "..........",
        "..........",
        "..........",
        "..........",
        "..........",
        "..........",
        "..........",
        "..........",
        ".........."
    };

    static const char* dArrow[] = {
        "10 18 2 1",
        "# c #000000",
        ". c #d3d3d3",
        "..........",
        "..........",
        "..........",
        "..........",
        "..........",
        "..........",
        "..........",
        "..........",
        "..........",
        "..........",
        "..........",
        "..........",
        "..#....#..",
        "..##..##..",
        "...####...",
        "....##....",
        "..........",
        ".........."
    };

    static const char* cuArrow[] = {
        "9 18 2 1",
        "# c #000000",
        ". c #d3d3d3",
        ".........",
        ".........",
        ".........",
        ".........",
        "....#....",
        "...###...",
        "..#.#.#..",
        ".#..#..#.",
        "....#....",
        "....#....",
        "....#....",
        "....#....",
        ".........",
        ".........",
        ".........",
        ".........",
        ".........",
        "........."
    };

    static const char* cdArrow[] = {
        "9 18 2 1",
        "# c #000000",
        ". c #d3d3d3",
        ".........",
        ".........",
        ".........",
        ".........",
        ".........",
        "....#....",
        "....#....",
        "....#....",
        "....#....",
        "....#....",
        ".#..#..#.",
        "..#.#.#..",
        "...###...",
        "....#....",
        ".........",
        ".........",
        ".........",
        "........."
    };

    static const char* openEye[] = {
        "11 18 2 1",
        "# c #000000",
        ". c #d3d3d3",
        "...........",
        "...........",
        "...........",
        "...........",
        "...####....",
        ".##....##..",
        ".#......#..",
        "#........#.",
        "#...##...#.",
        "#...##...#.",
        "#........#.",
        ".#......#..",
        ".##....##..",
        "...####....",
        "...........",
        "...........",
        "...........",
        "..........."
    };

    static const char* closedEye[] = {
        "11 18 2 1",
        "# c #000000",
        ". c #d3d3d3",
        "...........",
        "...........",
        "...........",
        "...........",
        "...........",
        "...........",
        "...........",
        "..######...",
        "##..##..##.",
        "##..##..##.",
        "..######...",
        "...........",
        "...........",
        "...........",
        "...........",
        "...........",
        "...........",
        "..........."
    };

    _mouseoverMap[MouseOverValue::COLLAPSE_DOWN_ARROW] = new QPixmap(cdArrow);
    _mouseoverMap[MouseOverValue::COLLAPSE_UP_ARROW] = new QPixmap(cuArrow);
    _mouseoverMap[MouseOverValue::MOVE_DOWN_ARROW] = new QPixmap(dArrow);
    _mouseoverMap[MouseOverValue::MOVE_UP_DOWN_ARROW] = new QPixmap(udArrow);
    _mouseoverMap[MouseOverValue::MOVE_UP_ARROW] = new QPixmap(uArrow);
    _mouseoverMap[MouseOverValue::OPEN_EYE] = new QPixmap(openEye);
    _mouseoverMap[MouseOverValue::CLOSED_EYE] = new QPixmap(closedEye);

    std::tuple<QGraphicsPixmapItem*, MouseOverValue, unsigned> tmp(nullptr, MouseOverValue::NONE, -1);
    _oldItemInfo = tmp;

    connect(this, &TRowLabels::requestContextMenu, _timeline, &Timeline::contextMenuEvent);
}

bool TRowLabels::handleEvent(QEvent* e)
{
    return QWidget::event(e);
}

//---------------------------------------------------------
//   TRowLabels::restrictScroll
//---------------------------------------------------------

void TRowLabels::restrictScroll(int value)
{
    TRACEFUNC;

    if (value > _timeline->verticalScrollBar()->maximum()) {
        verticalScrollBar()->setValue(_timeline->verticalScrollBar()->maximum());
    }
    for (std::vector<std::pair<QGraphicsItem*, int> >::iterator it = _metaLabels.begin();
         it != _metaLabels.end(); ++it) {
        std::pair<QGraphicsItem*, int> pairGraphicInt = *it;

        QGraphicsItem* graphicsItem = pairGraphicInt.first;

        QGraphicsRectItem* graphicsRectItem = qgraphicsitem_cast<QGraphicsRectItem*>(graphicsItem);
        QGraphicsLineItem* graphicsLineItem = qgraphicsitem_cast<QGraphicsLineItem*>(graphicsItem);
        QGraphicsPixmapItem* graphicsPixmapItem = qgraphicsitem_cast<QGraphicsPixmapItem*>(graphicsItem);
        int y = pairGraphicInt.second * 20;
        int scrollbarValue = verticalScrollBar()->value();

        if (graphicsRectItem) {
            QRectF rectf = graphicsRectItem->rect();
            rectf.setY(qreal(scrollbarValue + y));
            rectf.setHeight(20);
            graphicsRectItem->setRect(rectf);
        } else if (graphicsLineItem) {
            QLineF linef = graphicsLineItem->line();
            linef.setLine(linef.x1(), y + scrollbarValue + 1, linef.x2(), y + scrollbarValue + 1);
            graphicsLineItem->setLine(linef);
        } else if (graphicsPixmapItem) {
            graphicsPixmapItem->setY(qreal(scrollbarValue + y + 1));
        } else {
            graphicsItem->setY(qreal(scrollbarValue + y));
        }
    }
    viewport()->update();
}

//---------------------------------------------------------
//   TRowLabels::updateLabels
//---------------------------------------------------------

static constexpr int TRACK_COLOR_STRIP_WIDTH = 4;
static constexpr int TRACK_COLOR_STRIP_HIT_WIDTH = TRACK_COLOR_STRIP_WIDTH + 3;
static constexpr int TRACK_COLOR_STRIP_KEY = 3;

//! NOTE: the Mute / Solo / show-in-score buttons at the end of each instrument row,
//! all the same width
static constexpr int TRACK_BUTTON_KEY = 4;
static constexpr int TRACK_BUTTON_WIDTH = 18;
static constexpr int TRACK_BUTTON_SPACING = 2;
static constexpr int TRACK_BUTTONS_WIDTH = 3 * TRACK_BUTTON_WIDTH + 3 * TRACK_BUTTON_SPACING;

enum class TrackButton {
    None = 0,
    Mute,
    Solo,
    Visibility,
};

static bool isTrackColorStrip(const QGraphicsItem* item)
{
    return item && item->data(TRACK_COLOR_STRIP_KEY).toBool();
}

static TrackButton trackButton(const QGraphicsItem* item)
{
    return item ? static_cast<TrackButton>(item->data(TRACK_BUTTON_KEY).toInt()) : TrackButton::None;
}

static bool isClickableLabelItem(const QGraphicsItem* item)
{
    return isTrackColorStrip(item) || trackButton(item) != TrackButton::None;
}

namespace mu::notation {
//! NOTE: the Video row, above the meta rows of both the Timeline (pictures, see Timeline::paintVideoRow())
//! and the row labels column (its label): it isn't one of the meta rows, all of the same height, but sits
//! in their views' top margin. Its bottom edge is a handle to drag to change its height.
class TimelineVideoBand : public QWidget
{
public:
    enum class Kind {
        Label,
        Pictures
    };

    TimelineVideoBand(Timeline* timeline, Kind kind, QWidget* parent)
        : QWidget(parent), m_timeline(timeline), m_kind(kind)
    {
        setMouseTracking(true);
        hide();
    }

protected:
    void paintEvent(QPaintEvent*) override
    {
        QPainter painter(this);
        const TimelineTheme& theme = m_timeline->activeTheme();

        if (m_kind == Kind::Label) {
            painter.fillRect(rect(), theme.labelsColor3);

            // On the first line, like the other rows: the title, then the buttons on the right
            const int titleWidth = static_cast<int>(buttonRect(0).left()) - 4 - 2;
            QFontMetrics metrics(QApplication::font());
            painter.setPen(theme.labelsColor1);
            painter.setFont(QApplication::font());
            painter.drawText(QRect(4, 0, titleWidth, FIRST_LINE_HEIGHT), Qt::AlignLeft | Qt::AlignVCenter,
                             metrics.elidedText(muse::qtrc("notation/timeline", "Video"), Qt::ElideRight, titleWidth));
            paintButtons(painter, theme);

            // The handle's grip, centered at the bottom
            QColor gripColor = theme.labelsColor1;
            gripColor.setAlpha(140);
            painter.setPen(gripColor);
            const int centerX = width() / 2;
            for (int y = height() - 5; y < height() - 1; y += 2) {
                painter.drawLine(centerX - 8, y, centerX + 8, y);
            }
        } else {
            painter.fillRect(rect(), theme.gridColor2);
            m_timeline->paintVideoRow(&painter, height());
        }

        painter.setPen(QPen(theme.gridColor1, 2));
        painter.drawLine(0, height() - 1, width(), height() - 1);
    }

    void mousePressEvent(QMouseEvent* event) override
    {
        if (event->button() != Qt::LeftButton) {
            return;
        }

        if (m_kind == Kind::Label) {
            if (buttonRect(0).contains(event->position())) {
                m_timeline->toggleVideoMute();
                return;
            }
            if (buttonRect(1).contains(event->position())) {
                m_timeline->toggleVideoSolo();
                return;
            }
            if (buttonRect(2).contains(event->position())) {
                m_timeline->chooseVideoFile();
                return;
            }
        }

        if (!isOnHandle(event->position())) {
            return;
        }

        m_resizing = true;
        m_startY = event->globalPosition().y();
        m_startHeight = height();
    }

    void mouseMoveEvent(QMouseEvent* event) override
    {
        //! NOTE: the release can be lost on the way through the QML adapter (see Timeline::mouseMoveEvent())
        if (m_resizing && !event->buttons().testFlag(Qt::LeftButton)) {
            mouseReleaseEvent(event);
        }

        if (m_resizing) {
            m_timeline->setVideoRowHeight(m_startHeight + qRound(event->globalPosition().y() - m_startY), false);
            return;
        }

        setCursor(isOnHandle(event->position()) ? Qt::SizeVerCursor : Qt::ArrowCursor);
    }

    void mouseReleaseEvent(QMouseEvent*) override
    {
        if (!m_resizing) {
            return;
        }

        m_resizing = false;
        m_timeline->setVideoRowHeight(height(), true);
    }

private:
    static constexpr int HANDLE_HEIGHT = 6;
    static constexpr int FIRST_LINE_HEIGHT = 20; // a regular row's

    //! NOTE: Mute, Solo, Load video: where the instrument rows have Mute, Solo, Visibility
    QRectF buttonRect(int index) const
    {
        const qreal x = width() - TRACK_BUTTONS_WIDTH + index * (TRACK_BUTTON_WIDTH + TRACK_BUTTON_SPACING);
        return QRectF(x, 2, TRACK_BUTTON_WIDTH, FIRST_LINE_HEIGHT - 4);
    }

    void paintButtons(QPainter& painter, const TimelineTheme& theme)
    {
        const bool hasVideo = m_timeline->hasVideo();
        const mu::project::VideoAttachmentSettings attachment
            = hasVideo ? m_timeline->videoSettings()->attachment() : mu::project::VideoAttachmentSettings();
        const bool forceMuted = hasVideo && m_timeline->playbackController()->isVideoForceMuted();

        QFont letterFont = QApplication::font();
        letterFont.setPixelSize(10);
        letterFont.setBold(true);

        QFont iconFont(QString::fromStdString(m_timeline->uiConfiguration()->iconsFontFamily()));
        iconFont.setPixelSize(14);

        auto paintButton = [&](int index, const QString& text, const QFont& font, bool checked, bool dimmed) {
            const QRectF rect = buttonRect(index);
            painter.setOpacity(dimmed ? 0.5 : 1.0);
            painter.setPen(theme.labelsColor2);
            painter.setBrush(checked ? m_timeline->m_defaultTrackColor : theme.labelsColor3.lighter(115));
            painter.drawRect(rect);
            painter.setPen(checked ? QColor(Qt::white) : theme.labelsColor1);
            painter.setFont(font);
            painter.drawText(rect, Qt::AlignCenter, text);
            painter.setOpacity(1.0);
        };

        //! NOTE: same as the instrument rows' (see TRowLabels::addTrackButtons())
        paintButton(0, muse::qtrc("notation/timeline", "M", "mute button"), letterFont,
                    attachment.muted || forceMuted, !hasVideo || (forceMuted && !attachment.muted));
        paintButton(1, muse::qtrc("notation/timeline", "S", "solo button"), letterFont, attachment.solo, !hasVideo);
        paintButton(2, QChar(static_cast<char16_t>(muse::ui::IconCode::Code::OPEN_FILE)), iconFont, false, false);
    }

    bool isOnHandle(const QPointF& pos) const
    {
        return pos.y() >= height() - HANDLE_HEIGHT;
    }

    Timeline* m_timeline = nullptr;
    Kind m_kind = Kind::Pictures;
    bool m_resizing = false;
    qreal m_startY = 0.0;
    int m_startHeight = 0;
};
}

void TRowLabels::addTrackButtons(int row, unsigned labelRow, int ypos, int height, bool anythingSoloed)
{
    const TimelineTheme& theme = _timeline->activeTheme();
    const QColor accentColor = _timeline->m_defaultTrackColor;

    const Timeline::SoloMuteState state = _timeline->trackSoloMuteState(row);
    const bool hasTrack = _timeline->staffTrackId(row).isValid();
    const bool forceMuted = _timeline->isTrackMutedBySolo(row, anythingSoloed);
    //! NOTE: the eye shows/hides the whole part (see Timeline::toggleShow())
    const QList<Part*> parts = _timeline->getParts();
    const bool staffShown = row >= 0 && row < parts.size() && parts.at(row)->show();

    QFont letterFont = QApplication::font();
    letterFont.setPixelSize(10);
    letterFont.setBold(true);

    QFont iconFont(QString::fromStdString(_timeline->uiConfiguration()->iconsFontFamily()));
    iconFont.setPixelSize(14);

    auto addButton = [&](TrackButton type, int index, const QString& text, const QFont& font, bool checked, bool dimmed,
                         const QString& tooltip) {
        const qreal x = width() - TRACK_BUTTONS_WIDTH + index * (TRACK_BUTTON_WIDTH + TRACK_BUTTON_SPACING);
        const QRectF rect(x, ypos + 2, TRACK_BUTTON_WIDTH, height - 4);

        QGraphicsRectItem* button = new QGraphicsRectItem(rect);
        button->setPen(QPen(theme.labelsColor2));
        button->setBrush(QBrush(checked ? accentColor : theme.labelsColor3.lighter(115)));
        button->setOpacity(dimmed ? 0.5 : 1.0);
        //! NOTE: below the meta rows' labels (z 1-2), which stay pinned on top while the
        //! instrument rows scroll underneath them
        button->setZValue(0.5);
        button->setToolTip(tooltip);

        QGraphicsSimpleTextItem* label = new QGraphicsSimpleTextItem(text, button);
        label->setFont(font);
        label->setBrush(QBrush(checked ? QColor(Qt::white) : theme.labelsColor1));
        const QRectF textRect = label->boundingRect();
        label->setPos(rect.center() - textRect.center());

        for (QGraphicsItem* item : { static_cast<QGraphicsItem*>(button), static_cast<QGraphicsItem*>(label) }) {
            item->setData(0, QVariant::fromValue<bool>(false));
            item->setData(1, QVariant::fromValue<MouseOverValue>(MouseOverValue::NONE));
            item->setData(2, QVariant::fromValue<unsigned>(labelRow));
            item->setData(TRACK_BUTTON_KEY, QVariant::fromValue<int>(static_cast<int>(type)));
        }

        scene()->addItem(button);
    };

    //! NOTE: like the Mixer's Mute button, shown checked but dimmed (and not clickable)
    //! while the track is only muted because another track/group bus is soloed. Other
    //! reasons the engine force-mutes a track (e.g. playing back a range selection only
    //! plays its staves) aren't a mute the user set, so they're not shown here.
    addButton(TrackButton::Mute, 0, muse::qtrc("notation/timeline", "M", "mute button"), letterFont,
              state.mute || forceMuted, !hasTrack || (forceMuted && !state.mute), muse::qtrc("notation/timeline", "Mute"));
    addButton(TrackButton::Solo, 1, muse::qtrc("notation/timeline", "S", "solo button"), letterFont,
              state.solo, !hasTrack, muse::qtrc("notation/timeline", "Solo"));

    const muse::ui::IconCode::Code eyeIcon = staffShown ? muse::ui::IconCode::Code::EYE_OPEN : muse::ui::IconCode::Code::EYE_CLOSED;
    addButton(TrackButton::Visibility, 2, QChar(static_cast<char16_t>(eyeIcon)), iconFont, false, false,
              staffShown ? muse::qtrc("notation/timeline", "Hide instrument in score")
              : muse::qtrc("notation/timeline", "Show instrument in score"));
}

void TRowLabels::updateLabels(std::vector<std::pair<QString, bool> > labels, int height)
{
    TRACEFUNC;

    scene()->clear();
    _metaLabels.clear();
    if (labels.empty()) {
        return;
    }

    unsigned numMetas = _timeline->nmetas();
    const bool measuresVisible = _timeline->measuresRowVisible();
    const int numSwappable = _timeline->nswappableMetas();
    const bool anythingSoloed = _timeline->score() && _timeline->isAnythingSoloed();
    int maxWidth = -1;
    int measureWidth = 0;
    for (unsigned row = 0; row < labels.size(); row++) {
        // Draw instrument name rectangle
        int ypos = (row < numMetas) ? row * height + verticalScrollBar()->value() : row * height + 3;
        QGraphicsRectItem* graphicsRectItem = new QGraphicsRectItem(0, ypos, width(), height);
        QGraphicsTextItem* graphicsTextItem = new QGraphicsTextItem(labels[row].first);

        if (row == numMetas - 1) {
            measureWidth = graphicsTextItem->boundingRect().width();
        }
        if (row >= numMetas) {
            maxWidth = std::max(maxWidth, TRACK_COLOR_STRIP_WIDTH + int(graphicsTextItem->boundingRect().width()) + TRACK_BUTTONS_WIDTH);
        } else {
            maxWidth = std::max(maxWidth, int(graphicsTextItem->boundingRect().width()));
        }

        //! NOTE: instrument rows start with a thin strip in the track's Mixer color
        const bool isInstrumentRow = row >= numMetas;
        const int textX = isInstrumentRow ? TRACK_COLOR_STRIP_WIDTH : 0;

        QFontMetrics f(QApplication::font());
        const int textRightReserve = isInstrumentRow ? TRACK_BUTTONS_WIDTH : 0;
        QString partName = f.elidedText(labels[row].first, Qt::ElideRight, width() - textX - textRightReserve);
        graphicsTextItem->setPlainText(partName);
        graphicsTextItem->setX(textX);
        graphicsTextItem->setY(ypos);
        if (labels[row].second) {
            graphicsTextItem->setDefaultTextColor(_timeline->activeTheme().labelsColor1);
        } else {
            graphicsTextItem->setDefaultTextColor(_timeline->activeTheme().labelsColor2);
        }
        graphicsRectItem->setPen(QPen(_timeline->activeTheme().labelsColor2));
        graphicsRectItem->setBrush(QBrush(_timeline->activeTheme().labelsColor3));
        graphicsTextItem->setZValue(-1);
        graphicsRectItem->setZValue(-1);

        graphicsRectItem->setData(0, QVariant::fromValue<bool>(false));
        graphicsTextItem->setData(0, QVariant::fromValue<bool>(false));

        MouseOverValue mouseOverArrow = MouseOverValue::NONE;
        const int metaRow = static_cast<int>(row);
        if (measuresVisible && numMetas - 1 == row && (numMetas > 2 || _timeline->collapsed())) {
            // Measures meta
            if (_timeline->collapsed()) {
                mouseOverArrow = MouseOverValue::COLLAPSE_DOWN_ARROW;
            } else {
                mouseOverArrow = MouseOverValue::COLLAPSE_UP_ARROW;
            }
        } else if (metaRow < numSwappable) {
            if (metaRow != 0 && metaRow + 1 <= numSwappable - 1) {
                mouseOverArrow = MouseOverValue::MOVE_UP_DOWN_ARROW;
            } else if (metaRow == 0 && metaRow + 1 < numSwappable) {
                mouseOverArrow = MouseOverValue::MOVE_DOWN_ARROW;
            } else if (metaRow == numSwappable - 1 && metaRow != 0) {
                mouseOverArrow = MouseOverValue::MOVE_UP_ARROW;
            }
        }
        graphicsTextItem->setData(1, QVariant::fromValue<MouseOverValue>(mouseOverArrow));
        graphicsRectItem->setData(1, QVariant::fromValue<MouseOverValue>(mouseOverArrow));

        graphicsTextItem->setData(2, QVariant::fromValue<unsigned>(row));
        graphicsRectItem->setData(2, QVariant::fromValue<unsigned>(row));

        scene()->addItem(graphicsRectItem);
        scene()->addItem(graphicsTextItem);

        if (isInstrumentRow && _timeline->score()) {
            QGraphicsRectItem* colorStrip = new QGraphicsRectItem(0, ypos, TRACK_COLOR_STRIP_WIDTH, height);
            colorStrip->setPen(Qt::NoPen);
            colorStrip->setBrush(QBrush(_timeline->trackColor(row - numMetas, Fraction(0, 1))));
            colorStrip->setZValue(0);
            colorStrip->setData(0, QVariant::fromValue<bool>(false));
            colorStrip->setData(1, QVariant::fromValue<MouseOverValue>(mouseOverArrow));
            colorStrip->setData(2, QVariant::fromValue<unsigned>(row));
            colorStrip->setData(TRACK_COLOR_STRIP_KEY, QVariant::fromValue<bool>(true));
            colorStrip->setToolTip(muse::qtrc("notation/timeline", "Edit color…"));
            scene()->addItem(colorStrip);

            //! NOTE: the strip itself is only a few pixels wide - this transparent area over
            //! it (and the start of the name) is what actually catches the clicks, so they
            //! don't have to land exactly on it
            QGraphicsRectItem* colorStripHitArea = new QGraphicsRectItem(0, ypos, TRACK_COLOR_STRIP_HIT_WIDTH, height);
            colorStripHitArea->setPen(Qt::NoPen);
            colorStripHitArea->setBrush(Qt::transparent);
            colorStripHitArea->setZValue(0.25);
            colorStripHitArea->setData(0, QVariant::fromValue<bool>(false));
            colorStripHitArea->setData(1, QVariant::fromValue<MouseOverValue>(mouseOverArrow));
            colorStripHitArea->setData(2, QVariant::fromValue<unsigned>(row));
            colorStripHitArea->setData(TRACK_COLOR_STRIP_KEY, QVariant::fromValue<bool>(true));
            colorStripHitArea->setToolTip(colorStrip->toolTip());
            scene()->addItem(colorStripHitArea);

            addTrackButtons(static_cast<int>(row - numMetas), row, ypos, height, anythingSoloed);
        }

        if (row < numMetas) {
            std::pair<QGraphicsItem*, int> p1 = std::make_pair(graphicsRectItem, row);
            std::pair<QGraphicsItem*, int> p2 = std::make_pair(graphicsTextItem, row);
            _metaLabels.push_back(p1);
            _metaLabels.push_back(p2);
            graphicsRectItem->setZValue(1);
            graphicsTextItem->setZValue(2);
        }
    }
    QGraphicsLineItem* graphicsLineItem = new QGraphicsLineItem(0,
                                                                height * numMetas + verticalScrollBar()->value() + 1,
                                                                std::max(maxWidth + 20, 70),
                                                                height * numMetas + verticalScrollBar()->value() + 1);
    graphicsLineItem->setPen(QPen(QColor(150, 150, 150), 4));
    graphicsLineItem->setZValue(0);
    graphicsLineItem->setData(0, QVariant::fromValue<bool>(false));
    scene()->addItem(graphicsLineItem);

    std::pair<QGraphicsItem*, int> graphicsLineItemPair = std::make_pair(graphicsLineItem, numMetas);
    _metaLabels.push_back(graphicsLineItemPair);

    setSceneRect(0, 0, maxWidth, _timeline->getHeight() + _timeline->horizontalScrollBar()->height());

    std::tuple<QGraphicsPixmapItem*, MouseOverValue, unsigned> tmp(nullptr, MouseOverValue::NONE, -1);
    _oldItemInfo = tmp;

    //! NOTE: at least wide enough for the instrument rows' color strip + buttons
    //! with a few characters of name left between them
    setMinimumWidth(std::max(measureWidth + 9, TRACK_COLOR_STRIP_WIDTH + 40 + TRACK_BUTTONS_WIDTH));
    setMaximumWidth(std::max(maxWidth + 20, 70));
    mouseOver(mapToScene(viewport()->mapFromGlobal(QCursor::pos())));
}

//---------------------------------------------------------
//   TRowLabels::resizeEvent
//---------------------------------------------------------

void TRowLabels::resizeEvent(QResizeEvent*)
{
    std::vector<std::pair<QString, bool> > labels = _timeline->getLabels();
    updateLabels(labels, 20);

    //! NOTE: not isVisible(): the Timeline is rendered offscreen (see TimelineView), never shown
    if (m_videoLabel && !m_videoLabel->isHidden()) {
        m_videoLabel->setGeometry(viewport()->x(), 0, viewport()->width(), m_videoLabel->height());
    }
}

void TRowLabels::setVideoBand(int height)
{
    if (!m_videoLabel) {
        m_videoLabel = new TimelineVideoBand(_timeline, TimelineVideoBand::Kind::Label, this);
    }

    setViewportMargins(0, height, 0, 0);
    m_videoLabel->setVisible(height > 0);
    m_videoLabel->setGeometry(viewport()->x(), 0, viewport()->width(), height);
    m_videoLabel->raise();
    m_videoLabel->update();
}

//---------------------------------------------------------
//   TRowLabels::mousePressEvent
//---------------------------------------------------------

void TRowLabels::mousePressEvent(QMouseEvent* event)
{
    if (event->button() == Qt::RightButton) {
        return;
    }

    TRACEFUNC;

    QPointF scenePt = mapToScene(event->pos());
    unsigned numMetas = _timeline->nmetas();

    // Check if mouse position in scene is on the last meta
    QPointF measureMetaTl = QPointF(0, (static_cast<int>(numMetas) - 1) * 20 + verticalScrollBar()->value());
    QPointF measureMetaBr = QPointF(width(), numMetas * 20 + verticalScrollBar()->value());
    if (_timeline->measuresRowVisible() && QRectF(measureMetaTl, measureMetaBr).contains(scenePt)
        && (numMetas > 2 || _timeline->collapsed())) {
        if (std::get<0>(_oldItemInfo)) {
            std::pair<QGraphicsItem*, int> p = std::make_pair(std::get<0>(_oldItemInfo), std::get<2>(_oldItemInfo));
            std::vector<std::pair<QGraphicsItem*, int> >::iterator it = std::find(_metaLabels.begin(), _metaLabels.end(), p);
            if (it != _metaLabels.end()) {
                _metaLabels.erase(it);
            }
            scene()->removeItem(std::get<0>(_oldItemInfo));
        }
        std::tuple<QGraphicsPixmapItem*, MouseOverValue, unsigned> tmp(nullptr, MouseOverValue::NONE, -1);
        _oldItemInfo = tmp;
        mouseOver(mapToScene(viewport()->mapFromGlobal(QCursor::pos())));

        _timeline->setCollapsed(!_timeline->collapsed());
        _timeline->updateGridView();
    } else {
        if (QGraphicsItem* graphicsItem = scene()->itemAt(scenePt, transform()); isClickableLabelItem(graphicsItem)) {
            const int row = static_cast<int>(graphicsItem->data(2).value<unsigned>()) - static_cast<int>(numMetas);

            //! NOTE: each of these relayouts the labels (deleting graphicsItem), so nothing
            //! may touch it afterwards
            switch (trackButton(graphicsItem)) {
            case TrackButton::Mute:
                _timeline->toggleTrackMute(row);
                break;
            case TrackButton::Solo:
                _timeline->toggleTrackSolo(row);
                break;
            case TrackButton::Visibility:
                _timeline->toggleShow(row);
                break;
            case TrackButton::None:
                _timeline->editTrackColor(row);
                break;
            }
            return;
        }

        // Check if pixmap was selected
        if (QGraphicsItem* graphicsItem = scene()->itemAt(scenePt, transform())) {
            QGraphicsPixmapItem* graphicsPixmapItem = qgraphicsitem_cast<QGraphicsPixmapItem*>(graphicsItem);
            if (graphicsPixmapItem) {
                unsigned row = graphicsPixmapItem->data(2).value<unsigned>();
                if (static_cast<int>(row) < _timeline->nswappableMetas()) {
                    // Find mid point between up and down arrow
                    qreal midPoint = graphicsPixmapItem->boundingRect().height() / 2 + graphicsPixmapItem->scenePos().y();
                    if (scenePt.y() > midPoint) {
                        emit swapMeta(row, false);
                    } else {
                        emit swapMeta(row, true);
                    }
                } else if (row >= numMetas) {
                    _timeline->toggleShow(row - numMetas);
                }
            } else {
                _dragging = true;
                setCursor(Qt::SizeAllCursor);
                _oldLoc = QPoint(int(scenePt.x()), int(scenePt.y()));
            }
        } else {
            _dragging = true;
            setCursor(Qt::SizeAllCursor);
            _oldLoc = QPoint(int(scenePt.x()), int(scenePt.y()));
        }
    }
}

//---------------------------------------------------------
//   TRowLabels::mouseMoveEvent
//---------------------------------------------------------

void TRowLabels::mouseMoveEvent(QMouseEvent* event)
{
    QPointF scenePt = mapToScene(event->pos());
    if (_dragging) {
        setCursor(Qt::SizeAllCursor);
        int yOffset = int(_oldLoc.y()) - int(scenePt.y());
        verticalScrollBar()->setValue(verticalScrollBar()->value() + yOffset);
    } else {
        mouseOver(scenePt);
    }
    emit moved(QPointF(-1, -1));
}

//---------------------------------------------------------
//   TRowLabels::mouseReleaseEvent
//---------------------------------------------------------

void TRowLabels::mouseReleaseEvent(QMouseEvent* event)
{
    if (QGraphicsItem* graphicsItem = scene()->itemAt(mapToScene(event->pos()), transform())) {
        QGraphicsPixmapItem* graphicsPixmapItem = qgraphicsitem_cast<QGraphicsPixmapItem*>(graphicsItem);
        if (graphicsPixmapItem || isClickableLabelItem(graphicsItem)) {
            setCursor(Qt::PointingHandCursor);
        } else {
            setCursor(Qt::ArrowCursor);
        }
    } else {
        setCursor(Qt::ArrowCursor);
    }
    _dragging = false;
}

//---------------------------------------------------------
//   TRowLabels::leaveEvent
//---------------------------------------------------------

void TRowLabels::leaveEvent(QEvent*)
{
    if (!viewport()->rect().contains(viewport()->mapFromGlobal(QCursor::pos()))) {
        mouseOver(mapToScene(viewport()->mapFromGlobal(QCursor::pos())));
    }
}

//---------------------------------------------------------
//   TRowLabels::contextMenuEvent
//---------------------------------------------------------

void TRowLabels::contextMenuEvent(QContextMenuEvent* event)
{
    emit requestContextMenu(event);
}

//---------------------------------------------------------
//   TRowLabels::mouseOver
//---------------------------------------------------------

void TRowLabels::mouseOver(QPointF scenePt)
{
    TRACEFUNC;

    // Handle drawing of arrows
    if (QGraphicsItem* graphicsItem = scene()->itemAt(scenePt, transform())) {
        QGraphicsPixmapItem* graphicsPixmapItem = qgraphicsitem_cast<QGraphicsPixmapItem*>(graphicsItem);
        if (graphicsPixmapItem || isClickableLabelItem(graphicsItem)) {
            setCursor(Qt::PointingHandCursor);
            return;
        }

        MouseOverValue mouseOverArrow = graphicsItem->data(1).value<MouseOverValue>();
        if (mouseOverArrow != MouseOverValue::NONE) {
            QPixmap* pixmapArrow = _mouseoverMap[mouseOverArrow];
            QGraphicsPixmapItem* graphicsPixmapItemArrow = new QGraphicsPixmapItem(*pixmapArrow);
            unsigned row = graphicsItem->data(2).value<unsigned>();

            QString tooltip;
            switch (mouseOverArrow) {
            case MouseOverValue::COLLAPSE_DOWN_ARROW:
                tooltip = muse::qtrc("notation/timeline", "Expand meta rows");
                break;
            case MouseOverValue::COLLAPSE_UP_ARROW:
                tooltip = muse::qtrc("notation/timeline", "Collapse meta rows");
                break;
            case MouseOverValue::MOVE_DOWN_ARROW:
                tooltip = muse::qtrc("notation/timeline", "Move meta row down one");
                break;
            case MouseOverValue::MOVE_UP_ARROW:
                tooltip = muse::qtrc("notation/timeline", "Move meta row up one");
                break;
            case MouseOverValue::MOVE_UP_DOWN_ARROW:
                tooltip = muse::qtrc("notation/timeline", "Move meta row up/down one");
                break;
            case MouseOverValue::OPEN_EYE:
                tooltip = muse::qtrc("notation/timeline", "Hide instrument in score");
                break;
            case MouseOverValue::CLOSED_EYE:
                tooltip = muse::qtrc("notation/timeline", "Show instrument in score");
                break;
            default:
                tooltip = "";
                break;
            }

            graphicsPixmapItemArrow->setToolTip(tooltip);
            if (mouseOverArrow == MouseOverValue::OPEN_EYE || mouseOverArrow == MouseOverValue::CLOSED_EYE) {
                graphicsPixmapItemArrow->setData(0, QVariant::fromValue<bool>(false));
            } else {
                graphicsPixmapItemArrow->setData(0, QVariant::fromValue<bool>(true));
            }
            graphicsPixmapItemArrow->setData(1, QVariant::fromValue<MouseOverValue>(mouseOverArrow));
            graphicsPixmapItemArrow->setData(2, QVariant::fromValue<unsigned>(row));

            // Draw arrow at correct location
            if (row < _timeline->nmetas()) {
                graphicsPixmapItemArrow->setPos(width() - 12, verticalScrollBar()->value() + 1 + row * 20);
                graphicsPixmapItemArrow->setZValue(3);
            } else {
                graphicsPixmapItemArrow->setPos(width() - 13, row * 20 + 5);
                graphicsPixmapItemArrow->setZValue(-1);
            }

            if (std::get<2>(_oldItemInfo) == row && std::get<1>(_oldItemInfo) == mouseOverArrow) {
                // DO NOTHING
            } else {
                if (std::get<0>(_oldItemInfo)) {
                    std::pair<QGraphicsItem*, int> p = std::make_pair(std::get<0>(_oldItemInfo), std::get<2>(_oldItemInfo));
                    std::vector<std::pair<QGraphicsItem*, int> >::iterator it = std::find(_metaLabels.begin(), _metaLabels.end(), p);
                    if (it != _metaLabels.end()) {
                        _metaLabels.erase(it);
                    }
                    scene()->removeItem(std::get<0>(_oldItemInfo));
                }
                std::tuple<QGraphicsPixmapItem*, MouseOverValue, unsigned> tmp(graphicsPixmapItemArrow, mouseOverArrow, row);
                _oldItemInfo = tmp;
                if (mouseOverArrow != MouseOverValue::OPEN_EYE && mouseOverArrow != MouseOverValue::CLOSED_EYE) {
                    std::pair<QGraphicsItem*, int> p = std::make_pair(graphicsPixmapItemArrow, row);
                    _metaLabels.push_back(p);
                }
                scene()->addItem(graphicsPixmapItemArrow);
            }
        } else {
            if (std::get<0>(_oldItemInfo)) {
                scene()->removeItem(std::get<0>(_oldItemInfo));
            }
            std::tuple<QGraphicsPixmapItem*, MouseOverValue, unsigned> tmp(nullptr, MouseOverValue::NONE, -1);
            _oldItemInfo = tmp;
        }
    } else {
        if (std::get<0>(_oldItemInfo)) {
            std::pair<QGraphicsItem*, int> p = std::make_pair(std::get<0>(_oldItemInfo), std::get<2>(_oldItemInfo));
            std::vector<std::pair<QGraphicsItem*, int> >::iterator it = std::find(_metaLabels.begin(), _metaLabels.end(), p);
            if (it != _metaLabels.end()) {
                _metaLabels.erase(it);
            }
            scene()->removeItem(std::get<0>(_oldItemInfo));
        }
        std::tuple<QGraphicsPixmapItem*, MouseOverValue, unsigned> tmp(nullptr, MouseOverValue::NONE, -1);
        _oldItemInfo = tmp;
    }
    if (QGraphicsItem* graphicsItem = scene()->itemAt(scenePt, transform())) {
        QGraphicsPixmapItem* graphicsPixmapItem = qgraphicsitem_cast<QGraphicsPixmapItem*>(graphicsItem);
        if (graphicsPixmapItem || isClickableLabelItem(graphicsItem)) {
            setCursor(Qt::PointingHandCursor);
        } else {
            setCursor(Qt::ArrowCursor);
        }
    } else {
        setCursor(Qt::ArrowCursor);
    }
}

//---------------------------------------------------------
//   TRiwLabels::cursorIsOn
//---------------------------------------------------------

QString TRowLabels::cursorIsOn()
{
    QPointF scenePos = mapToScene(viewport()->mapFromGlobal(QCursor::pos()));
    QGraphicsItem* graphicsItem = scene()->itemAt(scenePos, transform());
    if (graphicsItem) {
        auto it = _metaLabels.begin();
        for (; it != _metaLabels.end(); ++it) {
            if ((*it).first == graphicsItem) {
                break;
            }
        }
        if (it != _metaLabels.end()) {
            return "meta";
        } else {
            return "instrument";
        }
    } else {
        return "";
    }
}

//---------------------------------------------------------
//   Timeline
//---------------------------------------------------------

Timeline::Timeline(QSplitter* splitter, const muse::modularity::ContextPtr& iocCtx)
    : QGraphicsView(splitter), muse::Contextable(iocCtx)
{
    TRACEFUNC;

    setFocusPolicy(Qt::NoFocus);
    setAlignment(Qt::Alignment((Qt::AlignLeft | Qt::AlignTop)));
    setAttribute(Qt::WA_OpaquePaintEvent);
    setObjectName("Timeline");

    // theming
    _lightTheme.backgroundColor      = QColor(Qt::lightGray);
    _lightTheme.labelsColor1         = QColor(Qt::black);
    _lightTheme.labelsColor2         = QColor(150, 150, 150);
    _lightTheme.labelsColor3         = QColor(211, 211, 211);
    _lightTheme.gridColor1           = QColor(150, 150, 150);
    _lightTheme.gridColor2           = QColor(211, 211, 211);
    _lightTheme.measureMetaColor     = QColor(0, 0, 0);
    _lightTheme.selectionColor       = QColor(173, 216, 230);
    _lightTheme.nonVisiblePenColor   = QColor(100, 150, 250);
    _lightTheme.nonVisibleBrushColor = QColor(192, 192, 192, 180);
    _lightTheme.colorBoxColor        = QColor(Qt::gray);
    _lightTheme.metaValuePenColor    = QColor(Qt::black);
    _lightTheme.metaValueBrushColor  = QColor(Qt::gray);

    _darkTheme.backgroundColor       = QColor(35, 35, 35);
    _darkTheme.labelsColor1          = QColor(225, 225, 225);
    _darkTheme.labelsColor2          = QColor(55, 55, 55);
    _darkTheme.labelsColor3          = QColor(70, 70, 70);
    _darkTheme.gridColor1            = QColor(50, 50, 50);
    _darkTheme.gridColor2            = QColor(75, 75, 75);
    _darkTheme.measureMetaColor      = QColor(200, 200, 200);
    _darkTheme.selectionColor        = QColor(55, 70, 75);
    _darkTheme.nonVisiblePenColor    = QColor(40, 60, 80);
    _darkTheme.nonVisibleBrushColor  = QColor(55, 55, 55, 180);
    _darkTheme.colorBoxColor         = QColor(Qt::gray);
    _darkTheme.metaValuePenColor     = QColor(Qt::lightGray);
    _darkTheme.metaValueBrushColor   = QColor(Qt::darkGray);

    _splitter = splitter;
    _rowNames = new TRowLabels(splitter, this);
    _splitter->addWidget(_rowNames);
    _splitter->addWidget(this);
    _splitter->setChildrenCollapsible(false);
    _splitter->setStretchFactor(0, 0);
    _splitter->setStretchFactor(1, 0);

    setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Ignored);

    setScene(new QGraphicsScene);
    setSceneRect(0, 0, 100, 100);
    scene()->setBackgroundBrush(QBrush(activeTheme().backgroundColor));

    connect(verticalScrollBar(), &QScrollBar::valueChanged, _rowNames->verticalScrollBar(), &QScrollBar::setValue);
    connect(verticalScrollBar(), &QScrollBar::valueChanged, this, &Timeline::handleScroll);
    connect(_rowNames, &TRowLabels::swapMeta, this, &Timeline::swapMeta);
    connect(this, &Timeline::moved, _rowNames, &TRowLabels::mouseOver);

    m_videoThumbnails = new TimelineVideoThumbnails(this);
    m_videoBand = new TimelineVideoBand(this, TimelineVideoBand::Kind::Pictures, this);
    m_videoRowHeight = std::clamp(configuration()->timelineVideoRowHeight(), _gridHeight, 6 * _gridHeight);
    connect(m_videoThumbnails, &TimelineVideoThumbnails::thumbnailsChanged, m_videoBand, [this]() {
        m_videoBand->update();
    });

    initMetas();

    std::tuple<QGraphicsItem*, int, QColor> ohi(nullptr, -1, QColor());
    _oldHoverInfo = ohi;
    std::tuple<int, qreal, EngravingItem*, EngravingItem*, bool> ri(0, 0, nullptr, nullptr, false);
    _repeatInfo = ri;

    static const char* startRepeat[] = {
        "7 14 2 1",
        "# c #000000",
        ". c None",
        "##.#...",
        "##.#...",
        "##.#...",
        "##.#...",
        "##.#.##",
        "##.#.##",
        "##.#...",
        "##.#...",
        "##.#.##",
        "##.#.##",
        "##.#...",
        "##.#...",
        "##.#...",
        "##.#..."
    };

    static const char* endRepeat[] = {
        "7 14 2 1",
        "# c #000000",
        ". c None",
        "...#.##",
        "...#.##",
        "...#.##",
        "...#.##",
        "##.#.##",
        "##.#.##",
        "...#.##",
        "...#.##",
        "##.#.##",
        "##.#.##",
        "...#.##",
        "...#.##",
        "...#.##",
        "...#.##"
    };

    static const char* endBarline[] = {
        "7 14 2 1",
        "# c #000000",
        ". c None",
        "...#.##",
        "...#.##",
        "...#.##",
        "...#.##",
        "...#.##",
        "...#.##",
        "...#.##",
        "...#.##",
        "...#.##",
        "...#.##",
        "...#.##",
        "...#.##",
        "...#.##",
        "...#.##"
    };

    static const char* doubleBarline[] = {
        "7 14 2 1",
        "# c #000000",
        ". c None",
        "..#.#..",
        "..#.#..",
        "..#.#..",
        "..#.#..",
        "..#.#..",
        "..#.#..",
        "..#.#..",
        "..#.#..",
        "..#.#..",
        "..#.#..",
        "..#.#..",
        "..#.#..",
        "..#.#..",
        "..#.#.."
    };

    static const char* reverseEndBarline[] = {
        "7 14 2 1",
        "# c #000000",
        ". c None",
        "##.#...",
        "##.#...",
        "##.#...",
        "##.#...",
        "##.#...",
        "##.#...",
        "##.#...",
        "##.#...",
        "##.#...",
        "##.#...",
        "##.#...",
        "##.#...",
        "##.#...",
        "##.#..."
    };

    static const char* heavyBarline[] = {
        "6 14 2 1",
        "# c #000000",
        ". c None",
        "..##..",
        "..##..",
        "..##..",
        "..##..",
        "..##..",
        "..##..",
        "..##..",
        "..##..",
        "..##..",
        "..##..",
        "..##..",
        "..##..",
        "..##..",
        "..##.."
    };

    static const char* doubleHeavyBarline[] = {
        "7 14 2 1",
        "# c #000000",
        ". c None",
        ".##.##.",
        ".##.##.",
        ".##.##.",
        ".##.##.",
        ".##.##.",
        ".##.##.",
        ".##.##.",
        ".##.##.",
        ".##.##.",
        ".##.##.",
        ".##.##.",
        ".##.##.",
        ".##.##.",
        ".##.##."
    };

    QPixmap* startRepeatPixmap = new QPixmap(startRepeat);
    QPixmap* endRepeatPixmap = new QPixmap(endRepeat);
    QPixmap* endBarlinePixmap = new QPixmap(endBarline);
    QPixmap* doubleBarlinePixmap = new QPixmap(doubleBarline);
    QPixmap* reverseEndBarlinePixmap = new QPixmap(reverseEndBarline);
    QPixmap* heavyBarlinePixmap = new QPixmap(heavyBarline);
    QPixmap* doubleHeavyBarlinePixmap = new QPixmap(doubleHeavyBarline);

    _barlines[BarLineType::START_REPEAT] = startRepeatPixmap;
    _barlines[BarLineType::END_REPEAT] = endRepeatPixmap;
    _barlines[BarLineType::END] = endBarlinePixmap;
    _barlines[BarLineType::DOUBLE] = doubleBarlinePixmap;
    _barlines[BarLineType::REVERSE_END] = reverseEndBarlinePixmap;
    _barlines[BarLineType::HEAVY] = heavyBarlinePixmap;
    _barlines[BarLineType::DOUBLE_HEAVY] = doubleHeavyBarlinePixmap;

    updateDefaultTrackColor();

    uiConfiguration()->currentThemeChanged().onNotify(this, [this]() {
        updateTimelineTheme();
    });

    initPlaybackCursor();

    //! NOTE: tracks becoming/stopping being force-muted by another track's solo
    playbackController()->trackMuteStateChanged().onReceive(this, [this](const InstrumentTrackId&, bool, bool) {
        scheduleLabelsUpdate();
    });

    configuration()->timelineRowsVisibilityChanged().onNotify(this, [this]() {
        applyMetaRowsVisibility();
        updateVideoBand();
        updateGrid();
    });
}

//! NOTE: the stable id each meta row's visibility is persisted under, see notationscenetypes.h
const std::string& Timeline::metaRowId(void (Timeline::* func)(Segment*, int*, int))
{
    static const std::vector<std::pair<void (Timeline::*)(Segment*, int*, int), std::string> > ids {
        { &Timeline::tempoMeta, TIMELINE_ROW_TEMPO },
        { &Timeline::timeMeta, TIMELINE_ROW_TIME_SIGNATURE },
        { &Timeline::timecodeMeta, TIMELINE_ROW_TIMECODE },
        { &Timeline::hitPointMeta, TIMELINE_ROW_HIT_POINTS },
        { &Timeline::rehearsalMeta, TIMELINE_ROW_REHEARSAL_MARK },
        { &Timeline::keyMeta, TIMELINE_ROW_KEY_SIGNATURE },
        { &Timeline::barlineMeta, TIMELINE_ROW_BARLINES },
        { &Timeline::jumpMarkerMeta, TIMELINE_ROW_JUMPS_AND_MARKERS },
        { &Timeline::measureMeta, TIMELINE_ROW_MEASURES },
    };

    for (const auto& [metaFunc, id] : ids) {
        if (metaFunc == func) {
            return id;
        }
    }

    static const std::string empty;
    return empty;
}

void Timeline::initMetas()
{
    _metas.clear();
    _metas.push_back({ muse::qtrc("notation/timeline", "Tempo"), &Timeline::tempoMeta, true });
    _metas.push_back({ muse::qtrc("notation/timeline", "Time signature"), &Timeline::timeMeta, true });
    _metas.push_back({ muse::qtrc("notation/timeline", "Timecode"), &Timeline::timecodeMeta, true });
    _metas.push_back({ muse::qtrc("notation/timeline", "Hit points"), &Timeline::hitPointMeta, true });
    _metas.push_back({ muse::qtrc("notation/timeline", "Rehearsal mark"), &Timeline::rehearsalMeta, true });
    _metas.push_back({ muse::qtrc("notation/timeline", "Key signature"), &Timeline::keyMeta, true });
    _metas.push_back({ muse::qtrc("notation/timeline", "Barlines"), &Timeline::barlineMeta, true });
    _metas.push_back({ muse::qtrc("notation/timeline", "Jumps and markers"), &Timeline::jumpMarkerMeta, true });
    _metas.push_back({ muse::qtrc("notation/timeline", "Measures"), &Timeline::measureMeta, true });

    applyMetaRowsVisibility();
}

void Timeline::applyMetaRowsVisibility()
{
    for (auto& meta : _metas) {
        std::get<2>(meta) = configuration()->isTimelineRowVisible(metaRowId(std::get<1>(meta)));
    }

    //! NOTE: the collapsed view is anchored on the Measures row (it's where the
    //! expand arrow lives), so it can't stay collapsed without it
    if (_collapsedMeta && !measuresRowVisible()) {
        _collapsedMeta = false;
    }
}

bool Timeline::measuresRowVisible() const
{
    if (_collapsedMeta) {
        return true;
    }

    for (const auto& meta : _metas) {
        if (std::get<1>(meta) == &Timeline::measureMeta) {
            return std::get<2>(meta);
        }
    }

    return false;
}

int Timeline::nswappableMetas() const
{
    return static_cast<int>(nmetas()) - (measuresRowVisible() ? 1 : 0);
}

bool Timeline::handleEvent(QEvent* e)
{
    return QWidget::event(e);
}

//---------------------------------------------------------
//   Timeline::drawGrid
//---------------------------------------------------------

void Timeline::drawGrid(int globalRows, int globalCols, int startMeasure, int endMeasure)
{
    TRACEFUNC;

    if (endMeasure < 0) {
        endMeasure = globalCols;
    }
    if (startMeasure < 0) {
        endMeasure = startMeasure;
    }

    const bool rebuildAll = (
        gridRows != globalRows || gridCols != globalCols
        || (startMeasure == 0 && 2 * (endMeasure - startMeasure) > globalCols)  // rebuild all if more than half of score has changed
        );
    const bool rebuildPartial = !rebuildAll && (startMeasure >= 0);

    const unsigned numMetas = nmetas();

    if (rebuildAll) {
        clearScene();
        startMeasure = 0;
        endMeasure = globalCols;
    } else {
        if (rebuildPartial) {
            const QRectF replacedRect
                = getMeasureRect(startMeasure, 0, numMetas) | getMeasureRect(endMeasure - 1, globalRows - 1, numMetas);
            const QList<QGraphicsItem*> replacedItems = scene()->items(replacedRect, Qt::ContainsItemShape);
            for (QGraphicsItem* item : replacedItems) {
                if (item->data(keyItemType).value<ItemType>() != ItemType::TYPE_MEASURE) {
                    continue;
                }
                scene()->removeItem(item);
                delete item;
            }
        }

        // Meta rows are still rebuilt from scratch, remove old meta rows manually
        const QList<QGraphicsItem*> items = scene()->items();
        for (QGraphicsItem* item : items) {
            if (item->data(keyItemType).value<ItemType>() != ItemType::TYPE_META) {
                continue;
            }
            scene()->removeItem(item);
            delete item;
        }
    }

    _metaRows.clear();

    if (globalRows == 0 || globalCols == 0) {
        return;
    }

    int stagger = 0;
    setMinimumHeight(_gridHeight * (numMetas + 1) + 5 + horizontalScrollBar()->height() + videoBandHeight());
    setMinimumWidth(std::min(_gridWidth * 3, 150)); // 3 measures, not more than at the old maximum zoom
    _globalZValue = 1;

    m_measureStartTicks.clear();
    for (const Measure* m = score()->firstMeasure(); m; m = m->nextMeasure()) {
        m_measureStartTicks.push_back(m->tick().ticks());
    }

    // Draw grid
    Measure* currMeasure = score()->firstMeasure();
    for (int i = 0; i < startMeasure; ++i) {
        currMeasure = currMeasure->nextMeasure();
    }

    QList<Part*> partList = getParts();

    for (int col = startMeasure; col < endMeasure; col++) {
        for (int row = 0; row < globalRows; row++) {
            QGraphicsRectItem* graphicsRectItem = new QGraphicsRectItem(getMeasureRect(col, row, numMetas));
            graphicsRectItem->setData(keyItemType, QVariant::fromValue(ItemType::TYPE_MEASURE));

            setMetaData(graphicsRectItem, row, ElementType::INVALID, currMeasure, false, 0);

            QString translateMeasure = muse::qtrc("notation/timeline", "Measure");
            QChar initialLetter = translateMeasure[0];
            const QString partName = partList.size() > row ? partLabel(partList.at(row)) : QString();

            graphicsRectItem->setToolTip(initialLetter + u" "_s + QString::number(currMeasure->measureNumber() + 1) + u", "_s + partName);
            graphicsRectItem->setPen(QPen(activeTheme().backgroundColor));
            graphicsRectItem->setBrush(QBrush(colorBox(graphicsRectItem)));
            graphicsRectItem->setZValue(-3);
            scene()->addItem(graphicsRectItem);
        }

        currMeasure = currMeasure->nextMeasure();
    }
    setSceneRect(0, 0, getWidth(), getHeight());

    // Draw meta rows and separator
    QGraphicsLineItem* graphicsLineItemSeparator = new QGraphicsLineItem(0,
                                                                         _gridHeight * numMetas + verticalScrollBar()->value() + 1,
                                                                         getWidth() - 1,
                                                                         _gridHeight * numMetas + verticalScrollBar()->value() + 1);
    graphicsLineItemSeparator->setData(keyItemType, QVariant::fromValue(ItemType::TYPE_META));
    graphicsLineItemSeparator->setPen(QPen(activeTheme().gridColor1, 4));
    graphicsLineItemSeparator->setZValue(-2);
    scene()->addItem(graphicsLineItemSeparator);
    std::pair<QGraphicsItem*, int> pairGraphicsIntSeparator(graphicsLineItemSeparator, numMetas);
    _metaRows.push_back(pairGraphicsIntSeparator);

    for (unsigned row = 0; row < numMetas; row++) {
        QGraphicsRectItem* metaRow = new QGraphicsRectItem(0,
                                                           _gridHeight * row + verticalScrollBar()->value(),
                                                           getWidth(),
                                                           _gridHeight);
        metaRow->setData(keyItemType, QVariant::fromValue(ItemType::TYPE_META));
        metaRow->setBrush(QBrush(activeTheme().gridColor2));
        metaRow->setPen(QPen(activeTheme().gridColor1));
        metaRow->setData(0, QVariant::fromValue<int>(-1));

        scene()->addItem(metaRow);

        std::pair<QGraphicsItem*, int> pairGraphicsIntMeta(metaRow, row);
        _metaRows.push_back(pairGraphicsIntMeta);
    }

    int xPos = 0;

    // Create stagger array if _collapsedMeta is false
    std::vector<int> staggerArr(numMetas, 0);    // Default initialized, loop not required

    bool noKey = true;
    std::get<4>(_repeatInfo) = false;

    for (Measure* cm = score()->firstMeasure(); cm; cm = cm->nextMeasure()) {
        for (Segment* currSeg = cm->first(); currSeg; currSeg = currSeg->next()) {
            // Toggle noKey if initial key signature is found
            if (currSeg->isKeySigType() && cm == score()->firstMeasure()) {
                if (noKey && currSeg->tick().isZero()) {
                    noKey = false;
                }
            }

            // If no initial key signature is found, add key signature
            if (cm == score()->firstMeasure() && noKey
                && (currSeg->isTimeSigType() || currSeg->isChordRestType())) {
                if (getMetaRow(muse::qtrc("notation/timeline", "Key signature")) != numMetas) {
                    if (_collapsedMeta) {
                        keyMeta(0, &stagger, xPos);
                    } else {
                        keyMeta(0, &staggerArr[getMetaRow(muse::qtrc("notation/timeline", "Key signature"))], xPos);
                    }
                }
                noKey = false;
            }
            int row = 0;
            for (auto it = _metas.begin(); it != _metas.end(); ++it) {
                std::tuple<QString, void (Timeline::*)(Segment*, int*, int), bool> meta = *it;
                if (!std::get<2>(meta)) {
                    continue;
                }
                void (Timeline::* func)(Segment*, int*, int) = std::get<1>(meta);
                if (_collapsedMeta) {
                    (this->*func)(currSeg, &stagger, xPos);
                } else {
                    (this->*func)(currSeg, &staggerArr[row], xPos);
                }
                row++;
            }
        }
        // Handle all jumps here
        if (getMetaRow(muse::qtrc("notation/timeline", "Jumps and markers")) != numMetas) {
            ElementList measureElementsList = cm->el();
            for (EngravingItem* element : measureElementsList) {
                std::get<3>(_repeatInfo) = element;
                if (element->isMarker()) {
                    jumpMarkerMeta(0, &stagger, xPos);
                }
            }
            for (EngravingItem* element : measureElementsList) {
                if (element->isJump()) {
                    std::get<2>(_repeatInfo) = element;
                    if (_collapsedMeta) {
                        jumpMarkerMeta(0, &stagger, xPos);
                    } else {
                        jumpMarkerMeta(0, &std::get<0>(_repeatInfo), xPos);
                    }
                }
            }
        }
        stagger = 0;
        std::get<0>(_repeatInfo) = 0;

        for (unsigned row = 0; row < numMetas; row++) {
            staggerArr[row] = 0;
        }
        xPos += _gridWidth;
        std::get<4>(_repeatInfo) = false;
    }

    gridRows = globalRows;
    gridCols = globalCols;
}

//---------------------------------------------------------
//   Video row
//---------------------------------------------------------

bool Timeline::hasVideo() const
{
    const mu::project::IProjectVideoSettingsPtr settings = videoSettings();
    return settings && settings->attachment().isValid();
}

//! NOTE: like the Video panel's and the Mixer's Mute/Solo: one cancels the other
void Timeline::toggleVideoMute()
{
    mu::project::updateVideoAttachment(videoSettings(), [](mu::project::VideoAttachmentSettings& attachment) {
        attachment.muted = !attachment.muted;
        if (attachment.muted) {
            attachment.solo = false;
        }
    });
}

void Timeline::toggleVideoSolo()
{
    mu::project::updateVideoAttachment(videoSettings(), [](mu::project::VideoAttachmentSettings& attachment) {
        attachment.solo = !attachment.solo;
        if (attachment.solo) {
            attachment.muted = false;
        }
    });
}

//! NOTE: same as choosing a file in the Video panel (see VideoPanelModel::setVideoPath())
void Timeline::chooseVideoFile()
{
    const mu::project::IProjectVideoSettingsPtr settings = videoSettings();
    if (!settings) {
        return;
    }

    const muse::io::path_t currentPath = settings->attachment().path;
    const std::vector<std::string> filter {
        muse::trc("notation/timeline", "Video files") + " (*.mp4 *.mov *.m4v *.avi *.mkv *.webm)",
        muse::trc("notation/timeline", "All files") + " (*)"
    };

    const muse::io::path_t path = interactive()->selectOpeningFileSync(muse::trc("notation/timeline", "Choose video"),
                                                                       currentPath.empty() ? muse::io::path_t() : muse::io::dirpath(
                                                                           currentPath),
                                                                       filter);
    if (path.empty()) {
        return;
    }

    mu::project::VideoAttachmentSettings updated = settings->attachment();
    updated.path = path;
    //! NOTE: hit points are timed against the previous video's own footage
    updated.hitPoints.clear();
    settings->setAttachment(updated);

    playbackConfiguration()->addRecentVideoFile(path.toQString());
}

void Timeline::onVideoSettingsChanged()
{
    const muse::io::path_t path = hasVideo() ? videoSettings()->attachment().path : muse::io::path_t();

    if (path == m_videoPath) {
        //! NOTE: e.g. a new offset: the pictures are filed by video time, still valid
        m_videoBand->update();
        return;
    }

    m_videoPath = path;
    m_videoThumbnails->setVideo(path);
    updateVideoBand();
    updateGridFull();
}

int Timeline::videoBandHeight() const
{
    //! NOTE: shown even without a video (saying so, see paintVideoRow()), like its View menu item says
    return configuration()->isTimelineRowVisible(TIMELINE_ROW_VIDEO) ? m_videoRowHeight : 0;
}

void Timeline::updateVideoBand()
{
    const int height = videoBandHeight();

    setViewportMargins(0, height, 0, 0);
    _rowNames->setVideoBand(height);

    m_videoBand->setVisible(height > 0);
    m_videoBand->setGeometry(viewport()->x(), 0, viewport()->width(), height);
    m_videoBand->raise();
    m_videoBand->update();

    setMinimumHeight(_gridHeight * (nmetas() + 1) + 5 + horizontalScrollBar()->height() + height);
}

//! NOTE: from one row to six
void Timeline::setVideoRowHeight(int height, bool save)
{
    height = std::clamp(height, _gridHeight, 6 * _gridHeight);
    if (height != m_videoRowHeight) {
        m_videoRowHeight = height;
        updateVideoBand();
    }

    if (save) {
        configuration()->setTimelineVideoRowHeight(m_videoRowHeight);
    }
}

void Timeline::resizeEvent(QResizeEvent* event)
{
    QGraphicsView::resizeEvent(event);

    //! NOTE: not isVisible(): the Timeline is rendered offscreen (see TimelineView), never shown
    if (m_videoBand && !m_videoBand->isHidden()) {
        m_videoBand->setGeometry(viewport()->x(), 0, viewport()->width(), m_videoBand->height());
    }
}

static const QColor VIDEO_PICTURE_FRAME_COLOR(0x3B, 0x94, 0xE5);

//! NOTE: the row is cut into picture-wide slots, independent of the measures: each shows the frame
//! at its middle (any frame shown within the slot will do, see TimelineVideoThumbnails::Slot)
void Timeline::paintVideoRow(QPainter* painter, int height)
{
    auto drawMessage = [this, painter, height](const QString& message) {
        painter->save();
        painter->resetTransform();
        painter->setPen(activeTheme().measureMetaColor);
        painter->setFont(QApplication::font());
        painter->drawText(QRect(8, 0, viewport()->width() - 16, height - 2), Qt::AlignLeft | Qt::AlignVCenter, message);
        painter->restore();
    };

    if (!hasVideo()) {
        drawMessage(muse::qtrc("notation/timeline", "No video"));
        return;
    }

    if (!score() || m_measureStartTicks.empty()) {
        return;
    }

    //! NOTE: until the first pictures in view are there (it takes a few seconds for a video just opened)
    auto drawLoading = [&drawMessage]() {
        drawMessage(muse::qtrc("notation/timeline", "Loading…"));
    };

    const double aspect = m_videoThumbnails->aspectRatio();
    if (aspect <= 0.0) {
        if (m_videoThumbnails->isOpening()) {
            drawLoading();
        }
        return;
    }

    const qreal pictureHeight = height - 3; // a pixel above, the separator below
    const int wantedPixels = static_cast<int>(std::ceil(pictureHeight * qGuiApp->devicePixelRatio()));
    const qreal slotWidth = std::max<qreal>(8.0, std::round(pictureHeight * aspect));
    const qreal rowWidth = getWidth();
    const int offsetMs = videoSettings()->attachment().offsetMs;
    const double durationSecs = m_videoThumbnails->durationSecs();

    const QRectF visible = mapToScene(viewport()->rect()).boundingRect();
    const qreal right = std::min(visible.right(), rowWidth);

    painter->save();
    painter->setRenderHint(QPainter::SmoothPixmapTransform);
    painter->translate(-visible.left(), 0); // painting in scene x

    std::vector<TimelineVideoThumbnails::Slot> missing;
    bool anyDrawn = false;
    for (int i = std::max(0, static_cast<int>(std::floor(visible.left() / slotWidth))); i * slotWidth < right; ++i) {
        const qreal x0 = i * slotWidth;
        const qreal x1 = std::min(x0 + slotWidth, rowWidth);
        const double startSecs = videoSecsAtX(x0, offsetMs);
        const double endSecs = videoSecsAtX(x1, offsetMs);
        if (durationSecs > 0.0 && startSecs >= durationSecs) {
            break; // past the end of the video
        }

        const TimelineVideoThumbnails::Slot slot { (startSecs + endSecs) / 2, (endSecs - startSecs) / 2 };
        const QImage image = m_videoThumbnails->thumbnail(slot);
        if (image.isNull()) {
            missing.push_back(slot);
            continue;
        }

        // The last slot may be cut by the end of the row: so is its picture
        const qreal shownFraction = (x1 - x0) / slotWidth;
        const QRectF source(0, 0, image.width() * shownFraction, image.height());
        const QRectF target(x0, 1, x1 - x0, pictureHeight);
        painter->drawImage(target, image, source);
        anyDrawn = true;

        // A thin blue frame around each picture, to tell them apart
        painter->setPen(QPen(VIDEO_PICTURE_FRAME_COLOR, 0.5)); // a single physical pixel on high-DPI screens
        painter->setBrush(Qt::NoBrush);
        painter->drawRect(target.adjusted(0.25, 0.25, -0.25, -0.25));

        //! NOTE: decoded for a lower row: shown enlarged until decoded again at this height
        if (image.height() < wantedPixels * 0.8) {
            missing.push_back(slot);
        }
    }

    painter->restore();

    if (!missing.empty()) {
        m_videoThumbnails->request(missing, wantedPixels);

        if (!anyDrawn) {
            drawLoading();
        }
    }
}

//! NOTE: the video time at a Timeline x, within its measure in proportion (measures all have the
//! same width); same conversion as mu::project::videoPositionMsForTick(), without rounding to ms
double Timeline::videoSecsAtX(qreal x, int offsetMs) const
{
    const int count = static_cast<int>(m_measureStartTicks.size());
    const int index = std::clamp(static_cast<int>(std::floor(x / _gridWidth)), 0, count - 1);
    const double fraction = std::clamp((x - index * _gridWidth) / _gridWidth, 0.0, 1.0);

    const int startTick = m_measureStartTicks[index];
    const int endTick = index + 1 < count ? m_measureStartTicks[index + 1] : score()->endTick().ticks();
    const int tick = startTick + static_cast<int>(std::lround(fraction * (endTick - startTick)));

    return std::max(0.0, score()->utick2utime(tick) + offsetMs / 1000.0);
}

//---------------------------------------------------------
//   Timeline::tempoMeta
//---------------------------------------------------------

void Timeline::tempoMeta(Segment* seg, int* stagger, int pos)
{
    // Find position of tempoMeta in metas
    int row = getMetaRow(muse::qtrc("notation/timeline", "Tempo"));

    // Add all tempo texts in this segment
    const std::vector<EngravingItem*> annotations = seg->annotations();
    for (EngravingItem* element : annotations) {
        if (element->isTempoText()) {
            TempoText* text = toTempoText(element);
            qreal x = pos + (*stagger) * _spacing;
            if (addMetaValue(x, pos, text->plainText(), row, ElementType::TEMPO_TEXT, element, 0, seg->measure())) {
                (*stagger)++;
                _globalZValue++;
            }
        }
    }
}

//---------------------------------------------------------
//   Timeline::timeMeta
//---------------------------------------------------------

void Timeline::timeMeta(Segment* seg, int* stagger, int pos)
{
    if (!seg->isTimeSigType()) {
        return;
    }

    TRACEFUNC;

    int x = pos + (*stagger) * _spacing;

    // Find position of timeMeta in metas
    int row = getMetaRow(muse::qtrc("notation/timeline", "Time signature"));

    TimeSig* originalTimeSig = toTimeSig(seg->element(0));
    if (!originalTimeSig) {
        return;
    }

    // Check if same across all staves
    const size_t nrows = score()->staves().size();
    bool same = true;
    for (size_t track = 0; track < nrows; track++) {
        const TimeSig* currTimeSig = toTimeSig(seg->element(track * VOICES));
        if (!currTimeSig) {
            same = false;
            break;
        }
        if (*currTimeSig == *originalTimeSig) {
            continue;
        }
        same = false;
        break;
    }
    if (!same) {
        return;
    }
    QString text = QString::number(originalTimeSig->numerator()) + QString("/") + QString::number(originalTimeSig->denominator());

    if (addMetaValue(x, pos, text, row, ElementType::TIMESIG, 0, seg, seg->measure())) {
        (*stagger)++;
        _globalZValue++;
    }
}

//---------------------------------------------------------
//   Timeline::rehearsalMeta
//---------------------------------------------------------

void Timeline::rehearsalMeta(Segment* seg, int* stagger, int pos)
{
    int row = getMetaRow(muse::qtrc("notation/timeline", "Rehearsal mark"));

    for (EngravingItem* element : seg->annotations()) {
        int x = pos + (*stagger) * _spacing;
        if (element->isRehearsalMark()) {
            RehearsalMark* rehearsal_mark = toRehearsalMark(element);
            if (!rehearsal_mark) {
                continue;
            }

            if (addMetaValue(x, pos, rehearsal_mark->plainText(), row, ElementType::REHEARSAL_MARK, element, 0, seg->measure())) {
                (*stagger)++;
                _globalZValue++;
            }
        }
    }
}

//---------------------------------------------------------
//   Timeline::keyMeta
//---------------------------------------------------------

void Timeline::keyMeta(Segment* seg, int* stagger, int pos)
{
    // If seg is nullptr, handle initial key signature
    if (seg && !seg->isKeySigType()) {
        return;
    }

    TRACEFUNC;

    int row = getMetaRow(muse::qtrc("notation/timeline", "Key signature"));
    std::map<Key, int> keyFrequencies;
    const std::vector<Staff*>& staves = score()->staves();

    int track = 0;
    for (Staff* stave : staves) {
        if (!stave->show()) {
            track += VOICES;
            continue;
        }

        // Ignore unpitched staves
        if ((seg && !stave->isPitchedStaff(seg->tick())) || (!seg && !stave->isPitchedStaff(Fraction(0, 1)))) {
            track += VOICES;
            continue;
        }

        // Add corrected key signature to map
        // Atonal -> Key::INVALID
        // Custom -> Key::NUM_OF
        const KeySig* currKeySig = nullptr;
        if (seg) {
            currKeySig = toKeySig(seg->element(track));
        }

        Key globalKey;
        if (seg) {
            globalKey = stave->concertKey(seg->tick());
        } else {
            globalKey = stave->concertKey(Fraction(0, 1));
        }
        if (currKeySig) {
            if (currKeySig->generated()) {
                return;
            }
            globalKey = currKeySig->concertKey();
        }

        if (currKeySig && currKeySig->isAtonal()) {
            globalKey = Key::INVALID;
        } else if (currKeySig && currKeySig->isCustom()) {
            globalKey = Key::NUM_OF;
        }

        std::map<Key, int>::iterator it = keyFrequencies.find(globalKey);
        if (it != keyFrequencies.end()) {
            keyFrequencies[globalKey]++;
        } else {
            keyFrequencies[globalKey] = 1;
        }

        track += VOICES;
    }

    // Change key into QString
    Key newKey = Key::C;
    int maxKeyFreq = 0;
    for (std::map<Key, int>::iterator iter = keyFrequencies.begin(); iter != keyFrequencies.end(); ++iter) {
        if (iter->second > maxKeyFreq) {
            newKey = iter->first;
            maxKeyFreq = iter->second;
        }
    }
    QString keyText;
    QString tooltip;
    if (newKey == Key::INVALID) {
        keyText = "X";
        tooltip = TConv::translatedUserName(Key::INVALID, true);
    } else if (newKey == Key::NUM_OF) {
        keyText = "?";
        tooltip = muse::qtrc("notation/timeline", "Custom key signature");
    } else if (int(newKey) == 0) {
        keyText = "\u266E";
        tooltip = TConv::translatedUserName(Key::C);
    } else if (int(newKey) < 0) {
        keyText = QString::number(std::abs(int(newKey))) + "\u266D";
        tooltip = TConv::translatedUserName(newKey);
    } else {
        keyText = QString::number(std::abs(int(newKey))) + "\u266F";
        tooltip = TConv::translatedUserName(newKey);
    }

    int x = pos + (*stagger) * _spacing;
    Measure* measure = seg ? seg->measure() : 0;
    if (addMetaValue(x, pos, keyText, row, ElementType::KEYSIG, 0, seg, measure, tooltip)) {
        (*stagger)++;
        _globalZValue++;
    }
}

//---------------------------------------------------------
//   Timeline::barLineMeta
//---------------------------------------------------------

void Timeline::barlineMeta(Segment* seg, int* stagger, int pos)
{
    if (!seg->isBeginBarLineType() && !seg->isEndBarLineType() && !seg->isBarLine() && !seg->isStartRepeatBarLineType()) {
        return;
    }

    TRACEFUNC;

    // Find position of repeat_meta in metas
    int row = getMetaRow(muse::qtrc("notation/timeline", "Barlines"));

    QString repeatText = "";
    BarLine* barline = toBarLine(seg->element(0));

    if (barline) {
        switch (barline->barLineType()) {
        case BarLineType::START_REPEAT:
        case BarLineType::END_REPEAT:
        case BarLineType::END:
        case BarLineType::DOUBLE:
        case BarLineType::REVERSE_END:
        case BarLineType::HEAVY:
        case BarLineType::DOUBLE_HEAVY:
            repeatText = BarLine::translatedUserTypeName(barline->barLineType());
            break;
        case BarLineType::END_START_REPEAT:
        // actually an end repeat followed by a start repeat, so nothing needs to be done here
        default:
            break;
        }
        _isBarline = true;
    } else {
        return;
    }

    Measure* measure = seg->measure();
    ElementType elementType = ElementType::BAR_LINE;
    EngravingItem* element = nullptr;

    if (repeatText == "") {
        _isBarline = false;
        return;
    }

    int x = pos + (*stagger) * _spacing;
    if (addMetaValue(x, pos, repeatText, row, elementType, element, seg, measure)) {
        (*stagger)++;
        _globalZValue++;
    }
    _isBarline = false;
}

//---------------------------------------------------------
//   Timeline::jumpMarkerMeta
//---------------------------------------------------------

void Timeline::jumpMarkerMeta(Segment* seg, int* stagger, int pos)
{
    if (seg) {
        return;
    }

    TRACEFUNC;

    // Find position of repeat_meta in metas
    int row = getMetaRow(muse::qtrc("notation/timeline", "Jumps and markers"));

    QString text = "";
    EngravingItem* element = nullptr;
    if (std::get<2>(_repeatInfo)) {
        element = std::get<2>(_repeatInfo);
    } else if (std::get<3>(_repeatInfo)) {
        element = std::get<3>(_repeatInfo);
    }

    Measure* measure;
    ElementType elementType;
    if (std::get<2>(_repeatInfo)) {
        Jump* jump = toJump(std::get<2>(_repeatInfo));
        text = jump->plainText();
        measure = jump->measure();
        elementType = ElementType::JUMP;
    } else {
        Marker* marker = toMarker(std::get<3>(_repeatInfo));
        std::list<TextFragment> tf_list = marker->fragmentList();
        for (TextFragment tf : tf_list) {
            text.push_back(tf.text);
        }
        measure = marker->measure();
        if (marker->markerType() == MarkerType::FINE
            || marker->markerType() == MarkerType::TOCODA
            || marker->markerType() == MarkerType::TOCODASYM
            || marker->markerType() == MarkerType::DA_CODA
            || marker->markerType() == MarkerType::DA_DBLCODA
            ) {
            elementType = ElementType::MARKER;
            std::get<2>(_repeatInfo) = std::get<3>(_repeatInfo);
            std::get<3>(_repeatInfo) = nullptr;
        } else {
            elementType = ElementType::MARKER;
        }
    }

    if (text == "") {
        std::get<2>(_repeatInfo) = nullptr;
        std::get<3>(_repeatInfo) = nullptr;
        return;
    }

    int x = pos + (*stagger) * _spacing;
    if (addMetaValue(x, pos, text, row, elementType, element, seg, measure)) {
        (*stagger)++;
        _globalZValue++;
    }
    std::get<2>(_repeatInfo) = nullptr;
    std::get<3>(_repeatInfo) = nullptr;
}

//---------------------------------------------------------
//   Timeline::hitPointMeta
//---------------------------------------------------------

void Timeline::hitPointMeta(Segment* seg, int* stagger, int pos)
{
    if (!seg || seg != seg->measure()->first()) {
        return;
    }

    mu::project::IProjectVideoSettingsPtr settings = videoSettings();
    if (!settings || !settings->attachment().isValid()) {
        return;
    }

    const mu::project::VideoAttachmentSettings& attachment = settings->attachment();
    if (attachment.hitPoints.empty()) {
        return;
    }

    const int row = getMetaRow(muse::qtrc("notation/timeline", "Hit points"));
    const int currentMeasureIndex = pos / _gridWidth;

    for (const mu::project::VideoHitPointSettings& hitPoint : attachment.hitPoints) {
        const int scoreRelativeMs = hitPoint.timeMs - attachment.offsetMs;
        if (scoreRelativeMs < 0) {
            continue;
        }

        const double scoreTimeSeconds = static_cast<double>(scoreRelativeMs) / 1000.0;
        const int tick = std::max(0, score()->utime2utick(scoreTimeSeconds));
        //! NOTE Resolve via the multi-measure-rest-aware measure (matching how the
        //! notation canvas locates the same hit point), so a hit point under a
        //! collapsed multi-measure rest still lands on that rest's Timeline column.
        const Measure* measure = score()->tick2measureMM(Fraction::fromTicks(tick));
        if (!measure || measure->measureIndex() != currentMeasureIndex) {
            continue;
        }

        const int x = pos + (*stagger) * _spacing;
        const QString label = hitPoint.label.empty() ? muse::qtrc("notation/timeline", "Hit") : hitPoint.label.toQString();
        const QString tooltip = muse::qtrc("notation/timeline", "Video hit point at %1").arg(formatVideoTimecode(hitPoint.timeMs));
        if (addMetaValue(x, pos, label, row, ElementType::INVALID, nullptr, nullptr, seg->measure(), tooltip)) {
            (*stagger)++;
            _globalZValue++;
        }
    }
}

//---------------------------------------------------------
//   Timeline::timecodeMeta
//---------------------------------------------------------

void Timeline::timecodeMeta(Segment* seg, int* stagger, int pos)
{
    if (!seg || seg != seg->measure()->first()) {
        return;
    }

    mu::project::IProjectVideoSettingsPtr settings = videoSettings();
    if (!settings || !settings->attachment().isValid()) {
        return;
    }

    const mu::project::VideoAttachmentSettings& attachment = settings->attachment();
    if (attachment.timecodeDisplayMode == mu::project::VideoTimecodeDisplayMode::Off) {
        return;
    }

    const int measureTick = seg->measure()->tick().ticks();
    const int videoPositionMs = mu::project::videoPositionMsForTick(score(), measureTick, attachment.offsetMs);
    const int row = getMetaRow(muse::qtrc("notation/timeline", "Timecode"));
    const int x = pos + (*stagger) * _spacing;

    if (addMetaValue(x, pos, formatVideoTimecode(videoPositionMs), row, ElementType::INVALID, nullptr, nullptr, seg->measure())) {
        (*stagger)++;
        _globalZValue++;
    }
}

//---------------------------------------------------------
//   Timeline::measureMeta
//---------------------------------------------------------

void Timeline::measureMeta(Segment*, int*, int pos)
{
    TRACEFUNC;

    int currMeasureNumber = pos / _gridWidth;
    if (currMeasureNumber == _globalMeasureNumber) {
        return;
    }

    _globalMeasureNumber = currMeasureNumber;

    // Find position of measureMeta in metas
    int row = getMetaRow(muse::qtrc("notation/timeline", "Measures"));

    // Adjust number
    Measure* currMeasure;
    for (currMeasure = score()->firstMeasure(); currMeasureNumber != 0; currMeasureNumber--, currMeasure = currMeasure->nextMeasure()) {
    }

    // Add measure number
    QString measureNumber = (currMeasure->excludeFromNumbering()) ? u"( )"_s : QString::number(currMeasure->measureNumber() + 1);
    QGraphicsTextItem* graphicsTextItem = new QGraphicsTextItem(measureNumber);
    graphicsTextItem->setData(keyItemType, QVariant::fromValue(ItemType::TYPE_META));
    graphicsTextItem->setDefaultTextColor(activeTheme().measureMetaColor);
    graphicsTextItem->setX(pos);
    graphicsTextItem->setY(_gridHeight * row + verticalScrollBar()->value());

    QFont f = graphicsTextItem->font();
    f.setPointSizeF(7.0);
    graphicsTextItem->setFont(f);

    // Left-justify text in its measure (cancelling the text item's own document
    // margin, so the number sits right against the measure's left edge), centered vertically
    qreal remainingHeight = _gridHeight - graphicsTextItem->boundingRect().height();
    graphicsTextItem->setX(graphicsTextItem->x() - graphicsTextItem->document()->documentMargin() + 1);
    graphicsTextItem->setY(graphicsTextItem->y() + remainingHeight / 2);

    int endOfText = graphicsTextItem->x() + graphicsTextItem->boundingRect().width();
    int endOfGrid = getWidth();
    if (endOfText <= endOfGrid) {
        scene()->addItem(graphicsTextItem);

        std::pair<QGraphicsItem*, int> pairMeasureText = std::make_pair(graphicsTextItem, row);
        _metaRows.push_back(pairMeasureText);
    }
}

//---------------------------------------------------------
//   Timeline::getMetaRow
//---------------------------------------------------------

unsigned Timeline::getMetaRow(QString targetText)
{
    if (_collapsedMeta) {
        if (targetText == muse::qtrc("notation/timeline", "Measures")) {
            return 1;
        } else {
            return 0;
        }
    }
    int row = 0;
    for (auto it = _metas.begin(); it != _metas.end(); ++it) {
        std::tuple<QString, void (Timeline::*)(Segment*, int*, int), bool> meta = *it;
        QString metaText = std::get<0>(meta);
        bool visible = std::get<2>(meta);
        if (metaText == targetText && visible) {
            break;
        } else if (!visible) {
            continue;
        }
        row++;
    }
    return row;
}

//---------------------------------------------------------
//   Timeline::addMetaValue
//---------------------------------------------------------

bool Timeline::addMetaValue(int x, int pos, QString metaText, int row, ElementType elementType, EngravingItem* element, Segment* seg,
                            Measure* measure, QString tooltip)
{
    TRACEFUNC;

    QGraphicsTextItem* graphicsTextItem = new QGraphicsTextItem(metaText);
    qreal textWidth = graphicsTextItem->boundingRect().width();

    QGraphicsPixmapItem* graphicsPixmapItem = nullptr;

    std::map<QString, BarLineType> barLineTypes = {
        { BarLine::translatedUserTypeName(BarLineType::START_REPEAT), BarLineType::START_REPEAT },
        { BarLine::translatedUserTypeName(BarLineType::END_REPEAT), BarLineType::END_REPEAT },
        { BarLine::translatedUserTypeName(BarLineType::END), BarLineType::END },
        { BarLine::translatedUserTypeName(BarLineType::DOUBLE), BarLineType::DOUBLE },
        { BarLine::translatedUserTypeName(BarLineType::REVERSE_END), BarLineType::REVERSE_END },
        { BarLine::translatedUserTypeName(BarLineType::HEAVY), BarLineType::HEAVY },
        { BarLine::translatedUserTypeName(BarLineType::DOUBLE_HEAVY), BarLineType::DOUBLE_HEAVY },
    };

    BarLineType barLineType = barLineTypes[metaText];

    if (_isBarline) {
        graphicsPixmapItem = new QGraphicsPixmapItem(*_barlines[barLineType]);
    }

    if (graphicsPixmapItem) {
        textWidth = 10;
        if (textWidth > _gridWidth) {
            textWidth = _gridWidth;
            if (barLineType == BarLineType::END_REPEAT && std::get<4>(_repeatInfo)) {
                textWidth /= 2;
            }
        }
    }

    if (textWidth + x > getWidth()) {
        textWidth = getWidth() - x;
    }

    // Adjust x for end repeats
    if ((barLineType == BarLineType::END_REPEAT
         || barLineType == BarLineType::END
         || barLineType == BarLineType::DOUBLE
         || barLineType == BarLineType::REVERSE_END
         || barLineType == BarLineType::HEAVY
         || barLineType == BarLineType::DOUBLE_HEAVY
         || std::get<2>(_repeatInfo))
        && !_collapsedMeta) {
        if (std::get<0>(_repeatInfo) > 0) {
            x = pos + _gridWidth - std::get<1>(_repeatInfo) + std::get<0>(_repeatInfo) * _spacing;
        } else {
            x = pos + _gridWidth - textWidth;
            std::get<1>(_repeatInfo) = textWidth;
        }
        // Check if extending past left side
        if (x < 0) {
            textWidth = textWidth + x;
            x = 0;
        }
    }

    // Return if past width
    if (x >= getWidth()) {
        return false;
    }

    QGraphicsItem* itemToAdd;
    if (graphicsPixmapItem) {
        // Exact values required for repeat pixmap to work visually
        if (textWidth != 10) {
            graphicsPixmapItem = new QGraphicsPixmapItem();
        }
        if (barLineType == BarLineType::START_REPEAT) {
            std::get<4>(_repeatInfo) = true;
        }
        graphicsPixmapItem->setX(x + 2);
        graphicsPixmapItem->setY(_gridHeight * row + verticalScrollBar()->value() + 3);
        itemToAdd = graphicsPixmapItem;
    } else if (metaText == "\uE047" || metaText == "\uE048") {
        graphicsTextItem->setX(x);
        graphicsTextItem->setY(_gridHeight * row + verticalScrollBar()->value() - 2);
        itemToAdd = graphicsTextItem;
    } else if (row == 0) {
        graphicsTextItem->setX(x);
        graphicsTextItem->setY(_gridHeight * row + verticalScrollBar()->value() - 6);
        itemToAdd = graphicsTextItem;
    } else {
        graphicsTextItem->setX(x);
        graphicsTextItem->setY(_gridHeight * row + verticalScrollBar()->value() - 1);
        itemToAdd = graphicsTextItem;
    }

    QFontMetrics f(QApplication::font());
    QString partName = f.elidedText(graphicsTextItem->toPlainText(),
                                    Qt::ElideRight,
                                    textWidth);

    // Set tool tip if elided
    if (tooltip != "") {
        graphicsTextItem->setToolTip(tooltip);
    } else if (partName != metaText) {
        graphicsTextItem->setToolTip(graphicsTextItem->toPlainText());
    }
    graphicsTextItem->setPlainText(partName);

    // Make text fit within rectangle
    while (graphicsTextItem->boundingRect().width() > textWidth
           && graphicsTextItem->toPlainText() != "") {
        QString text = graphicsTextItem->toPlainText();
        text.chop(1);
        graphicsTextItem->setPlainText(text);
    }

    QGraphicsRectItem* graphicsRectItem = new QGraphicsRectItem(x,
                                                                _gridHeight * row + verticalScrollBar()->value(),
                                                                textWidth,
                                                                _gridHeight);
    if (tooltip != "") {
        graphicsRectItem->setToolTip(tooltip);
    } else if (partName != metaText || graphicsPixmapItem) {
        graphicsRectItem->setToolTip(metaText);
    }

    setMetaData(graphicsRectItem, -1, elementType, measure, true, element, itemToAdd, seg);
    setMetaData(itemToAdd, -1, elementType, measure, true, element, graphicsRectItem, seg);

    graphicsRectItem->setData(keyItemType, QVariant::fromValue(ItemType::TYPE_META));
    itemToAdd->setData(keyItemType, QVariant::fromValue(ItemType::TYPE_META));

    graphicsRectItem->setZValue(_globalZValue);
    itemToAdd->setZValue(_globalZValue);

    graphicsRectItem->setPen(QPen(activeTheme().metaValuePenColor));
    graphicsRectItem->setBrush(QBrush(activeTheme().metaValueBrushColor));

    scene()->addItem(graphicsRectItem);
    scene()->addItem(itemToAdd);

    std::pair<QGraphicsItem*, int> pairTimeRect = std::make_pair(graphicsRectItem, row);
    std::pair<QGraphicsItem*, int> pairTimeText = std::make_pair(itemToAdd, row);
    _metaRows.push_back(pairTimeRect);
    _metaRows.push_back(pairTimeText);

    if (barLineType == BarLineType::END_REPEAT) {
        std::get<0>(_repeatInfo)++;
    }

    return true;
}

//---------------------------------------------------------
//   Timeline::setMetaData
//---------------------------------------------------------

void Timeline::setMetaData(QGraphicsItem* gi, int staff, ElementType et, Measure* m, bool full_measure, EngravingItem* e,
                           QGraphicsItem* pairItem,
                           Segment* seg)
{
    // full_measure true for meta values
    // pr is null for grid items, set for meta values
    // seg is set if key meta
    gi->setData(0, QVariant::fromValue<int>(staff));
    gi->setData(1, QVariant::fromValue<ElementType>(et));
    gi->setData(2, QVariant::fromValue<void*>(m));
    gi->setData(3, QVariant::fromValue<bool>(full_measure));
    gi->setData(4, QVariant::fromValue<void*>(e));
    gi->setData(5, QVariant::fromValue<void*>(pairItem));
    gi->setData(6, QVariant::fromValue<void*>(seg));
}

//---------------------------------------------------------
//   Timeline::getWidth
//---------------------------------------------------------

int Timeline::getWidth() const
{
    if (score()) {
        return static_cast<int>(score()->nmeasures()) * _gridWidth;
    } else {
        return 0;
    }
}

//---------------------------------------------------------
//   Timeline::getHeight
//---------------------------------------------------------

int Timeline::getHeight() const
{
    if (score()) {
        return (nstaves() + static_cast<int>(nmetas())) * _gridHeight + 3;
    } else {
        return 0;
    }
}

//---------------------------------------------------------
//   Timeline::correctStave
//---------------------------------------------------------

staff_idx_t Timeline::correctStave(staff_idx_t stave)
{
    // Find correct stave (skipping hidden staves)
    const std::vector<Staff*>& list = score()->staves();
    size_t count = 0;
    while (stave >= count) {
        if (count >= list.size()) {
            count = list.size() - 1;
            return count;
        }
        if (!list.at(count)->show()) {
            stave++;
        }
        count++;
    }
    return stave;
}

//---------------------------------------------------------
//   Timeline::correctPart
//---------------------------------------------------------

int Timeline::correctPart(staff_idx_t stave)
{
    // Find correct stave (skipping hidden staves)
    const std::vector<Staff*>& list = score()->staves();
    staff_idx_t count = correctStave(stave);
    return getParts().indexOf(list.at(count)->part());
}

//---------------------------------------------------------
//   Timeline::getParts
//---------------------------------------------------------

//! NOTE: one instrument row per part (a multi-staff instrument like a piano gets a
//! single row, its cells colored when ANY of its staves has content), see rowPart()
QList<Part*> Timeline::getParts()
{
    QList<Part*> partList;
    for (Part* p : timelineParts()) {
        partList.append(p);
    }

    return partList;
}

//! NOTE: the parts shown as rows, like the score shows them with "Enable stave sharing"
//! (Layout panel): while it's on, the combined part ("Horn in F 1-2") replaces the parts
//! it combines (their row buttons act on those parts, see rowAudioParts()); once it's off
//! again, the combined part stays in the score, just disabled, and is skipped.
//! NOTE: cached, since it's needed for every grid cell, selected element and playback
//! cursor frame; rebuilt whenever the score's parts or the stave sharing option change
//! (so it never holds a part removed in the meantime)
const std::vector<Part*>& Timeline::timelineParts() const
{
    if (!score()) {
        m_rowsCache.parts.clear();
        m_rowsCache.sourceParts.clear();
        m_rowsCache.score = nullptr;
        return m_rowsCache.parts;
    }

    const bool staveSharing = score()->style().styleB(Sid::enableStaveSharing);
    if (m_rowsCache.score == score() && m_rowsCache.staveSharing == staveSharing && m_rowsCache.sourceParts == score()->parts()) {
        return m_rowsCache.parts;
    }

    m_rowsCache.score = score();
    m_rowsCache.staveSharing = staveSharing;
    m_rowsCache.sourceParts = score()->parts();
    m_rowsCache.parts.clear();

    for (Part* part : score()->parts()) {
        if (part->isSharedPart() && !toSharedPart(part)->enabled()) {
            continue;
        }
        if (part->sharedPart() && part->sharedPart()->enabled()) {
            continue;
        }
        m_rowsCache.parts.push_back(part);
    }

    return m_rowsCache.parts;
}

Part* Timeline::rowPart(int row) const
{
    const std::vector<Part*>& parts = timelineParts();
    if (row < 0 || static_cast<size_t>(row) >= parts.size()) {
        return nullptr;
    }

    return parts.at(row);
}

int Timeline::staffRow(staff_idx_t staffIdx) const
{
    if (!score() || staffIdx >= score()->staves().size()) {
        return -1;
    }

    const std::vector<Part*>& parts = timelineParts();
    const auto it = std::find(parts.begin(), parts.end(), score()->staves().at(staffIdx)->part());
    return it != parts.end() ? static_cast<int>(std::distance(parts.begin(), it)) : -1;
}

staff_idx_t Timeline::rowFirstStaff(int row) const
{
    const Part* part = rowPart(row);
    return part && !part->staves().empty() ? part->staves().front()->idx() : 0;
}

staff_idx_t Timeline::rowLastStaff(int row) const
{
    const Part* part = rowPart(row);
    return part && !part->staves().empty() ? part->staves().back()->idx() : 0;
}

//---------------------------------------------------------
//   clearScene
//---------------------------------------------------------

void Timeline::clearScene()
{
    scene()->clear();

    // clear pointers to scene items, they have been deleted by clear()
    nonVisiblePathItem = nullptr;
    visiblePathItem = nullptr;
    selectionItem = nullptr;
    m_playbackCursorItem = nullptr;
}

//---------------------------------------------------------
//   Timeline::changeSelection
//---------------------------------------------------------

void Timeline::changeSelection(SelState)
{
    TRACEFUNC;

    scene()->blockSignals(true);
    scene()->clearSelection();

    QRectF selectionRect = _selectionPath.boundingRect();
    if (selectionRect == QRectF()) {
        return;
    }

    int nmeta = nmetas();

    // Get borders of the current viewport
    int leftBorder = horizontalScrollBar()->value();
    int rightBorder = horizontalScrollBar()->value() + viewport()->width();
    int topBorder = verticalScrollBar()->value() + nmeta * _gridHeight;
    int bottomBorder = verticalScrollBar()->value() + viewport()->height();

    bool selectionExtendsUp = false,    selectionExtendsLeft = false;
    bool selectionExtendsRight = false, selectionExtendsDown = false;

    // Figure out which directions the selection extends
    if (selectionRect.top() < topBorder) {
        selectionExtendsUp = true;
    }
    if (selectionRect.left() < leftBorder - 1) {
        selectionExtendsLeft = true;
    }
    if (selectionRect.right() > rightBorder) {
        selectionExtendsRight = true;
    }
    if (selectionRect.bottom() > bottomBorder) {
        selectionExtendsDown = true;
    }

    if (selectionExtendsDown
        && _oldSelectionRect.bottom() != selectionRect.bottom()
        && !_metaValue) {
        int newScrollbarValue = int(verticalScrollBar()->value() + selectionRect.bottom() - bottomBorder);
        verticalScrollBar()->setValue(newScrollbarValue);
    } else if (selectionExtendsUp
               && !selectionExtendsDown
               && _oldSelectionRect.bottom() != selectionRect.bottom()
               && !_metaValue
               && _oldSelectionRect.contains(selectionRect)) {
        int newScrollbarValue = int(verticalScrollBar()->value() + selectionRect.bottom() - bottomBorder);
        verticalScrollBar()->setValue(newScrollbarValue);
    }

    if (selectionExtendsRight
        && _oldSelectionRect.right() != selectionRect.right()) {
        int newScrollbarValue = int(horizontalScrollBar()->value() + selectionRect.right() - rightBorder);
        horizontalScrollBar()->setValue(newScrollbarValue);
    }
    if (selectionExtendsUp
        && _oldSelectionRect.top() != selectionRect.top()
        && !_metaValue) {
        int newScrollbarValue = int(selectionRect.top()) - nmeta * _gridHeight;
        verticalScrollBar()->setValue(newScrollbarValue);
    }
    if (selectionExtendsLeft
        && _oldSelectionRect.left() != selectionRect.left()) {
        int newScrollbarValue = int(selectionRect.left());
        horizontalScrollBar()->setValue(newScrollbarValue);
    }

    if (selectionExtendsLeft
        && !selectionExtendsRight
        && _oldSelectionRect.right() != selectionRect.right()
        && _oldSelectionRect.contains(selectionRect)) {
        int newScrollbarValue = int(horizontalScrollBar()->value() + selectionRect.right() - rightBorder);
        horizontalScrollBar()->setValue(newScrollbarValue);
    }
    if (selectionExtendsRight
        && !selectionExtendsLeft
        && _oldSelectionRect.left() != selectionRect.left()
        && _oldSelectionRect.contains(selectionRect)) {
        int newScrollbarValue = int(selectionRect.left());
        horizontalScrollBar()->setValue(newScrollbarValue);
    }

    if (selectionExtendsDown
        && !selectionExtendsUp
        && _oldSelectionRect.top() != selectionRect.top()
        && !_metaValue
        && _oldSelectionRect.contains(selectionRect)) {
        int newScrollbarValue = int(selectionRect.top()) - nmeta * _gridHeight;
        verticalScrollBar()->setValue(newScrollbarValue);
    }

    _oldSelectionRect = selectionRect;

    _metaValue = false;
    scene()->blockSignals(false);
}

//---------------------------------------------------------
//   Timeline::drawSelection
//---------------------------------------------------------

void Timeline::drawSelection()
{
    if (!score()) {
        return;
    }

    TRACEFUNC;

    _selectionPath = QPainterPath();
    _selectionPath.setFillRule(Qt::WindingFill);

    std::set<std::tuple<Measure*, int, ElementType> > metaLabelsSet;

    INotationSelectionPtr selection = interaction()->selection();

    for (EngravingItem* element : selection->elements()) {
        if (element->tick() == Fraction(-1, 1)) {
            continue;
        } else {
            switch (element->type()) {
            case ElementType::INSTRUMENT_NAME:
            case ElementType::VBOX:
            case ElementType::HBOX:
            case ElementType::TEXT:
            case ElementType::TIE_SEGMENT:
            case ElementType::SLUR_SEGMENT:
            case ElementType::TIE:
            case ElementType::SLUR:
            case ElementType::HAMMER_ON_PULL_OFF:
                continue;
                break;
            default: break;
            }
        }

        int staffIdx;
        Fraction tick = element->tick();
        Measure* measure = score()->tick2measure(tick);
        staffIdx = static_cast<int>(element->staffIdx());
        if (numToStaff(staffIdx) && !numToStaff(staffIdx)->show()) {
            continue;
        }
        //! NOTE: an element on a staff without a row (e.g. a part combined by stave
        //! sharing) is skipped, unless it's a meta element (set to -1 below): -1 is also
        //! what identifies the meta rows' items, so keeping it would highlight those
        staffIdx = staffRow(element->staffIdx());
        const bool hasRow = staffIdx >= 0;
        bool isMetaElement = false;

        if ((element->isTempoText()
             || element->isKeySig()
             || element->isTimeSig()
             || element->isRehearsalMark()
             || element->isJump()
             || element->isMarker())
            && !element->generated()) {
            staffIdx = -1;
            isMetaElement = true;
        }

        if (element->isBarLine()) {
            staffIdx = -1;
            isMetaElement = true;
            BarLine* barline = toBarLine(element);
            if (barline
                && (barline->barLineType() == BarLineType::END_REPEAT
                    || barline->barLineType() == BarLineType::END
                    || barline->barLineType() == BarLineType::DOUBLE
                    || barline->barLineType() == BarLineType::REVERSE_END
                    || barline->barLineType() == BarLineType::HEAVY
                    || barline->barLineType() == BarLineType::DOUBLE_HEAVY)
                && measure != score()->lastMeasure()) {
                if (measure->prevMeasure()) {
                    measure = measure->prevMeasure();
                }
            }
        }

        if (!hasRow && !isMetaElement) {
            continue;
        }

        // element->type() for meta rows, invalid for everything else
        ElementType elementType = (staffIdx == -1) ? element->type() : ElementType::INVALID;

        // If has a multi measure rest, find the count and add each measure to it
        // ws: If style flag Sid::createMultiMeasureRests is not set, then
        // measure->mmRest() is not valid

        if (measure->mmRest() && measure->score()->style().styleB(Sid::createMultiMeasureRests)) {
            int mmrestCount = measure->mmRest()->mmRestCount();
            Measure* tmpMeasure = measure;
            for (int mmrestMeasure = 0; mmrestMeasure < mmrestCount; mmrestMeasure++) {
                std::tuple<Measure*, int, ElementType> tmp(tmpMeasure, staffIdx, elementType);
                metaLabelsSet.insert(tmp);
                tmpMeasure = tmpMeasure->nextMeasure();
            }
        } else {
            std::tuple<Measure*, int, ElementType> tmp(measure, staffIdx, elementType);
            metaLabelsSet.insert(tmp);
        }
    }

    const QList<QGraphicsItem*> graphicsItemList = scene()->items();
    for (QGraphicsItem* graphicsItem : graphicsItemList) {
        int stave = graphicsItem->data(0).value<int>();
        ElementType elementType = graphicsItem->data(1).value<ElementType>();
        Measure* measure = static_cast<Measure*>(graphicsItem->data(2).value<void*>());

        std::tuple<Measure*, int, ElementType> targetTuple(measure, stave, elementType);
        std::set<std::tuple<Measure*, int, ElementType> >::iterator it;
        it = metaLabelsSet.find(targetTuple);

        if (stave == -1 && it != metaLabelsSet.end()) {
            //Make sure the element is correct
            const std::vector<EngravingItem*>& elementList = interaction()->selection()->elements();
            EngravingItem* targetElement = static_cast<EngravingItem*>(graphicsItem->data(4).value<void*>());
            Segment* seg = static_cast<Segment*>(graphicsItem->data(6).value<void*>());

            if (targetElement) {
                for (EngravingItem* element : elementList) {
                    if (element == targetElement) {
                        QGraphicsRectItem* graphicsRectItem = qgraphicsitem_cast<QGraphicsRectItem*>(graphicsItem);
                        if (graphicsRectItem) {
                            graphicsRectItem->setBrush(QBrush(activeTheme().selectionColor));
                        }
                    }
                }
            } else if (seg) {
                for (EngravingItem* element : elementList) {
                    QGraphicsRectItem* graphicsRectItem = qgraphicsitem_cast<QGraphicsRectItem*>(graphicsItem);
                    if (graphicsRectItem) {
                        for (size_t track = 0; track < score()->nstaves() * VOICES; track++) {
                            if (element == seg->element(track)) {
                                graphicsRectItem->setBrush(QBrush(activeTheme().selectionColor));
                            }
                        }
                    }
                }
            } else {
                QGraphicsRectItem* graphicsRectItem = qgraphicsitem_cast<QGraphicsRectItem*>(graphicsItem);
                if (graphicsRectItem) {
                    graphicsRectItem->setBrush(QBrush(activeTheme().selectionColor));
                }
            }
        }
        // Change color from gray to only blue
        else if (it != metaLabelsSet.end()) {
            QGraphicsRectItem* graphicsRectItem = qgraphicsitem_cast<QGraphicsRectItem*>(graphicsItem);
            graphicsRectItem->setBrush(QBrush(QColor(graphicsRectItem->brush().color().red(),
                                                     graphicsRectItem->brush().color().green(),
                                                     255)));
            _selectionPath.addRect(graphicsRectItem->rect());
        } else {
            // Ensure unselected measures are not marked selected
            QGraphicsRectItem* graphicsRectItem = qgraphicsitem_cast<QGraphicsRectItem*>(graphicsItem);
            if (graphicsRectItem && graphicsRectItem->data(keyItemType).value<ItemType>() == ItemType::TYPE_MEASURE) {
                graphicsRectItem->setBrush(QBrush(colorBox(graphicsRectItem)));
            }
        }
    }

    if (selectionItem) {
        scene()->removeItem(selectionItem);
        delete selectionItem;
        selectionItem = nullptr;
    }

    selectionItem = new QGraphicsPathItem(_selectionPath.simplified());
    if (selection->isRange()) {
        selectionItem->setPen(QPen(QColor(0, 0, 255), 3));
    } else {
        selectionItem->setPen(QPen(QColor(0, 0, 0), 1));
    }

    selectionItem->setBrush(Qt::NoBrush);
    selectionItem->setZValue(-1);
    scene()->addItem(selectionItem);

    if (std::get<0>(_oldHoverInfo)) {
        std::get<0>(_oldHoverInfo) = nullptr;
        std::get<1>(_oldHoverInfo) = -1;
    }
}

//---------------------------------------------------------
//   Timeline::mousePressEvent
//---------------------------------------------------------

void Timeline::mousePressEvent(QMouseEvent* event)
{
    if (!score()) {
        return;
    }

    if (event->button() == Qt::RightButton) {
        return;
    }

    TRACEFUNC;

    // Set as clicked
    _mousePressed = true;
    scene()->clearSelection();

    QPointF scenePt = mapToScene(event->pos());
    // Set as old location
    _oldLoc = QPoint(int(scenePt.x()), int(scenePt.y()));
    QList<QGraphicsItem*> graphicsItemList = scene()->items(scenePt);
    // Find highest z value for rect
    int maxZValue = -4;
    QGraphicsItem* currGraphicsItem = nullptr;
    for (QGraphicsItem* graphicsItem : graphicsItemList) {
        QGraphicsRectItem* graphicsRectItem = qgraphicsitem_cast<QGraphicsRectItem*>(graphicsItem);
        if (graphicsRectItem && graphicsItem->zValue() > maxZValue) {
            currGraphicsItem = graphicsItem;
            maxZValue = graphicsItem->zValue();
        }
    }
    if (currGraphicsItem) {
        //! NOTE: an instrument row (= part, see getParts()), not a staff
        int stave = currGraphicsItem->data(0).value<int>();
        Measure* currMeasure = static_cast<Measure*>(currGraphicsItem->data(2).value<void*>());
        if (rowPart(stave) && !rowPart(stave)->show()) {
            return;
        }

        if (!currMeasure) {
            int nmeta = nmetas();
            int bottomOfMeta = nmeta * _gridHeight + verticalScrollBar()->value();

            // Handle measure box clicks
            if (isOnMeasuresRow(scenePt)) {
                //! NOTE: dragging vertically from here zooms (see mouseMoveEvent())
                _measuresZoomPressed = true;
                _zoomStartY = event->pos().y();
                _zoomStartGridWidth = _gridWidth;
                _zoomAnchorViewX = event->pos().x();
                _zoomAnchorMeasures = scenePt.x() / qreal(_gridWidth);

                QRectF tmp(scenePt.x(), 0, 3, nmeta * _gridHeight + nstaves() * _gridHeight);
                QList<QGraphicsItem*> gl = scene()->items(tmp);
                Measure* measure = nullptr;

                for (QGraphicsItem* graphicsItem : gl) {
                    measure = static_cast<Measure*>(graphicsItem->data(2).value<void*>());
                    //-3 z value is the grid square values
                    if (graphicsItem->zValue() == -3 && measure) {
                        break;
                    }
                }

                //! NOTE: like a plain click on an instrument cell, but for all the visible
                //! instruments: moves the playback position to the start of the measure
                if (measure) {
                    measure = measure->coveringMMRestOrThis();

                    std::vector<const Part*> visibleParts;
                    for (const Part* part : timelineParts()) {
                        if (part->show()) {
                            visibleParts.push_back(part);
                        }
                    }

                    if (EngravingItem* firstElement = firstElementInParts(measure, visibleParts)) {
                        interaction()->select({ firstElement }, SelectType::SINGLE);
                    } else {
                        interaction()->select({ measure }, SelectType::SINGLE, 0);
                        interaction()->select({ measure }, SelectType::RANGE, score()->nstaves() - 1);
                    }

                    interaction()->showItem(measure);
                    seekSelection();
                }
                return;
            }
            if (scenePt.y() < bottomOfMeta) {
                return;
            }

            QList<QGraphicsItem*> gl = items(event->pos());
            for (QGraphicsItem* graphicsItem : gl) {
                currMeasure = static_cast<Measure*>(graphicsItem->data(2).value<void*>());
                stave = graphicsItem->data(0).value<int>();
                if (currMeasure) {
                    break;
                }
            }
            if (!currMeasure) {
                interaction()->clearSelection();
                return;
            }
        }

        bool metaValueClicked = currGraphicsItem->data(3).value<bool>();

        scene()->clearSelection();
        if (metaValueClicked) {
            _metaValue = true;
            _oldSelectionRect = QRect();

            interaction()->showItem(currMeasure, 0);
            verticalScrollBar()->setValue(0);

            Segment* seg = static_cast<Segment*>(currGraphicsItem->data(6).value<void*>());

            if (seg) {
                std::vector<EngravingItem*> elements;

                for (size_t track = 0; track < score()->nstaves() * VOICES; track++) {
                    EngravingItem* element = seg->element(track);
                    if (element) {
                        elements.push_back(element);
                    }
                }

                if (elements.empty()) {
                    interaction()->clearSelection();
                } else {
                    interaction()->select(elements);
                }
            } else {
                // Also select the elements that they correspond to
                ElementType elementType = currGraphicsItem->data(1).value<ElementType>();
                SegmentType segmentType = SegmentType::Invalid;
                if (elementType == ElementType::KEYSIG) {
                    segmentType = SegmentType::KeySig;
                } else if (elementType == ElementType::TIMESIG) {
                    segmentType = SegmentType::TimeSig;
                }

                if (segmentType != SegmentType::Invalid) {
                    Segment* currSeg = currMeasure->first();
                    for (; currSeg && currSeg->segmentType() != segmentType; currSeg = currSeg->next()) {
                    }
                    if (currSeg) {
                        std::vector<EngravingItem*> elements;

                        for (size_t j = 0; j < score()->nstaves(); j++) {
                            EngravingItem* element = currSeg->firstElementForNavigation(j);
                            if (element) {
                                elements.push_back(element);
                            }
                        }

                        if (elements.empty()) {
                            interaction()->clearSelection();
                        } else {
                            interaction()->select(elements);
                        }
                    }
                } else {
                    // Select just the element for tempo_text
                    EngravingItem* element = static_cast<EngravingItem*>(currGraphicsItem->data(4).value<void*>());
                    if (element) {
                        interaction()->select({ element });
                    } else if (currMeasure) {
                        interaction()->select({ currMeasure });
                    } else {
                        interaction()->clearSelection();
                    }
                }
            }
        } else {
            // Handle cell clicks
            if (event->modifiers() == Qt::ShiftModifier) {
                //! NOTE: the multimeasure rest covering this measure, if shown (prevMeasureMM() here
                //! used to select the measure BEFORE the clicked one)
                currMeasure = currMeasure->coveringMMRestOrThis();

                if (currMeasure) {
                    interaction()->select({ currMeasure }, SelectType::RANGE, rowFirstStaff(stave));
                    interaction()->select({ currMeasure }, SelectType::RANGE, rowLastStaff(stave));
                }
            } else if (event->modifiers() == Qt::ControlModifier) {
                if (interaction()->selection()->isNone()) {
                    //! NOTE: the multimeasure rest covering this measure, if shown (prevMeasureMM() here
                    //! used to select the measure BEFORE the clicked one)
                    currMeasure = currMeasure->coveringMMRestOrThis();

                    if (currMeasure) {
                        interaction()->select({ currMeasure }, SelectType::RANGE, 0);
                        interaction()->select({ currMeasure }, SelectType::RANGE, score()->nstaves() - 1);
                    }
                } else {
                    interaction()->clearSelection();
                }
            } else {
                //! NOTE: the multimeasure rest covering this measure, if shown (prevMeasureMM() here
                //! used to select the measure BEFORE the clicked one)
                currMeasure = currMeasure->coveringMMRestOrThis();

                //! NOTE: a plain click only moves the playback position: it selects the
                //! measure's first element on this staff (playback starts from it, see
                //! seekSelection() below), not the measure itself - a range selection
                //! would make playback play that staff only. Shift+click selects measures.
                if (EngravingItem* firstElement = currMeasure ? firstElementInRow(currMeasure, stave) : nullptr) {
                    interaction()->select({ firstElement }, SelectType::SINGLE);
                } else if (currMeasure && rowPart(stave)) {
                    interaction()->select({ currMeasure }, SelectType::SINGLE, rowFirstStaff(stave));
                    interaction()->select({ currMeasure }, SelectType::RANGE, rowLastStaff(stave));
                }
            }

            if (currMeasure) {
                interaction()->showItem(currMeasure, static_cast<int>(rowFirstStaff(stave)));
            }
        }
    } else {
        interaction()->clearSelection();
    }

    this->seekSelection();
}

//! NOTE: the earliest element of the measure on any of the row's (part's) staves,
//! the top staff first at equal ticks
EngravingItem* Timeline::firstElementInRow(Measure* measure, int row) const
{
    const Part* part = rowPart(row);
    if (!part) {
        return nullptr;
    }

    return firstElementInParts(measure, { part });
}

//! NOTE: same as firstElementInRow(), on the staves of all the given parts (in score order)
EngravingItem* Timeline::firstElementInParts(Measure* measure, const std::vector<const Part*>& parts) const
{
    for (Segment* segment = measure->first(SegmentType::ChordRest); segment; segment = segment->next(SegmentType::ChordRest)) {
        for (const Part* part : parts) {
            for (const Staff* staff : part->staves()) {
                for (voice_idx_t voice = 0; voice < VOICES; ++voice) {
                    EngravingItem* element = segment->element(staff->idx() * VOICES + voice);
                    if (!element) {
                        continue;
                    }

                    //! NOTE: a chord is selected through its notes, like clicking it in the score
                    if (element->isChord()) {
                        return toChord(element)->upNote();
                    }

                    return element;
                }
            }
        }
    }

    return nullptr;
}

void Timeline::seekSelection()
{
    const INotationSelectionPtr selection = interaction()->selection();
    const std::vector<EngravingItem*>& elements = selection->elements();
    if (elements.empty()) {
        return;
    }

    EngravingItem* elementToSeek = elements.front();
    for (EngravingItem* element : elements) {
        if (element->tick() > elementToSeek->tick()) {
            continue;
        }
        elementToSeek = element;
    }

    playbackController()->seekElement(elementToSeek);
}

bool Timeline::isOnMeasuresRow(const QPointF& scenePt) const
{
    if (!measuresRowVisible()) {
        return false;
    }

    int nmeta = nmetas();
    int top = (nmeta - 1) * _gridHeight + verticalScrollBar()->value();
    return scenePt.y() > top && scenePt.y() < top + _gridHeight;
}

//! NOTE: sets the measure width, keeping the point anchorMeasures (in measures from the
//! score start) at the viewport x anchorViewX
void Timeline::zoomAround(int gridWidth, int anchorViewX, qreal anchorMeasures)
{
    gridWidth = std::clamp(gridWidth, _minZoom, _maxZoom);
    if (gridWidth == _gridWidth) {
        return;
    }

    _gridWidth = gridWidth;
    updateGridFull();

    qreal anchorSceneX = mapToScene(QPoint(anchorViewX, 0)).x();
    qreal targetSceneX = anchorMeasures * _gridWidth;
    horizontalScrollBar()->setValue(horizontalScrollBar()->value() + qRound(targetSceneX - anchorSceneX));
}

//---------------------------------------------------------
//   Timeline::mouseMoveEvent
//---------------------------------------------------------

void Timeline::mouseMoveEvent(QMouseEvent* event)
{
    //! NOTE: the release can be lost on the way through the QML adapter (TimelineView):
    //! a move without the left button means the drag is over
    if (_mousePressed && !event->buttons().testFlag(Qt::LeftButton)) {
        mouseReleaseEvent(event);
    }

    QPointF newLoc = mapToScene(event->pos());
    if (!_mousePressed) {
        if (cursorIsOn(event->pos()) == "meta") {
            setCursor(Qt::ArrowCursor);
            mouseOver(newLoc);
        } else if (cursorIsOn(event->pos()) == "invalid") {
            setCursor(Qt::ForbiddenCursor);
        } else {
            setCursor(Qt::ArrowCursor);
        }

        emit moved(QPointF(-1, -1));
        return;
    }

    if (state == ViewState::NORMAL && _measuresZoomPressed) {
        if (std::abs(event->pos().y() - _zoomStartY) <= 2) {
            return;
        }
        state = ViewState::ZOOM;
        setCursor(Qt::SizeVerCursor);
    }

    if (state == ViewState::ZOOM) {
        //! NOTE: up zooms in, down zooms out; doubles/halves every 100 px
        int dy = _zoomStartY - event->pos().y();
        zoomAround(qRound(_zoomStartGridWidth * std::pow(2.0, dy / 100.0)), _zoomAnchorViewX, _zoomAnchorMeasures);
        return;
    }

    if (state == ViewState::NORMAL) {
        if (event->modifiers() == Qt::ShiftModifier) {
            // Slight wiggle room for selection (Same as score)
            if (std::abs(newLoc.x() - _oldLoc.x()) > 2
                || std::abs(newLoc.y() - _oldLoc.y()) > 2) {
                interaction()->clearSelection();
                updateGrid();
                state = ViewState::LASSO;
                _selectionBox = new QGraphicsRectItem();
                _selectionBox->setRect(_oldLoc.x(), _oldLoc.y(), 0, 0);
                _selectionBox->setPen(QPen(QColor(0, 0, 255), 2));
                _selectionBox->setBrush(QBrush(QColor(0, 0, 255, 50)));
                scene()->addItem(_selectionBox);
            }
        } else {
            state = ViewState::DRAG;
            setCursor(Qt::SizeAllCursor);
        }
    }

    if (state == ViewState::LASSO) {
        QRect tmp = QRect((_oldLoc.x() < newLoc.x()) ? _oldLoc.x() : newLoc.x(),
                          (_oldLoc.y() < newLoc.y()) ? _oldLoc.y() : newLoc.y(),
                          std::abs(newLoc.x() - _oldLoc.x()),
                          std::abs(newLoc.y() - _oldLoc.y()));
        _selectionBox->setRect(tmp);
    } else if (state == ViewState::DRAG) {
        int x_offset = int(_oldLoc.x()) - int(newLoc.x());
        int yOffset = int(_oldLoc.y()) - int(newLoc.y());
        horizontalScrollBar()->setValue(horizontalScrollBar()->value() + x_offset);
        verticalScrollBar()->setValue(verticalScrollBar()->value() + yOffset);
    }

    emit moved(QPointF(-1, -1));
}

//---------------------------------------------------------
//   Timeline::mouseReleaseEvent
//---------------------------------------------------------

void Timeline::mouseReleaseEvent(QMouseEvent*)
{
    _mousePressed = false;
    _measuresZoomPressed = false;

    if (state == ViewState::LASSO) {
        scene()->removeItem(_selectionBox);
        interaction()->clearSelection();

        int width, height;
        QPoint loc = mapFromScene(_selectionBox->rect().topLeft());
        width = int(_selectionBox->rect().width());
        height = int(_selectionBox->rect().height());

        QList<QGraphicsItem*> graphicsItemList = items(QRect(loc.x(), loc.y(), width, height));
        // Find top left and bottom right to create selection
        QGraphicsItem* tlGraphicsItem = nullptr;
        QGraphicsItem* brGraphicsItem = nullptr;
        for (QGraphicsItem* graphicsItem : graphicsItemList) {
            Measure* currMeasure = static_cast<Measure*>(graphicsItem->data(2).value<void*>());
            if (!currMeasure) {
                continue;
            }
            int stave = graphicsItem->data(0).value<int>();
            if (stave == -1) {
                continue;
            }

            if (!tlGraphicsItem && !brGraphicsItem) {
                tlGraphicsItem = graphicsItem;
                brGraphicsItem = graphicsItem;
                continue;
            }

            if (graphicsItem->boundingRect().top() < tlGraphicsItem->boundingRect().top()) {
                tlGraphicsItem = graphicsItem;
            }
            if (graphicsItem->boundingRect().left() < tlGraphicsItem->boundingRect().left()) {
                tlGraphicsItem = graphicsItem;
            }

            if (graphicsItem->boundingRect().bottom() > brGraphicsItem->boundingRect().bottom()) {
                brGraphicsItem = graphicsItem;
            }
            if (graphicsItem->boundingRect().right() > brGraphicsItem->boundingRect().right()) {
                brGraphicsItem = graphicsItem;
            }
        }

        // Select single tlGraphicsItem and then range brGraphicsItem
        if (tlGraphicsItem && brGraphicsItem) {
            Measure* tlMeasure = static_cast<Measure*>(tlGraphicsItem->data(2).value<void*>());
            int tlStave = static_cast<int>(rowFirstStaff(tlGraphicsItem->data(0).value<int>()));
            Measure* brMeasure = static_cast<Measure*>(brGraphicsItem->data(2).value<void*>());
            int brStave = static_cast<int>(rowLastStaff(brGraphicsItem->data(0).value<int>()));
            if (tlMeasure && brMeasure) {
                // Focus selection of mmRests here (see mousePressEvent())
                tlMeasure = tlMeasure->coveringMMRestOrThis();
                brMeasure = brMeasure->coveringMMRestOrThis();

                if (tlMeasure) {
                    interaction()->select({ tlMeasure }, SelectType::SINGLE, tlStave);
                }

                if (brMeasure) {
                    interaction()->select({ brMeasure }, SelectType::RANGE, brStave);
                }
            }

            if (tlMeasure) {
                interaction()->showItem(tlMeasure, tlStave);
            }
        }
    } else if (state == ViewState::DRAG || state == ViewState::ZOOM) {
        setCursor(Qt::ArrowCursor);
    }
    state = ViewState::NORMAL;
}

//---------------------------------------------------------
//   Timeline::leaveEvent
//---------------------------------------------------------

void Timeline::leaveEvent(QEvent*)
{
    if (!viewport()->rect().contains(viewport()->mapFromGlobal(QCursor::pos()))) {
        QPointF p = mapToScene(viewport()->mapFromGlobal(QCursor::pos()));
        mouseOver(p);
    }
}

//---------------------------------------------------------
//   Timeline::wheelEvent
//---------------------------------------------------------

void Timeline::wheelEvent(QWheelEvent* event)
{
    QPointF scenePt = mapToScene(event->position().toPoint());
    const bool zoomOnMeasuresRow = event->modifiers() == Qt::NoModifier && isOnMeasuresRow(scenePt);
    if (zoomOnMeasuresRow || event->modifiers().testFlag(Qt::ControlModifier)) {
        //! NOTE: like dragging vertically on the Measures row (see mouseMoveEvent()): up zooms in,
        //! doubles/halves every 4 wheel notches. Small (trackpad) deltas add up until they change the width.
        //! Ctrl+wheel anywhere too (it used to change the width by 1 px a notch, far too slow up to _maxZoom).
        _wheelZoomDelta += event->angleDelta().y();
        int gridWidth = qRound(_gridWidth * std::pow(2.0, _wheelZoomDelta / 480.0));
        gridWidth = std::clamp(gridWidth, _minZoom, _maxZoom);
        if (gridWidth != _gridWidth) {
            _wheelZoomDelta = 0;
            zoomAround(gridWidth, event->position().toPoint().x(), scenePt.x() / qreal(_gridWidth));
        } else if ((gridWidth == _maxZoom && _wheelZoomDelta > 0) || (gridWidth == _minZoom && _wheelZoomDelta < 0)) {
            _wheelZoomDelta = 0; // don't pile up past the limits
        }
        event->accept();
        return;
    }

    if (event->modifiers().testFlag(Qt::ShiftModifier)) {
        qreal numOfSteps = qreal(event->angleDelta().y()) / 2;
        horizontalScrollBar()->setValue(horizontalScrollBar()->value() - int(numOfSteps));
    } else {
        QGraphicsView::wheelEvent(event);
    }
}

//---------------------------------------------------------
//   showEvent
//---------------------------------------------------------

void Timeline::showEvent(QShowEvent* evt)
{
    QGraphicsView::showEvent(evt);
    if (!evt->spontaneous()) {
        setNotation(m_notation);
    }
}

//---------------------------------------------------------
//   changeEvent
//---------------------------------------------------------

void Timeline::changeEvent(QEvent* event)
{
    QGraphicsView::changeEvent(event);
    if (event->type() == QEvent::LanguageChange) {
        initMetas();

        updateGridFull();
    }
}

//---------------------------------------------------------
//   Timeline::updateGrid
//---------------------------------------------------------

void Timeline::updateGrid(int startMeasure, int endMeasure)
{
    TRACEFUNC;

    if (score() && score()->firstMeasure()) {
        drawGrid(static_cast<int>(nstaves()), static_cast<int>(score()->nmeasures()), startMeasure, endMeasure);
        updateView();
        drawSelection();
        mouseOver(mapToScene(viewport()->mapFromGlobal(QCursor::pos())));
        _rowNames->updateLabels(getLabels(), _gridHeight);
        updatePlaybackCursor();
    }
    viewport()->update();
}

//---------------------------------------------------------
//   updateGridFromCmdState
//---------------------------------------------------------

void Timeline::updateGridFromCmdState()
{
    if (!score()) {
        updateGridFull();
        return;
    }

    const CmdState& cState = score()->cmdState();

    const bool layoutChanged = cState.layoutRange();

    if (!layoutChanged) {
        updateGridView();
        return;
    }

    const bool layoutAll = layoutChanged && (cState.startTick() < Fraction(0, 1) || cState.endTick() < Fraction(0, 1));

    const Measure* startMeasure = layoutAll ? nullptr : score()->tick2measure(cState.startTick());
    const int startMeasureIndex = startMeasure ? startMeasure->measureIndex() : 0;

    const Measure* endMeasure = layoutAll ? nullptr : score()->tick2measure(cState.endTick());
    const int endMeasureIndex = endMeasure ? (endMeasure->measureIndex() + 1) : static_cast<int>(score()->nmeasures());

    updateGrid(startMeasureIndex, endMeasureIndex);
}

//---------------------------------------------------------
//   Timeline::setNotation
//---------------------------------------------------------

void Timeline::setNotation(INotationPtr notation)
{
    if (m_audioSettings) {
        m_audioSettings->settingsChanged().disconnect(this);
    }
    if (m_notation && m_notation->soloMuteState()) {
        m_notation->soloMuteState()->trackSoloMuteStateChanged().disconnect(this);
    }
    if (m_videoSettings) {
        m_videoSettings->settingsChanged().disconnect(this);
    }

    m_notation = notation;

    m_videoSettings = videoSettings();
    if (m_videoSettings) {
        m_videoSettings->settingsChanged().onNotify(this, [this]() {
            onVideoSettingsChanged();
        });
    }
    m_videoPath = hasVideo() ? m_videoSettings->attachment().path : muse::io::path_t();
    m_videoThumbnails->setVideo(m_videoPath);
    updateVideoBand();

    //! NOTE: keeps the instrument rows' Mute/Solo buttons in sync with the Mixer
    if (m_notation && m_notation->soloMuteState()) {
        m_notation->soloMuteState()->trackSoloMuteStateChanged().onReceive(this, [this](const InstrumentTrackId&, const SoloMuteState&) {
            scheduleLabelsUpdate();
        });
    }

    m_audioSettings = audioSettings();
    m_trackColors = trackColorsSnapshot();
    if (m_audioSettings) {
        m_audioSettings->settingsChanged().onNotify(this, [this]() {
            std::vector<QColor> colors = trackColorsSnapshot();
            if (colors != m_trackColors) {
                m_trackColors = std::move(colors);
                updateGrid();
            }
        });
    }

    clearScene();

    if (m_notation) {
        drawGrid(nstaves(), static_cast<int>(score()->nmeasures()));
        drawSelection();
        changeSelection(SelState::NONE);
        _rowNames->updateLabels(getLabels(), _gridHeight);
        updatePlaybackCursor();
    } else {
        // Clear timeline if no score is present
        if (_splitter && _splitter->count() > 0) {
            TRowLabels* tRowLabels = static_cast<TRowLabels*>(_splitter->widget(0));
            std::vector<std::pair<QString, bool> > noLabels;
            tRowLabels->updateLabels(noLabels, 0);
        }
        _metaRows.clear();
        setSceneRect(0, 0, 0, 0);
    }
}

//---------------------------------------------------------
//   Timeline::updateView
//---------------------------------------------------------

void Timeline::updateView()
{
    if (!score()) {
        return;
    }

    TRACEFUNC;

    //! FIXME
    RectF canvas;        // = QRectF(_cv->matrix().inverted().mapRect(_cv->geometry()));

    // Find visible elements in timeline
    QPainterPath visiblePainterPath = QPainterPath();
    visiblePainterPath.setFillRule(Qt::WindingFill);

    // Find visible measures of score
    int measureIndex = 0;
    const int numMetas = nmetas();

    for (Measure* currMeasure = score()->firstMeasure(); currMeasure; currMeasure = currMeasure->nextMeasure(), ++measureIndex) {
        System* system = currMeasure->system();

        if (currMeasure->mmRest() && score()->style().styleB(Sid::createMultiMeasureRests)) {
            // Handle mmRests
            Measure* mmrestMeasure = currMeasure->mmRest();
            system = mmrestMeasure->system();
            if (!system) {
                measureIndex += currMeasure->mmRestCount();
                continue;
            }

            // Add all measures within mmRest to visibleItemsSet if mmRest_visible
            for (; currMeasure != mmrestMeasure->mmRestLast(); currMeasure = currMeasure->nextMeasure(), ++measureIndex) {
                for (size_t staff = 0; staff < score()->staves().size(); staff++) {
                    if (!score()->staff(staff)->show()) {
                        continue;
                    }
                    RectF staveRect = RectF(system->canvasBoundingRect().left(),
                                            system->staffCanvasYpage(staff),
                                            system->width(),
                                            system->staff(staff)->bbox().height());
                    RectF showRect = mmrestMeasure->canvasBoundingRect().intersected(staveRect);

                    if (canvas.intersects(showRect)) {
                        if (const int row = staffRow(staff); row >= 0) {
                            visiblePainterPath.addRect(getMeasureRect(measureIndex, row, numMetas));
                        }
                    }
                }
            }

            // Handle last measure in mmRest
            for (size_t staff = 0; staff < score()->staves().size(); staff++) {
                if (!score()->staff(staff)->show()) {
                    continue;
                }
                RectF staveRect = RectF(system->canvasBoundingRect().left(),
                                        system->staffCanvasYpage(staff),
                                        system->width(),
                                        system->staff(staff)->bbox().height());
                RectF showRect = mmrestMeasure->canvasBoundingRect().intersected(staveRect);

                if (canvas.intersects(showRect)) {
                    if (const int row = staffRow(staff); row >= 0) {
                        visiblePainterPath.addRect(getMeasureRect(measureIndex, row, numMetas));
                    }
                }
            }
            continue;
        }

        if (!system) {
            continue;
        }

        for (size_t staff = 0; staff < score()->staves().size(); staff++) {
            if (!score()->staff(staff)->show()) {
                continue;
            }
            RectF staveRect = RectF(system->canvasBoundingRect().left(),
                                    system->staffCanvasYpage(staff),
                                    system->width(),
                                    system->staff(staff)->bbox().height());
            RectF showRect = currMeasure->canvasBoundingRect().intersected(staveRect);

            if (canvas.intersects(showRect)) {
                if (const int row = staffRow(staff); row >= 0) {
                    visiblePainterPath.addRect(getMeasureRect(measureIndex, row, numMetas));
                }
            }
        }
    }

    if (nonVisiblePathItem) {
        scene()->removeItem(nonVisiblePathItem);
        delete nonVisiblePathItem;
        nonVisiblePathItem = nullptr;
    }
    if (visiblePathItem) {
        scene()->removeItem(visiblePathItem);
        delete visiblePathItem;
        visiblePathItem = nullptr;
    }

    QPainterPath nonVisiblePainterPath = QPainterPath();
    nonVisiblePainterPath.setFillRule(Qt::WindingFill);

    QRectF timelineRect = QRectF(0, 0, getWidth(), getHeight());
    nonVisiblePainterPath.addRect(timelineRect);

    nonVisiblePainterPath = nonVisiblePainterPath.subtracted(visiblePainterPath);

    nonVisiblePathItem = new QGraphicsPathItem(nonVisiblePainterPath.simplified());

    QPen nonVisiblePen = QPen(activeTheme().nonVisiblePenColor);
    QBrush nonVisibleBrush = QBrush(activeTheme().nonVisibleBrushColor);
    nonVisiblePathItem->setPen(QPen(nonVisibleBrush.color()));
    nonVisiblePathItem->setBrush(nonVisibleBrush);
    nonVisiblePathItem->setZValue(-3);

    visiblePathItem = new QGraphicsPathItem(visiblePainterPath.simplified());
    visiblePathItem->setPen(nonVisiblePen);
    visiblePathItem->setBrush(Qt::NoBrush);
    visiblePathItem->setZValue(-2);

    scene()->addItem(nonVisiblePathItem);
    scene()->addItem(visiblePathItem);
}

//---------------------------------------------------------
//   Timeline::nstaves
//---------------------------------------------------------

//! NOTE: the number of instrument ROWS (one per part, see getParts())
int Timeline::nstaves() const
{
    return static_cast<int>(timelineParts().size());
}

//---------------------------------------------------------
//   Timeline::colorBox
//---------------------------------------------------------

QColor Timeline::colorBox(QGraphicsRectItem* item)
{
    Measure* measure = static_cast<Measure*>(item->data(2).value<void*>());
    const int row = item->data(0).value<int>();
    const Part* part = rowPart(row);
    if (!part) {
        return QColor(224, 224, 224);
    }

    for (Segment* seg = measure->first(); seg; seg = seg->next()) {
        if (!seg->isChordRestType()) {
            continue;
        }
        for (const Staff* staff : part->staves()) {
            const track_idx_t startTrack = staff->idx() * VOICES;
            for (track_idx_t track = startTrack; track < startTrack + VOICES; track++) {
                ChordRest* chordRest = seg->cr(track);
                if (chordRest) {
                    ElementType crt = chordRest->type();
                    if (crt == ElementType::CHORD || crt == ElementType::MEASURE_REPEAT) {
                        return trackColor(row, measure->tick());
                    }
                }
            }
        }
    }
    return QColor(224, 224, 224);
}

//---------------------------------------------------------
//   Timeline::trackColor
//---------------------------------------------------------

//! NOTE: the color the Mixer shows for the instrument track playing this row at this
//! tick (a part with instrument changes has one track per instrument; a stave sharing
//! combined part shows its first origin part's): its custom color if one was picked in
//! the Mixer, otherwise the theme's accent color, the same fallback the Mixer itself uses.
QColor Timeline::trackColor(int row, const Fraction& tick) const
{
    const std::vector<const Part*> parts = rowAudioParts(row);
    return parts.empty() ? m_defaultTrackColor : partTrackColor(parts.front(), tick);
}

QColor Timeline::partTrackColor(const Part* part, const Fraction& tick) const
{
    const Instrument* instrument = part ? part->instrument(tick) : nullptr;
    const project::IProjectAudioSettingsPtr audio = audioSettings();
    if (!instrument || !audio) {
        return m_defaultTrackColor;
    }

    const InstrumentTrackId trackId { part->id(), instrument->id() };
    if (audio->trackHasExistingOutputParams(trackId)) {
        const QColor color = audio->trackOutputParams(trackId).color;
        if (color.isValid()) {
            return color;
        }
    }

    return m_defaultTrackColor;
}

//! NOTE: snapshot of every instrument track's color, so a project audio settings change
//! (fired on any Mixer change, e.g. every step of a fader drag) only redraws the
//! Timeline when a color actually changed
std::vector<QColor> Timeline::trackColorsSnapshot() const
{
    std::vector<QColor> colors;
    if (!score()) {
        return colors;
    }

    //! NOTE: every part, rows or not (a combined part's row shows its origin parts' color)
    for (const Part* part : score()->parts()) {
        for (const auto& [tick, instrument] : part->instruments()) {
            colors.push_back(partTrackColor(part, Fraction::fromTicks(tick)));
        }
    }

    return colors;
}

//---------------------------------------------------------
//   Timeline playback cursor
//---------------------------------------------------------

//! NOTE: a vertical line at the playback position, following the same position and
//! the same seconds -> tick conversion as the score's own playback cursor (see
//! AbstractNotationPaintView): INotationPlayback::secToTick() already resolves repeats,
//! jumps and loops back to the right score tick. While stopped it stays at the position
//! playback will start from, which clicking the score or the Timeline moves (both seek).
void Timeline::initPlaybackCursor()
{
    m_elapsedTimer.start();

    //! NOTE: same interpolation rate as the score's cursor
    m_playbackCursorTimer.setInterval(23);
    connect(&m_playbackCursorTimer, &QTimer::timeout, this, [this]() {
        updatePlaybackCursor();
    });

    m_lastPlaybackPosition = globalContext()->playbackState()->playbackPosition();

    globalContext()->playbackState()->playbackPositionChanged().onReceive(this, [this](muse::audio::secs_t secs) {
        m_lastPlaybackPosition = secs;
        m_lastPlaybackPositionUpdateTimeNs = m_elapsedTimer.nsecsElapsed();

        if (!m_playbackCursorTimer.isActive()) {
            updatePlaybackCursor();
        }
    });

    globalContext()->playbackState()->playbackStatusChanged().onReceive(this, [this](muse::audio::PlaybackStatus) {
        onPlaybackStatusChanged();
    });
}

void Timeline::onPlaybackStatusChanged()
{
    m_lastPlaybackPosition = globalContext()->playbackState()->playbackPosition();
    m_lastPlaybackPositionUpdateTimeNs = m_elapsedTimer.nsecsElapsed();

    if (globalContext()->playbackState()->isPlaying()) {
        m_playbackCursorTimer.start();
    } else {
        m_playbackCursorTimer.stop();
    }

    updatePlaybackCursor();
}

qreal Timeline::playbackCursorX(int tick) const
{
    if (m_measureStartTicks.empty() || !score()) {
        return 0.0;
    }

    // Measure containing tick: the last one starting at or before it
    auto it = std::upper_bound(m_measureStartTicks.begin(), m_measureStartTicks.end(), tick);
    const size_t index = it == m_measureStartTicks.begin() ? 0 : static_cast<size_t>(std::distance(m_measureStartTicks.begin(), it) - 1);

    const int startTick = m_measureStartTicks.at(index);
    const int endTick = index + 1 < m_measureStartTicks.size() ? m_measureStartTicks.at(index + 1) : score()->endTick().ticks();
    const int duration = endTick - startTick;
    const qreal fraction = duration > 0 ? std::clamp(qreal(tick - startTick) / qreal(duration), 0.0, 1.0) : 0.0;

    return (static_cast<qreal>(index) + fraction) * _gridWidth;
}

void Timeline::updatePlaybackCursor()
{
    const INotationPlaybackPtr playback = m_notation ? m_notation->masterNotation()->playback() : nullptr;
    if (!score() || !playback || m_measureStartTicks.empty()) {
        if (m_playbackCursorItem) {
            m_playbackCursorItem->setVisible(false);
        }
        return;
    }

    muse::audio::secs_t secs = m_lastPlaybackPosition;
    const bool playing = m_playbackCursorTimer.isActive();
    if (playing) {
        secs += (m_elapsedTimer.nsecsElapsed() - m_lastPlaybackPositionUpdateTimeNs) / 1e9;
    }

    const qreal x = playbackCursorX(playback->secToTick(secs));

    if (!m_playbackCursorItem) {
        m_playbackCursorItem = new QGraphicsLineItem();
        m_playbackCursorItem->setAcceptedMouseButtons(Qt::NoButton);
        m_playbackCursorItem->setAcceptHoverEvents(false);
        //! NOTE: above everything (meta values' z keeps growing, see _globalZValue)
        m_playbackCursorItem->setZValue(1e9);
        scene()->addItem(m_playbackCursorItem);
    }

    QPen pen(m_defaultTrackColor, 1);
    pen.setCosmetic(true);
    m_playbackCursorItem->setPen(pen);
    m_playbackCursorItem->setLine(x, 0, x, std::max<qreal>(getHeight(), sceneRect().height()));
    m_playbackCursorItem->setVisible(true);

    if (playing) {
        ensurePlaybackCursorVisible(x);
    }
}

//! NOTE: page by page, like the score follows its cursor: once the cursor leaves the
//! visible area (past the right edge, or back before the left one after a repeat), the
//! view jumps so the cursor is near its left edge again
void Timeline::ensurePlaybackCursorVisible(qreal x)
{
    const int left = horizontalScrollBar()->value();
    const int visibleWidth = viewport()->width();
    if (visibleWidth <= 0) {
        return;
    }

    if (x < left || x > left + visibleWidth - _gridWidth / 2) {
        horizontalScrollBar()->setValue(static_cast<int>(x) - _gridWidth);
    }
}

//! NOTE: the parts whose instrument tracks a row's buttons and color strip act on: the
//! row's own part, or for a stave sharing combined part (which has no track of its own),
//! the parts it combines
std::vector<const Part*> Timeline::rowAudioParts(int row) const
{
    const Part* part = rowPart(row);
    if (!part) {
        return {};
    }

    if (part->isSharedPart()) {
        const std::vector<Part*>& originParts = toSharedPart(part)->originParts();
        return std::vector<const Part*>(originParts.begin(), originParts.end());
    }

    return { part };
}

static InstrumentTrackId partTrackId(const Part* part)
{
    const Instrument* instrument = part ? part->instrument(Fraction(0, 1)) : nullptr;
    if (!instrument) {
        return {};
    }

    return { part->id(), instrument->id() };
}

//! NOTE: each part's first instrument's track (a part with instrument changes has one
//! Mixer track per instrument)
std::vector<InstrumentTrackId> Timeline::rowTrackIds(int row) const
{
    std::vector<InstrumentTrackId> trackIds;
    for (const Part* part : rowAudioParts(row)) {
        const InstrumentTrackId trackId = partTrackId(part);
        if (trackId.isValid()) {
            trackIds.push_back(trackId);
        }
    }

    return trackIds;
}

//! NOTE: the first one, see rowTrackIds()
InstrumentTrackId Timeline::staffTrackId(int row) const
{
    const std::vector<InstrumentTrackId> trackIds = rowTrackIds(row);
    return trackIds.empty() ? InstrumentTrackId() : trackIds.front();
}

//! NOTE: for a row acting on several tracks, mute/solo show as on only when they're on
//! for ALL of them
Timeline::SoloMuteState Timeline::trackSoloMuteState(int row) const
{
    const std::vector<InstrumentTrackId> trackIds = rowTrackIds(row);
    if (trackIds.empty() || !playbackController()) {
        return {};
    }

    SoloMuteState result { true, true };
    for (const InstrumentTrackId& trackId : trackIds) {
        const SoloMuteState& state = playbackController()->trackSoloMuteState(trackId);
        result.mute &= state.mute;
        result.solo &= state.solo;
    }

    return result;
}

bool Timeline::isTrackMutedBySolo(int row) const
{
    return isTrackMutedBySolo(row, isAnythingSoloed());
}

//! NOTE: anythingSoloed = isAnythingSoloed(), passed in when checking several rows in a row
bool Timeline::isTrackMutedBySolo(int row, bool anythingSoloed) const
{
    const std::vector<InstrumentTrackId> trackIds = rowTrackIds(row);
    if (trackIds.empty() || !playbackController() || !anythingSoloed) {
        return false;
    }

    for (const InstrumentTrackId& trackId : trackIds) {
        if (!playbackController()->isTrackForceMuted(trackId) || playbackController()->trackSoloMuteState(trackId).solo) {
            return false;
        }
    }

    return true;
}

bool Timeline::isAnythingSoloed() const
{
    if (!score() || !playbackController()) {
        return false;
    }

    //! NOTE: every part, rows or not (a combined part's origin parts have no row)
    for (const Part* part : score()->parts()) {
        const InstrumentTrackId trackId = partTrackId(part);
        if (trackId.isValid() && playbackController()->trackSoloMuteState(trackId).solo) {
            return true;
        }
    }

    //! NOTE: a soloed group bus force-mutes every track not feeding it too
    if (const project::IProjectAudioSettingsPtr audio = audioSettings()) {
        for (muse::audio::aux_channel_idx_t index : audio->auxOutputParamsIndices()) {
            if (audio->isAuxBusGroup(index) && audio->auxSoloMuteState(index).solo) {
                return true;
            }
        }
    }

    return false;
}

//! NOTE: same as the Mixer's Mute/Solo buttons (which pick the change up from the same state);
//! a row acting on several tracks sets them all to the same new state
void Timeline::toggleTrackMute(int row)
{
    const std::vector<InstrumentTrackId> trackIds = rowTrackIds(row);
    if (trackIds.empty() || !playbackController()) {
        return;
    }

    const bool mute = !trackSoloMuteState(row).mute;
    if (mute && isTrackMutedBySolo(row)) {
        return;
    }

    for (const InstrumentTrackId& trackId : trackIds) {
        SoloMuteState state = playbackController()->trackSoloMuteState(trackId);
        state.mute = mute;
        playbackController()->setTrackSoloMuteState(trackId, state);
    }
}

void Timeline::toggleTrackSolo(int row)
{
    const std::vector<InstrumentTrackId> trackIds = rowTrackIds(row);
    if (trackIds.empty() || !playbackController()) {
        return;
    }

    const bool solo = !trackSoloMuteState(row).solo;
    for (const InstrumentTrackId& trackId : trackIds) {
        SoloMuteState state = playbackController()->trackSoloMuteState(trackId);
        state.solo = solo;
        playbackController()->setTrackSoloMuteState(trackId, state);
    }
}

void Timeline::scheduleLabelsUpdate()
{
    if (m_labelsUpdateScheduled) {
        return;
    }

    m_labelsUpdateScheduled = true;
    QMetaObject::invokeMethod(this, [this]() {
        m_labelsUpdateScheduled = false;
        if (score()) {
            _rowNames->updateLabels(getLabels(), _gridHeight);
        }
    }, Qt::QueuedConnection);
}

//! NOTE: same picker as the Mixer's "Edit color…", applied to every track the row acts
//! on (see rowTrackIds())
void Timeline::editTrackColor(int row)
{
    const project::IProjectAudioSettingsPtr audio = audioSettings();
    if (!score() || !audio) {
        return;
    }

    std::vector<InstrumentTrackId> trackIds;
    for (const InstrumentTrackId& trackId : rowTrackIds(row)) {
        if (audio->trackHasExistingOutputParams(trackId)) {
            trackIds.push_back(trackId);
        }
    }
    if (trackIds.empty()) {
        return;
    }

    const QColor currentColor = trackColor(row, Fraction(0, 1));

    //! NOTE: applied to the audio settings of the project the picker was opened for,
    //! even if another one became current meanwhile
    interactive()->selectColor(muse::Color::fromQColor(currentColor))
    .onResolve(this, [audio, trackIds](const muse::Color& color) {
        for (const InstrumentTrackId& trackId : trackIds) {
            if (!audio->trackHasExistingOutputParams(trackId)) {
                continue;
            }

            project::AudioOutputParams outParams = audio->trackOutputParams(trackId);
            outParams.color = color.toQColor();
            audio->setTrackOutputParams(trackId, outParams);
        }
    });
}

mu::project::IProjectAudioSettingsPtr Timeline::audioSettings() const
{
    return m_notation && m_notation->project() ? m_notation->project()->audioSettings() : nullptr;
}

void Timeline::updateDefaultTrackColor()
{
    m_defaultTrackColor = QColor(uiConfiguration()->currentTheme().values[muse::ui::ACCENT_COLOR].toString());
}

//---------------------------------------------------------
//   Timeline::partLabel
//---------------------------------------------------------

//! NOTE: the same name as the Layout panel shows (see PartTreeItem): the part name, which
//! includes the instrument's number and transposition (e.g. "Horn in F 1"), and for the
//! combined part of "Enable stave sharing" the parts it combines ("Horn in F 1-2", see
//! SharedPart::partName()). Falls back to the instrument's long name, then its name.
QString Timeline::partLabel(Part* part) const
{
    QTextDocument doc;
    doc.setHtml(part->partName());
    QString partName = doc.toPlainText().simplified();
    if (partName.isEmpty()) {
        doc.setHtml(part->longName());
        partName = doc.toPlainText().simplified();
    }
    if (partName.isEmpty()) {
        partName = part->instrumentName();
    }

    return partName;
}

//---------------------------------------------------------
//   Timeline::getLabels
//---------------------------------------------------------

std::vector<std::pair<QString, bool> > Timeline::getLabels()
{
    if (!score()) {
        std::vector<std::pair<QString, bool> > noLabels;
        return noLabels;
    }

    QList<Part*> partList = getParts();
    // Transfer them into a vector of qstrings and then add the meta row names
    std::vector<std::pair<QString, bool> > rowLabels;
    if (_collapsedMeta) {
        std::pair<QString, bool> first = std::make_pair("", true);
        std::pair<QString, bool> second = std::make_pair(muse::qtrc("notation/timeline", "Measures"), true);
        rowLabels.push_back(first);
        rowLabels.push_back(second);
    } else {
        for (auto it = _metas.begin(); it != _metas.end(); ++it) {
            std::tuple<QString, void (Timeline::*)(Segment*, int*, int), bool> meta = *it;
            if (!std::get<2>(meta)) {
                continue;
            }
            std::pair<QString, bool> metaLabel = std::make_pair(std::get<0>(meta), true);
            rowLabels.push_back(metaLabel);
        }
    }

    for (int stave = 0; stave < partList.size(); stave++) {
        const QString partName = partLabel(partList.at(stave));

        std::pair<QString, bool> instrumentLabel = std::make_pair(partName, partList.at(stave)->show());
        rowLabels.push_back(instrumentLabel);
    }
    return rowLabels;
}

//---------------------------------------------------------
//   Timeline::handleScroll
//---------------------------------------------------------

void Timeline::handleScroll(int value)
{
    if (!score()) {
        return;
    }

    for (auto it = _metaRows.begin(); it != _metaRows.end(); ++it) {
        std::pair<QGraphicsItem*, int> pairGraphicsInt = *it;

        QGraphicsItem* graphicsItem = pairGraphicsInt.first;
        QGraphicsRectItem* graphicsRectItem = qgraphicsitem_cast<QGraphicsRectItem*>(graphicsItem);
        QGraphicsLineItem* graphicsLineItem = qgraphicsitem_cast<QGraphicsLineItem*>(graphicsItem);
        QGraphicsPixmapItem* graphicsPixmapItem = qgraphicsitem_cast<QGraphicsPixmapItem*>(graphicsItem);

        int rowY = pairGraphicsInt.second * _gridHeight;
        int scrollbarValue = value;

        if (graphicsRectItem) {
            QRectF rectf = graphicsRectItem->rect();
            rectf.setY(qreal(scrollbarValue + rowY));
            rectf.setHeight(_gridHeight);
            graphicsRectItem->setRect(rectf);
        } else if (graphicsLineItem) {
            QLineF linef = graphicsLineItem->line();
            linef.setLine(linef.x1(), rowY + scrollbarValue + 1, linef.x2(), rowY + scrollbarValue + 1);
            graphicsLineItem->setLine(linef);
        } else if (graphicsPixmapItem) {
            graphicsPixmapItem->setY(qreal(scrollbarValue + rowY + 3));
        } else {
            graphicsItem->setY(qreal(scrollbarValue + rowY));
        }
    }
    viewport()->update();
}

//---------------------------------------------------------
//   Timeline::mouseOver
//---------------------------------------------------------

void Timeline::mouseOver(QPointF pos)
{
    TRACEFUNC;

    // Choose item with the largest original Z value...
    QList<QGraphicsItem*> graphicsList = scene()->items(pos);
    QGraphicsItem* hoveredGraphicsItem = 0;
    int maxZValue = -1;
    for (QGraphicsItem* currGraphicsItem : graphicsList) {
        if (qgraphicsitem_cast<QGraphicsTextItem*>(currGraphicsItem)) {
            continue;
        }
        if (currGraphicsItem->zValue() >= maxZValue && currGraphicsItem->zValue() < _globalZValue) {
            hoveredGraphicsItem = currGraphicsItem;
            maxZValue = hoveredGraphicsItem->zValue();
        } else if (currGraphicsItem->zValue() > _globalZValue && std::get<1>(_oldHoverInfo) >= maxZValue) {
            hoveredGraphicsItem = currGraphicsItem;
            maxZValue = std::get<1>(_oldHoverInfo);
        }
    }

    if (!hoveredGraphicsItem) {
        if (std::get<0>(_oldHoverInfo)) {
            std::get<0>(_oldHoverInfo)->setZValue(std::get<1>(_oldHoverInfo));
            static_cast<QGraphicsItem*>(std::get<0>(_oldHoverInfo)->data(5).value<void*>())->setZValue(std::get<1>(_oldHoverInfo));
            QGraphicsRectItem* graphicsRectItem1 = qgraphicsitem_cast<QGraphicsRectItem*>(std::get<0>(_oldHoverInfo));
            QGraphicsRectItem* graphicsRectItem2
                = qgraphicsitem_cast<QGraphicsRectItem*>(static_cast<QGraphicsItem*>(std::get<0>(_oldHoverInfo)->data(5).value<void*>()));
            if (graphicsRectItem1) {
                graphicsRectItem1->setBrush(QBrush(std::get<2>(_oldHoverInfo)));
            }
            if (graphicsRectItem2) {
                graphicsRectItem2->setBrush(QBrush(std::get<2>(_oldHoverInfo)));
            }
            std::get<0>(_oldHoverInfo) = nullptr;
            std::get<1>(_oldHoverInfo) = -1;
        }
        return;
    }
    QGraphicsItem* pairItem = static_cast<QGraphicsItem*>(hoveredGraphicsItem->data(5).value<void*>());
    if (!pairItem) {
        if (std::get<0>(_oldHoverInfo)) {
            std::get<0>(_oldHoverInfo)->setZValue(std::get<1>(_oldHoverInfo));
            static_cast<QGraphicsItem*>(std::get<0>(_oldHoverInfo)->data(5).value<void*>())->setZValue(std::get<1>(_oldHoverInfo));
            QGraphicsRectItem* graphicsRectItem1 = qgraphicsitem_cast<QGraphicsRectItem*>(std::get<0>(_oldHoverInfo));
            QGraphicsRectItem* graphicsRectItem2
                = qgraphicsitem_cast<QGraphicsRectItem*>(static_cast<QGraphicsItem*>(std::get<0>(_oldHoverInfo)->data(5).value<void*>()));
            if (graphicsRectItem1) {
                graphicsRectItem1->setBrush(QBrush(std::get<2>(_oldHoverInfo)));
            }
            if (graphicsRectItem2) {
                graphicsRectItem2->setBrush(QBrush(std::get<2>(_oldHoverInfo)));
            }
            std::get<0>(_oldHoverInfo) = nullptr;
            std::get<1>(_oldHoverInfo) = -1;
        }
        return;
    }

    if (std::get<0>(_oldHoverInfo) == hoveredGraphicsItem) {
        return;
    }

    if (std::get<0>(_oldHoverInfo)) {
        std::get<0>(_oldHoverInfo)->setZValue(std::get<1>(_oldHoverInfo));
        static_cast<QGraphicsItem*>(std::get<0>(_oldHoverInfo)->data(5).value<void*>())->setZValue(std::get<1>(_oldHoverInfo));
        QGraphicsRectItem* graphicsRectItem1 = qgraphicsitem_cast<QGraphicsRectItem*>(std::get<0>(_oldHoverInfo));
        QGraphicsRectItem* graphicsRectItem2
            = qgraphicsitem_cast<QGraphicsRectItem*>(static_cast<QGraphicsItem*>(std::get<0>(_oldHoverInfo)->data(5).value<void*>()));
        if (graphicsRectItem1) {
            graphicsRectItem1->setBrush(QBrush(std::get<2>(_oldHoverInfo)));
        }
        if (graphicsRectItem2) {
            graphicsRectItem2->setBrush(QBrush(std::get<2>(_oldHoverInfo)));
        }

        std::get<0>(_oldHoverInfo) = nullptr;
        std::get<1>(_oldHoverInfo) = -1;
    }

    std::get<1>(_oldHoverInfo) = hoveredGraphicsItem->zValue();
    std::get<0>(_oldHoverInfo) = hoveredGraphicsItem;

    // Give items the top z value
    hoveredGraphicsItem->setZValue(_globalZValue + 1);
    pairItem->setZValue(_globalZValue + 1);

    QGraphicsRectItem* graphicsRectItem1 = qgraphicsitem_cast<QGraphicsRectItem*>(hoveredGraphicsItem);
    QGraphicsRectItem* graphicsRectItem2 = qgraphicsitem_cast<QGraphicsRectItem*>(pairItem);
    if (graphicsRectItem1) {
        std::get<2>(_oldHoverInfo) = graphicsRectItem1->brush().color();
        if (std::get<2>(_oldHoverInfo) != activeTheme().selectionColor) {
            graphicsRectItem1->setBrush(QBrush(activeTheme().backgroundColor));
        }
    }
    if (graphicsRectItem2) {
        std::get<2>(_oldHoverInfo) = graphicsRectItem2->brush().color();
        if (std::get<2>(_oldHoverInfo) != activeTheme().selectionColor) {
            graphicsRectItem2->setBrush(QBrush(activeTheme().backgroundColor));
        }
    }
}

//---------------------------------------------------------
//   Timeline::swapMeta
//---------------------------------------------------------

void Timeline::swapMeta(unsigned row, bool switchUp)
{
    // Attempt to switch row up or down, skipping non visible rows
    if (switchUp && row != 0) {
        // traverse backwards until visible one is found
        auto swap = _metas.begin() + correctMetaRow(row) - 1;
        while (!std::get<2>(*swap)) {
            swap--;
        }
        iter_swap(_metas.begin() + correctMetaRow(row), swap);
    } else if (!switchUp && static_cast<int>(row) != nswappableMetas() - 1) {
        // traverse forwards until visible one is found
        auto swap = _metas.begin() + correctMetaRow(row) + 1;
        while (!std::get<2>(*swap)) {
            swap++;
        }
        iter_swap(_metas.begin() + correctMetaRow(row), swap);
    }

    updateGrid();
}

//---------------------------------------------------------
//   Timeline::numToStaff
//---------------------------------------------------------

Staff* Timeline::numToStaff(int staff)
{
    if (!score()) {
        return nullptr;
    }

    size_t staffIdx = static_cast<size_t>(staff);

    const std::vector<Staff*>& staves = score()->staves();
    if (staffIdx < staves.size()) {
        return staves.at(staffIdx);
    } else {
        return nullptr;
    }
}

//---------------------------------------------------------
//   Timeline::toggleShow
//---------------------------------------------------------

void Timeline::toggleShow(int staff)
{
    if (!score()) {
        return;
    }

    QList<Part*> parts = getParts();
    if (staff < 0 || staff >= parts.size()) {
        return;
    }

    Part* part = parts.at(staff);

    bool newShow = !part->show();
    TranslatableString actionName = newShow
                                    ? TranslatableString("undoableAction", "Show instrument")
                                    : TranslatableString("undoableAction", "Hide instrument");

    m_notation->undoStack()->prepareChanges(actionName);
    part->undoChangeProperty(Pid::VISIBLE, newShow);
    m_notation->undoStack()->commitChanges();
    m_notation->notationChanged().send(muse::RectF());
}

//---------------------------------------------------------
//   Timeline::showContextMenu
//---------------------------------------------------------

void Timeline::contextMenuEvent(QContextMenuEvent* event)
{
    QMenu* contextMenu = new QMenu(muse::qtrc("notation/timeline", "Context menu"), this);
    if (_rowNames->cursorIsOn() == "instrument") {
        QAction* edit_instruments = new QAction(muse::qtrc("notation/timeline", "Edit instruments"), this);
        connect(edit_instruments, &QAction::triggered, this, &Timeline::requestInstrumentDialog);
        contextMenu->addAction(edit_instruments);
        contextMenu->exec(QCursor::pos());
    } else if (_rowNames->cursorIsOn() == "meta" || cursorIsOn(event->pos()) == "meta") {
        for (auto it = _metas.begin(); it != _metas.end(); ++it) {
            std::tuple<QString, void (Timeline::*)(Segment*, int*, int), bool> meta = *it;
            QAction* action = new QAction(std::get<0>(meta), this);
            action->setCheckable(true);
            action->setChecked(std::get<2>(meta));
            connect(action, &QAction::triggered, this, &Timeline::toggleMetaRow);
            contextMenu->addAction(action);
        }
        contextMenu->addSeparator();
        QAction* hide_all = new QAction(muse::qtrc("notation/timeline", "Hide all"), this);
        connect(hide_all, &QAction::triggered, this, &Timeline::toggleMetaRow);
        contextMenu->addAction(hide_all);
        QAction* show_all = new QAction(muse::qtrc("notation/timeline", "Show all"), this);
        connect(show_all, &QAction::triggered, this, &Timeline::toggleMetaRow);
        contextMenu->addAction(show_all);
        contextMenu->exec(QCursor::pos());
    }
}

//---------------------------------------------------------
//   Timeline::toggleMetaRow
//---------------------------------------------------------

void Timeline::toggleMetaRow()
{
    QAction* action = qobject_cast<QAction*>(QObject::sender());
    if (!action) {
        return;
    }

    QString targetText = action->text();

    //! NOTE: copied, since each setTimelineRowVisible() call below re-applies the
    //! settings to _metas through timelineRowsVisibilityChanged()
    const auto metas = _metas;

    if (targetText == muse::qtrc("notation/timeline", "Hide all")) {
        for (const auto& meta : metas) {
            if (std::get<1>(meta) != &Timeline::measureMeta) {
                configuration()->setTimelineRowVisible(metaRowId(std::get<1>(meta)), false);
            }
        }
        return;
    } else if (targetText == muse::qtrc("notation/timeline", "Show all")) {
        for (const auto& meta : metas) {
            configuration()->setTimelineRowVisible(metaRowId(std::get<1>(meta)), true);
        }
        return;
    }

    // Find target text in metas and toggle visibility to the checked status of action
    for (const auto& meta : metas) {
        if (std::get<0>(meta) == targetText) {
            configuration()->setTimelineRowVisible(metaRowId(std::get<1>(meta)), action->isChecked());
            break;
        }
    }
}

//---------------------------------------------------------
//   Timeline::nmetas
//---------------------------------------------------------

unsigned Timeline::nmetas() const
{
    unsigned total = 0;
    if (_collapsedMeta) {
        return 2;
    }
    for (auto it = _metas.begin(); it != _metas.end(); ++it) {
        std::tuple<QString, void (Timeline::*)(Segment*, int*, int), bool> meta = *it;
        if (std::get<2>(meta)) {
            total++;
        }
    }
    return total;
}

//---------------------------------------------------------
//   Timeline::correctMetaRow
//---------------------------------------------------------

unsigned Timeline::correctMetaRow(unsigned row)
{
    unsigned count = 0;
    auto it = _metas.begin();
    while (row >= count) {
        if (!std::get<2>(*it)) {
            row++;
        }
        count++;
        ++it;
    }
    return row;
}

//---------------------------------------------------------
//   Timeline::correctMetaRow
//---------------------------------------------------------

QString Timeline::cursorIsOn(const QPoint& cursorPos)
{
    QGraphicsItem* graphicsItem = scene()->itemAt(cursorPos, transform());
    if (!graphicsItem) {
        return "";
    }

    auto it = _metaRows.begin();
    for (; it != _metaRows.end(); ++it) {
        if ((*it).first == graphicsItem) {
            break;
        }
    }
    if (it != _metaRows.end()) {
        return "meta";
    }
    QList<QGraphicsItem*> graphicsItemList = scene()->items(cursorPos);
    for (QGraphicsItem* currGraphicsItem : graphicsItemList) {
        Measure* currMeasure = static_cast<Measure*>(currGraphicsItem->data(2).value<void*>());
        const Part* part = rowPart(currGraphicsItem->data(0).value<int>());
        if (currMeasure && !(part && part->show())) {
            return "invalid";
        }
    }
    return "instrument";
}

//---------------------------------------------------------
//   Timeline::activeTheme
//---------------------------------------------------------

const TimelineTheme& Timeline::activeTheme() const
{
    if (uiConfiguration()->currentTheme().codeKey == muse::ui::DARK_THEME_CODE) {
        return _darkTheme;
    }

    return _lightTheme;
}

//---------------------------------------------------------
//   Timeline::updateTimelineTheme
//---------------------------------------------------------

void Timeline::updateTimelineTheme()
{
    updateDefaultTrackColor();
    const QBrush backgroundBrush = QBrush(activeTheme().backgroundColor);
    scene()->setBackgroundBrush(backgroundBrush);
    _rowNames->scene()->setBackgroundBrush(backgroundBrush);
    updateGrid();
}

//---------------------------------------------------------
//   Timeline::requestInstrumentDialog
//---------------------------------------------------------

void Timeline::requestInstrumentDialog()
{
    dispatcher()->dispatch("instruments");
}

INotationInteractionPtr Timeline::interaction() const
{
    return m_notation ? m_notation->interaction() : nullptr;
}

Score* Timeline::score() const
{
    return m_notation ? m_notation->elements()->msScore() : nullptr;
}

mu::project::IProjectVideoSettingsPtr Timeline::videoSettings() const
{
    return m_notation && m_notation->project() ? m_notation->project()->videoSettings() : nullptr;
}

QString Timeline::formatVideoTimecode(int videoPositionMs) const
{
    const mu::project::IProjectVideoSettingsPtr settings = videoSettings();
    const double frameRate = settings && settings->attachment().isValid() ? settings->attachment().frameRate : 24.0;
    return mu::project::formatVideoTimecode(videoPositionMs, frameRate);
}

TRowLabels* Timeline::labelsColumn() const
{
    return _rowNames;
}
