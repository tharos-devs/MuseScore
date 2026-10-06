/*
 * SPDX-License-Identifier: GPL-3.0-only
 * MuseScore-Studio-CLA-applies
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

import QtQuick
import QtQuick.Layouts
import QtQuick.Window

import Muse.Ui
import Muse.UiComponents
import MuseScore.Playback

Item {
    id: root

    property NavigationSection navigationSection: null
    property int contentNavigationPanelOrderStart: 0

    // NOTE: whether the enclosing DockPanel is currently floating (undocked)
    // -- fed in from NotationPage.qml (`floating: videoPanel.floating` on this
    // component's own instantiation), since this file can't reference that
    // panel's id directly (separate document). Full screen only makes sense
    // once this panel is its own floating window -- when docked, there's no
    // separate window to fullscreen, so "Full screen" would end up
    // fullscreening the whole MuseScore window instead, which isn't what a
    // user asking to fullscreen just the video expects (e.g. floating this
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

    // NOTE: this model -- unlike VideoPanel.qml itself -- is NOT behind the
    // lazy Loader below, so it's already loaded by the time the
    // enclosing DockPanel's own content actually needs it (pushed up via
    // Component.onCompleted on this component's own instantiation in
    // NotationPage.qml, the same pattern Mixer/Piano keyboard already use --
    // a plain property binding at the DockPanel level doesn't work here since
    // this whole file is the DockPanel's lazily-loaded default-property
    // content, only instantiated once the panel is actually shown).
    property alias contextMenuModel: contextMenuModel

    // NOTE: exposed so NotationPage.qml's DockPanel can relax its own
    // minimumWidth when docked -- with the sidebar below the timeline instead
    // of beside it, the panel's real content floor is much narrower (roughly
    // previewPaneMinWidth alone), which is what actually makes docking this
    // panel to a narrow left/right column practical. Read from
    // videoPanelLoader.item directly (rather than only via the contextMenuModel
    // copy above) since this needs to be usable even before that model exists.
    // NOTE: where the sidebar actually is (see dockedAtSide), not only the user's choice
    readonly property bool hitPointsPanelBelowTimeline: videoPanelLoader.item ? videoPanelLoader.item.sidebarBelow : false

    // NOTE: docked at the left or right of the score (set by NotationPage.qml): too narrow for the sidebar
    // beside the video, it always goes below the timeline there
    property bool dockedAtSide: false
    onDockedAtSideChanged: updateLoadedItem()

    // NOTE: same reason - a hidden sidebar doesn't take any width either, wherever it's placed
    readonly property bool hitPointsPanelVisible: videoPanelLoader.item ? videoPanelLoader.item.hitPointsPanelVisible : true
    readonly property bool needsWideMinimumWidth: hitPointsPanelVisible && !hitPointsPanelBelowTimeline

    // NOTE: the real height floor of the content (VideoPanel.qml's dockedMinimumHeight), for the DockPanel's
    // minimumHeight; 280 = its previewPaneMinHeight, until loaded
    //! NOTE: the "…" menu's zoom, applied to the content: its minimum sizes follow it
    readonly property real contentZoom: contextMenuModel.zoom
    readonly property int contentMinimumHeight: Math.ceil((videoPanelLoader.item ? videoPanelLoader.item.dockedMinimumHeight : 280) * contentZoom)

    // NOTE: kept loaded once it had a size: resizing a column it shares with other panels can report a 0
    // size for a moment, and unloading then would reset the panel's layout (and its minimum width)
    readonly property bool hasSize: width > 0 && height > 0
    property bool hadSize: false
    onHasSizeChanged: {
        if (hasSize) {
            hadSize = true
        }
    }

    readonly property bool shouldLoadPanel: hasSize || hadSize

    // NOTE: true once VideoPanel.qml has read its saved layout: hitPointsPanelBelowTimeline & co. (and so the
    // minimum sizes above) are only its defaults before. Not Loader.onLoaded: created along with the DockPanel's
    // content, the loaded item's Component.onCompleted only runs after it - reading them there pushed the wide
    // minimum width (640) for a moment, enough to widen the restored column for good
    readonly property bool contentReady: videoPanelLoader.item ? videoPanelLoader.item.stateRestored : false

    // NOTE: fired once contentReady - the first reliable point to read hitPointsPanelBelowTimeline & co.
    signal panelReady()

    onContentReadyChanged: {
        if (contentReady) {
            root.panelReady()
        }
    }

    // NOTE: this does NOT use QWindow.showFullScreen()/showNormal() at all --
    // tried that first, but it turned out unreliable specifically for this
    // window type (Qt::Tool, frameless -- see KDDockWidgets' FloatingWindow
    // setup): showNormal() correctly flipped `visibility` back to Windowed
    // but never restored x/y/width/height (confirmed via logging: stayed at
    // the full-screen geometry), and manually restoring geometry afterwards
    // -- even with a delay -- left the window unable to re-enter full screen
    // on a later toggle (some internal native full-screen state apparently
    // stays stuck; docking then re-undocking the panel, which recreates the
    // underlying window, was the only thing that reset it). Doing this as a
    // plain geometry resize instead -- filling the window's current screen,
    // no native full-screen transition involved at all -- sidesteps that
    // stuck state entirely, at the cost of not hiding the OS menu bar/dock on
    // the screen it's on the way real OS full screen would.
    property var savedGeometry: null

    VideoPanelContextMenuModel {
        id: contextMenuModel

        floating: root.floating
        isFullScreen: root.isFullScreen
        // NOTE: forwarded from the lazily-loaded VideoPanel.qml (the actual
        // owner of the sidebar's layout, via videoPanelLoader.item below) --
        // this model has no layout of its own, same reasoning as
        // screenAvailableGeometry() needing this file to reach into the real
        // window rather than owning that logic itself.
        hitPointsPanelBelowTimeline: root.hitPointsPanelBelowTimeline
        hitPointsPanelPlacementLocked: root.dockedAtSide
        hitPointsPanelVisible: videoPanelLoader.item ? videoPanelLoader.item.hitPointsPanelVisible : true
        timelineVisible: videoPanelLoader.item ? videoPanelLoader.item.timelineVisible : true
        controlsVisible: videoPanelLoader.item ? videoPanelLoader.item.controlsVisible : true

        Component.onCompleted: contextMenuModel.load()

        onSetHitPointsPanelBelowTimelineRequested: function(belowTimeline) {
            if (videoPanelLoader.item) {
                videoPanelLoader.item.setHitPointsPanelBelowTimeline(belowTimeline)
            }
        }

        onToggleHitPointsPanelVisibleRequested: {
            if (videoPanelLoader.item) {
                videoPanelLoader.item.toggleHitPointsPanelVisible()
            }
        }

        onToggleTimelineVisibleRequested: {
            if (videoPanelLoader.item) {
                videoPanelLoader.item.toggleTimelineVisible()
            }
        }

        onToggleControlsVisibleRequested: {
            if (videoPanelLoader.item) {
                videoPanelLoader.item.toggleControlsVisible()
            }
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

    function updateLoadedItem() {
        if (!videoPanelLoader.item) {
            return
        }

        videoPanelLoader.item.navigationSection = root.navigationSection
        videoPanelLoader.item.contentNavigationPanelOrderStart = root.contentNavigationPanelOrderStart
        videoPanelLoader.item.dockedAtSide = root.dockedAtSide
    }

    onNavigationSectionChanged: updateLoadedItem()
    onContentNavigationPanelOrderStartChanged: updateLoadedItem()

    ZoomContainer {
        anchors.fill: parent

        zoom: root.contentZoom

        Loader {
            id: videoPanelLoader

            anchors.fill: parent
            active: root.shouldLoadPanel
            //! NOTE: synchronous: an asynchronous load could stay stuck in Loader.Loading for good when
            //! the panel was restored open along with a score, leaving it empty until hidden and shown
            //! again. It stays lazy: QtMultimedia is only loaded once the panel is actually shown
            source: root.shouldLoadPanel ? "VideoPanel.qml" : ""

            onLoaded: {
                root.updateLoadedItem()
            }
        }
    }

    Rectangle {
        anchors.fill: parent

        visible: root.shouldLoadPanel && videoPanelLoader.status === Loader.Error
        color: ui.theme.backgroundPrimaryColor

        ColumnLayout {
            anchors.centerIn: parent
            width: Math.max(0, Math.min(parent.width - 48, 560))
            spacing: 12

            StyledTextLabel {
                Layout.fillWidth: true
                horizontalAlignment: Text.AlignHCenter
                font: ui.theme.headerBoldFont
                text: qsTrc("playback", "Video playback is unavailable")
            }

            StyledTextLabel {
                Layout.fillWidth: true
                horizontalAlignment: Text.AlignHCenter
                wrapMode: Text.WordWrap
                opacity: 0.75
                text: qsTrc("playback", "MuseScore could not load the Qt Multimedia module required by the Video panel. This build may be missing QtMultimedia in its package.")
            }
        }
    }
}
