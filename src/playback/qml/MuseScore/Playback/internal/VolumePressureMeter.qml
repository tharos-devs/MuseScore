/*
 * SPDX-License-Identifier: GPL-3.0-only
 * MuseScore-CLA-applies
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
 
import QtQuick
import QtQuick.Window

import Muse.UiComponents
import Muse.Ui

//! NOTE: the level bar is drawn with rectangles on whole physical pixels (see indicatorWidth); the Canvas only draws
//! the dB ruler. Drawn by the Canvas, a meter scaled by a panel zoom (e.g. 0.9 on a Retina screen: 5.4 pixels) got
//! another width than the one next to it, depending on where it fell on the pixel grid
Canvas {
    id: root

    property real currentVolumePressure: -60.0
    property real minDisplayedVolumePressure: -60.0
    property real maxDisplayedVolumePressure: 0.0

    property bool showRuler: false

    //! NOTE: the Mixer's size by default; smaller for a compact meter (e.g. the Track list's rows)
    property real meterLength: 140
    property real meterThickness: 6
    property int overloadLength: 4

    property bool isClipping: currentVolumePressure >= maxDisplayedVolumePressure

    //! NOTE: physical pixels per logical one here: the screen's, times the zooms of the panels around
    readonly property real pixelScale: {
        let result = Screen.devicePixelRatio > 0 ? Screen.devicePixelRatio : 1
        for (let item = root.parent; item; item = item.parent) {
            result *= item.scale
        }
        return result > 0 ? result : 1
    }

    //! NOTE: a length rounded to whole physical pixels (at least one)
    function snapped(length) {
        return Math.max(1, Math.round(length * root.pixelScale)) / root.pixelScale
    }

    readonly property real indicatorWidth: prv.indicatorWidth

    //! NOTE: the ruler is drawn next to the bar
    onIndicatorWidthChanged: {
        prv.rulerNeedsPaint = true
        requestPaint()
    }

    onShowRulerChanged: {
        prv.rulerNeedsPaint = true
        requestPaint()
    }

    width: root.showRuler ? prv.indicatorWidth + 20 : prv.indicatorWidth
    height: prv.indicatorHeight + (prv.overloadHeight * 2)

    QtObject {
        id: prv

        readonly property int overloadHeight: root.overloadLength

        readonly property real indicatorHeight: root.meterLength
        readonly property real indicatorWidth: root.snapped(root.meterThickness)

        // value ranges
        readonly property int fullValueRangeLength: root.maxDisplayedVolumePressure - root.minDisplayedVolumePressure
        readonly property real divisionPixels: (prv.indicatorHeight - prv.overloadHeight) / fullValueRangeLength

        //! NOTE: the level's full height, and where a stop of the Canvas's former gradient (from the bottom over the
        //! meter's length minus both margins) falls on it, from its top
        readonly property real levelMaxHeight: prv.indicatorHeight - prv.overloadHeight
        function gradientStopPosition(stop) {
            return 1 - stop * (prv.indicatorHeight - 2 * prv.overloadHeight) / prv.levelMaxHeight
        }

        readonly property real unitsTextWidth: 12
        readonly property color unitTextColor: Utils.colorWithAlpha(ui.theme.fontPrimaryColor, 0.8)
        readonly property string unitTextFont: {
            var pxSize = String('8px')
            var family = String('\'' + ui.theme.bodyFont.family + '\'')

            return pxSize + ' ' + family
        }

        onUnitTextColorChanged: { prv.rulerNeedsPaint = true; root.requestPaint() }
        onUnitTextFontChanged: { prv.rulerNeedsPaint = true; root.requestPaint() }

        // strokes
        readonly property real strokeHorizontalMargin: 2
        readonly property real longStrokeHeight: 1
        readonly property real longStrokeWidth: 5
        readonly property color longStrokeColor: Utils.colorWithAlpha(ui.theme.fontPrimaryColor, 0.5)
        readonly property real shortStrokeHeight: 1
        readonly property real shortStrokeWidth: 2
        readonly property color shortStrokeColor: Utils.colorWithAlpha(ui.theme.fontPrimaryColor, 0.3)

        onLongStrokeColorChanged: { prv.rulerNeedsPaint = true; root.requestPaint() }
        onShortStrokeColorChanged: { prv.rulerNeedsPaint = true; root.requestPaint() }

        property bool rulerNeedsPaint: true
    }

    function drawRuler(ctx, originVPos, originHPos, fullStep, smallStep, strokeHeight, strokeWidth) {
        ctx.clearRect(0, prv.indicatorWidth, root.height, root.width - prv.indicatorWidth)
        ctx.font = prv.unitTextFont

        var currentStrokeVPos = 0

        for (var i = 0; i <= prv.fullValueRangeLength; i+=smallStep) {
            if (i == 0) {
                currentStrokeVPos = originVPos
            } else {
                currentStrokeVPos += prv.divisionPixels * smallStep
            }

            if (i % fullStep) {
                ctx.fillStyle = prv.shortStrokeColor
                ctx.fillRect(currentStrokeVPos,
                             originHPos,
                             prv.shortStrokeHeight,
                             prv.shortStrokeWidth)

            } else {
                ctx.fillStyle = prv.longStrokeColor
                ctx.fillRect(currentStrokeVPos,
                             originHPos,
                             prv.longStrokeHeight,
                             prv.longStrokeWidth)

                let textHPos = originHPos + prv.longStrokeWidth + prv.strokeHorizontalMargin

                ctx.save()

                ctx.rotate(Math.PI/2)
                ctx.fillStyle = prv.unitTextColor
                ctx.fillText(prv.fullValueRangeLength - i, textHPos, -currentStrokeVPos + 2)

                ctx.restore()
            }
        }

        prv.rulerNeedsPaint = false
    }

    onPaint: {
        //! NOTE: without a ruler (e.g. the Track list's meters), nothing is drawn here at all
        if (!root.showRuler || !prv.rulerNeedsPaint) {
            return
        }

        var ctx = root.context

        if (!ctx) {
            ctx = getContext("2d")
            ctx.translate(0, root.height)
            ctx.rotate(3 * (Math.PI/2))

            if (ui.currentLanguageLayoutDirection() === Qt.RightToLeft) {
                ctx.textAlign = "end"
            } else {
                ctx.textAlign = "start"
            }
        }

        var originVPos = prv.overloadHeight
        var originHPos = prv.indicatorWidth + prv.strokeHorizontalMargin

        drawRuler(ctx, originVPos, originHPos, 6/*fullStep*/, 3/*smallStep*/)
    }

    //! NOTE: the background, from just above the bottom margin up to the top margin
    Rectangle {
        x: 0
        y: prv.overloadHeight
        width: prv.indicatorWidth
        height: prv.indicatorHeight
        color: "#4D4D4D"
    }

    //! NOTE: the overload light, at the top of the background
    Rectangle {
        x: 0
        y: prv.overloadHeight
        width: prv.indicatorWidth
        height: prv.overloadHeight
        color: root.isClipping ? "#FF1C1C" : "#666666"
    }

    //! NOTE: the level, from the bottom; its gradient stays fixed along the meter (only the visible part grows)
    Item {
        x: 0
        width: prv.indicatorWidth
        height: Math.max(0, Math.min(prv.levelMaxHeight,
                                     prv.divisionPixels * (prv.fullValueRangeLength - Math.abs(root.currentVolumePressure))))
        anchors.bottom: parent.bottom
        anchors.bottomMargin: prv.overloadHeight

        clip: true

        Rectangle {
            anchors.bottom: parent.bottom
            width: parent.width
            height: prv.levelMaxHeight

            gradient: Gradient {
                GradientStop { position: 0.0; color: "#FC8226" }
                GradientStop { position: prv.gradientStopPosition(0.80); color: "#FC8226" }
                GradientStop { position: prv.gradientStopPosition(0.55); color: "#CBED41" }
                GradientStop { position: 1.0; color: "#26E386" }
            }
        }
    }

    Component.onCompleted: {
        prv.rulerNeedsPaint = true
        requestPaint()
    }
}
