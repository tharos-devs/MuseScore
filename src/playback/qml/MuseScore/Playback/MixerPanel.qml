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

pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Window

import Muse.Ui
import Muse.UiComponents
import MuseScore.Playback

import "internal"

ColumnLayout {
    id: root

    property NavigationSection navigationSection: null
    property int contentNavigationPanelOrderStart: 1

    property alias contextMenuModel: contextMenuModel
    property Component toolbarComponent: MixerPanelToolbar {
        navigation.section: root.navigationSection
        navigation.order: root.contentNavigationPanelOrderStart
    }

    // NOTE: whether the enclosing DockPanel is currently floating (undocked)
    // -- fed in from NotationPage.qml (`floating: mixerPanel.floating` on
    // this component's own instantiation). Full screen only makes sense once
    // this panel is its own floating window -- when docked, there's no
    // separate window to fullscreen, so "Full screen" would end up
    // fullscreening the whole MuseScore window instead, which isn't what a
    // user asking to fullscreen just the mixer expects (e.g. floating this
    // panel onto a second monitor and fullscreening it there).
    property bool floating: false

    // NOTE: docking while "full screen" (simulated, see below) leaves no
    // sensible state to return to on a later undock -- start fresh instead of
    // carrying a stale saved geometry/flag across a dock/undock cycle.
    onFloatingChanged: {
        if (!floating) {
            isFullScreen = false
            savedGeometry = null
        }
    }

    // NOTE: tracked ourselves rather than reading Window.window.visibility ===
    // Window.FullScreen -- see the NOTE on savedGeometry below for why native
    // showFullScreen()/showNormal() aren't used here at all.
    property bool isFullScreen: false

    // NOTE: this does NOT use QWindow.showFullScreen()/showNormal() at all --
    // this window type (Qt::Tool, frameless -- see KDDockWidgets' FloatingWindow
    // setup) doesn't reliably restore geometry on showNormal() (confirmed for
    // the Video panel's floating window, same window type). Doing this as a
    // plain geometry resize instead -- filling the window's current screen,
    // no native full-screen transition involved at all -- sidesteps that
    // stuck state entirely, at the cost of not hiding the OS menu bar/dock on
    // the screen it's on the way real OS full screen would.
    property var savedGeometry: null

    signal resizeRequested(var newWidth, var newHeight)

    spacing: 0

    function resizePanelToContentHeight() {
        if (contentColumn.completed && implicitHeight > 0) {
            root.resizeRequested(width, implicitHeight)
        }
    }

    onImplicitHeightChanged: {
        root.resizePanelToContentHeight()
    }

    QtObject {
        id: prv

        property var currentNavigateControlIndex: undefined
        property bool isPanelActivated: false

        readonly property real headerWidth: 98
        readonly property real channelItemWidth: 108

        //! NOTE: right-edge analogue of headerPinOffsetX's left-edge pin -- see
        //! MixerPanelSection.qml's masterPinOffsetX property for the full
        //! explanation of the contentX-cancelling trick. Centralized here (rather
        //! than repeated per section instantiation below) since every section
        //! needs the exact same value.
        readonly property real masterPinOffsetX: flickable.contentX + flickable.width - channelItemWidth

        function setNavigateControlIndex(index) {
            if (!Boolean(prv.currentNavigateControlIndex) ||
                    index.row !== prv.currentNavigateControlIndex.row ||
                    index.column !== prv.currentNavigateControlIndex.column) {
                prv.isPanelActivated = false
            }

            prv.currentNavigateControlIndex = index
        }
    }

    function scrollToFocusedItem(focusedIndex) {
        let targetScrollPosition = (focusedIndex) * (prv.channelItemWidth + 1) // + 1 for separators
        let maxContentX = flickable.contentWidth - flickable.width

        if (targetScrollPosition + prv.channelItemWidth > flickable.contentX + flickable.width) {
            flickable.contentX = Math.min(targetScrollPosition + prv.channelItemWidth - flickable.width, maxContentX)
        } else if (targetScrollPosition < flickable.contentX) {
            flickable.contentX = Math.max(targetScrollPosition - prv.channelItemWidth, 0)
        }
    }

    MixerPanelModel {
        id: mixerPanelModel

        navigationSection: root.navigationSection
        navigationOrderStart: root.contentNavigationPanelOrderStart + 1 // +1 for toolbar

        onModelReset: {
            Qt.callLater(setupConnections)
        }

        function setupConnections() {
            for (let i = 0; i < mixerPanelModel.rowCount(); i++) {
                let item = mixerPanelModel.get(i)
                item.channelItem.panel.navigationEvent.connect(function(event) {
                    if (event.type === NavigationEvent.AboutActive) {
                        if (Boolean(prv.currentNavigateControlIndex)) {
                            event.setData("controlIndex", [prv.currentNavigateControlIndex.row, prv.currentNavigateControlIndex.column])
                            event.setData("controlOptional", true)
                        }

                        prv.isPanelActivated = true
                        scrollToFocusedItem(i)
                    }
                })
            }
        }
    }

    MixerPanelContextMenuModel {
        id: contextMenuModel

        floating: root.floating
        isFullScreen: root.isFullScreen

        Component.onCompleted: {
            contextMenuModel.load()
        }

        onToggleFullScreenRequested: {
            var win = root.Window.window
            if (!win) {
                return
            }

            if (root.isFullScreen) {
                if (root.savedGeometry) {
                    win.x = root.savedGeometry.x
                    win.y = root.savedGeometry.y
                    win.width = root.savedGeometry.width
                    win.height = root.savedGeometry.height
                    root.savedGeometry = null
                }
                root.isFullScreen = false
            } else {
                root.savedGeometry = { x: win.x, y: win.y, width: win.width, height: win.height }

                // NOTE: fills the AVAILABLE area of whichever screen the
                // window is currently on (QScreen::availableGeometry(), via
                // contextMenuModel.screenAvailableGeometry() -- win itself is
                // a KDDockWidgets::QuickView wrapper around the real
                // QQuickWindow and doesn't forward a usable `.screen`, so C++
                // resolves the right QScreen from the window's own position
                // instead), not the screen's raw full geometry -- the
                // available area is exactly what's left over once the OS
                // reserves its own space (the menu bar on macOS, the taskbar
                // on Windows, panels on Linux), so this fills the screen
                // without ever landing underneath any of that, on any
                // platform.
                var availableGeometry = contextMenuModel.screenAvailableGeometry(win.x, win.y)
                win.x = availableGeometry.x
                win.y = availableGeometry.y
                win.width = availableGeometry.width
                win.height = availableGeometry.height
                root.isFullScreen = true
            }
        }
    }

    StyledFlickable {
        id: flickable

        Layout.fillWidth: true
        Layout.fillHeight: true

        //! NOTE: + channelItemWidth reserves the master channel's own slot at the
        //! tail end -- master's delegate is excluded from contentColumn's normal
        //! flow (zero width, see MixerPanelSection.qml) since it's rendered
        //! pinned instead, but the scrollable range still needs to leave that
        //! much room, or scrolling to the end would put the LAST REGULAR channel
        //! directly underneath the pinned master column instead of stopping
        //! just before it.
        contentWidth: contentColumn.width + 1 + prv.channelItemWidth // for trailing separator + reserved master slot
        contentHeight: Math.max(contentColumn.height, height)

        implicitHeight: contentColumn.height

        interactive: (height < contentHeight || width < contentWidth) && !flickable.resourcePickingActive

        ScrollBar.horizontal: horizontalScrollBar

        ScrollBar.vertical: StyledScrollBar { policy: ScrollBar.AlwaysOn }

        property bool completed: false
        property bool resourcePickingActive: soundSection.resourcePickingActive || fxSection.resourcePickingActive

        function positionViewAtEnd() {
            if (!flickable.completed) {
                return
            }

            if (flickable.contentY == flickable.contentHeight) {
                return
            }

            flickable.contentY = flickable.contentHeight - flickable.height
        }

        onContentHeightChanged: {
            flickable.positionViewAtEnd()
        }

        Component.onCompleted: {
            flickable.completed = true
        }

        MouseArea {
            anchors.fill: parent
            z: -1

            onClicked: {
                mixerPanelModel.clearSelection()
            }
        }

        Row {
            id: separators

            anchors.fill: parent
            anchors.leftMargin: prv.channelItemWidth + (contextMenuModel.labelsSectionVisible ? prv.headerWidth : 0)

            spacing: prv.channelItemWidth

            Repeater {
                //! NOTE: -2 to drop both the boundary right before the master channel
                //! and the trailing one right after it -- master is now pinned
                //! (excluded from the normal scrolling flow, see
                //! MixerPanelSection.qml), so neither boundary tracks real scrolled
                //! content any more; the "before master" one is replaced by the
                //! dedicated pinned separator below instead, and the trailing one has
                //! nothing left to bound.
                model: Math.max(0, mixerPanelModel.count - 2)

                SeparatorLine { orientation: Qt.Vertical }
            }
        }

        //! NOTE: this used to be the leading item in `separators` above (the boundary
        //! right after the label column) -- pinned in lockstep with the sticky header
        //! labels (see MixerPanelSection.qml's headerPinOffsetX) via the same
        //! contentX-cancelling trick, since it would otherwise visibly detach from the
        //! header as soon as the user scrolled.
        SeparatorLine {
            orientation: Qt.Vertical
            visible: contextMenuModel.labelsSectionVisible
            z: 2

            x: flickable.contentX + prv.headerWidth
        }

        //! NOTE: right-edge analogue of the pinned separator above, for the master
        //! channel's own pinned strip (see MixerPanelSection.qml's masterPinOffsetX) --
        //! sits immediately to its left so it doesn't visually detach from it either.
        SeparatorLine {
            orientation: Qt.Vertical
            visible: mixerPanelModel.count > 0
            z: 2

            x: prv.masterPinOffsetX - 1
        }

        Column {
            id: contentColumn

            anchors.bottom: parent.bottom
            width: childrenRect.width
            spacing: 0

            property bool completed: false

            Component.onCompleted: {
                contentColumn.completed = true
            }

                MixerSoundSection {
                    id: soundSection

                    visible: contextMenuModel.soundSectionVisible
                    headerVisible: contextMenuModel.labelsSectionVisible
                    headerWidth: prv.headerWidth
                    channelItemWidth: prv.channelItemWidth
                    headerPinOffsetX: flickable.contentX
                    masterPinOffsetX: prv.masterPinOffsetX
                    spacingAbove: 8

                    model: mixerPanelModel

                    navigationRowStart: 1
                    needReadChannelName: prv.isPanelActivated

                    onNavigateControlIndexChanged: function(index) {
                        prv.setNavigateControlIndex(index)
                    }
                }

                MixerGainSection {
                    id: gainSection

                    visible: contextMenuModel.gainSectionVisible
                    headerVisible: contextMenuModel.labelsSectionVisible
                    headerWidth: prv.headerWidth
                    channelItemWidth: prv.channelItemWidth
                    headerPinOffsetX: flickable.contentX
                    masterPinOffsetX: prv.masterPinOffsetX

                    model: mixerPanelModel

                    navigationRowStart: 50
                    needReadChannelName: prv.isPanelActivated

                    onNavigateControlIndexChanged: function(index) {
                        prv.setNavigateControlIndex(index)
                    }
                }

                MixerFxSection {
                    id: fxSection

                    visible: contextMenuModel.audioFxSectionVisible
                    headerVisible: contextMenuModel.labelsSectionVisible
                    headerWidth: prv.headerWidth
                    channelItemWidth: prv.channelItemWidth
                    headerPinOffsetX: flickable.contentX
                    masterPinOffsetX: prv.masterPinOffsetX

                    model: mixerPanelModel

                    navigationRowStart: 100
                    needReadChannelName: prv.isPanelActivated

                    onNavigateControlIndexChanged: function(index) {
                        prv.setNavigateControlIndex(index)
                    }
                }

                MixerAuxSendsSection {
                    id: auxSendsSection

                    visible: contextMenuModel.auxSendsSectionVisible
                    headerVisible: contextMenuModel.labelsSectionVisible
                    headerWidth: prv.headerWidth

                    channelItemWidth: prv.channelItemWidth
                    headerPinOffsetX: flickable.contentX
                    masterPinOffsetX: prv.masterPinOffsetX

                    model: mixerPanelModel

                    navigationRowStart: 200
                    needReadChannelName: prv.isPanelActivated

                    onNavigateControlIndexChanged: function(index) {
                        prv.setNavigateControlIndex(index)
                    }
                }

                MixerBalanceSection {
                    id: balanceSection

                    visible: contextMenuModel.balanceSectionVisible
                    headerVisible: contextMenuModel.labelsSectionVisible
                    headerWidth: prv.headerWidth
                    channelItemWidth: prv.channelItemWidth
                    headerPinOffsetX: flickable.contentX
                    masterPinOffsetX: prv.masterPinOffsetX

                    model: mixerPanelModel

                    navigationRowStart: 300
                    needReadChannelName: prv.isPanelActivated

                    onNavigateControlIndexChanged: function(index) {
                        prv.setNavigateControlIndex(index)
                    }
                }

                MixerVolumeSection {
                    id: volumeSection

                    visible: contextMenuModel.volumeSectionVisible
                    headerVisible: contextMenuModel.labelsSectionVisible
                    headerWidth: prv.headerWidth
                    channelItemWidth: prv.channelItemWidth
                    headerPinOffsetX: flickable.contentX
                    masterPinOffsetX: prv.masterPinOffsetX

                    model: mixerPanelModel

                    navigationRowStart: 400
                    needReadChannelName: prv.isPanelActivated

                    onNavigateControlIndexChanged: function(index) {
                        prv.setNavigateControlIndex(index)
                    }
                }

                MixerFaderSection {
                    id: faderSection

                    visible: contextMenuModel.faderSectionVisible
                    headerVisible: contextMenuModel.labelsSectionVisible
                    headerWidth: prv.headerWidth
                    channelItemWidth: prv.channelItemWidth
                    headerPinOffsetX: flickable.contentX
                    masterPinOffsetX: prv.masterPinOffsetX
                    spacingAbove: -3
                    spacingBelow: -2

                    model: mixerPanelModel

                    navigationRowStart: 500
                    needReadChannelName: prv.isPanelActivated

                    onNavigateControlIndexChanged: function(index) {
                        prv.setNavigateControlIndex(index)
                    }
                }

                MixerMuteAndSoloSection {
                    id: muteAndSoloSection

                    visible: contextMenuModel.muteAndSoloSectionVisible
                    headerVisible: contextMenuModel.labelsSectionVisible
                    headerWidth: prv.headerWidth
                    channelItemWidth: prv.channelItemWidth
                    headerPinOffsetX: flickable.contentX
                    masterPinOffsetX: prv.masterPinOffsetX

                    model: mixerPanelModel

                    navigationRowStart: 600
                    needReadChannelName: prv.isPanelActivated

                    onNavigateControlIndexChanged: function(index) {
                        prv.setNavigateControlIndex(index)
                    }
                }

                MixerTitleSection {
                    id: titleSection

                    visible: contextMenuModel.titleSectionVisible
                    headerVisible: contextMenuModel.labelsSectionVisible
                    headerWidth: prv.headerWidth
                    channelItemWidth: prv.channelItemWidth
                    headerPinOffsetX: flickable.contentX
                    masterPinOffsetX: prv.masterPinOffsetX
                    spacingAbove: 2
                    spacingBelow: 0

                    model: mixerPanelModel

                    navigationRowStart: 700
                    needReadChannelName: prv.isPanelActivated

                    onNavigateControlIndexChanged: function(index) {
                        prv.setNavigateControlIndex(index)
                    }
                }
        }
    }

    Column {
        spacing: 0
        Layout.fillWidth: true
        visible: horizontalScrollBar.isScrollbarNeeded

        SeparatorLine {}

        StyledScrollBar {
            id: horizontalScrollBar
            width: parent.width
            policy: ScrollBar.AlwaysOn
        }
    }

    onHeightChanged: {
        flickable.positionViewAtEnd()
    }
}
