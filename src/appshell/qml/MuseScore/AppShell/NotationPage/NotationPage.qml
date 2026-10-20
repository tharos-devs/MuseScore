/*
 * SPDX-License-Identifier: GPL-3.0-only
 * MuseScore-Studio-CLA-applies
 *
 * MuseScore Studio
 * Music Composition & Notation
 *
 * Copyright (C) 2024 MuseScore Limited and others
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
import QtQuick.Window
import QtQml.Models

import Muse.Ui
import Muse.UiComponents
import Muse.Dock
import Muse.Extensions
import MuseScore.AppShell

import MuseScore.NotationScene
import MuseScore.Palette
import MuseScore.PropertiesPanel
import MuseScore.InstrumentsScene
import MuseScore.Playback

DockPage {
    id: root

    objectName: "Notation"
    uri: "musescore://notation"

    secondaryWindowAvailable: true

    required property NavigationSection topToolbarKeyNavSec

    property NotationPageModel pageModel: NotationPageModel {}

    property NavigationSection noteInputKeyNavSec: NavigationSection {
        name: "NoteInputSection"
        order: 2
    }

    property NavigationSection keynavTopPanelSec: NavigationSection {
        name: "NavigationTopPanel"
        enabled: root.visible
        order: 3
    }

    property NavigationSection keynavLeftPanelSec: NavigationSection {
        name: "NavigationLeftPanel"
        enabled: root.visible
        order: 4
    }

    property NavigationSection keynavRightPanelSec: NavigationSection {
        name: "NavigationRightPanel"
        enabled: root.visible
        order: 6
    }

    property NavigationSection keynavBottomPanelSec: NavigationSection {
        name: "NavigationBottomPanel"
        enabled: root.visible
        order: 7
    }

    function navigationPanelSec(location) {
        switch(location) {
        case Location.Top: return keynavTopPanelSec
        case Location.Left: return keynavLeftPanelSec
        case Location.Right: return keynavRightPanelSec
        case Location.Bottom: return keynavBottomPanelSec
        }

        return null
    }

    onInited: {
        Qt.callLater(pageModel.init)
    }

    readonly property int verticalPanelDefaultWidth: 300

    readonly property int horizontalPanelMinHeight: 100
    readonly property int horizontalPanelMaxHeight: 520

    readonly property int panelMinDimension: 10
    readonly property int panelMaxDimension: 7500 //! NOTE: Value found experimentally - see issue #27770

    readonly property string verticalPanelsGroup: "VERTICAL_PANELS"
    readonly property string horizontalPanelsGroup: "HORIZONTAL_PANELS"

    readonly property var verticalPanelDropDestinations: [
        { "dock": root.centralDock, "dropLocation": Location.Left, "dropDistance": root.verticalPanelDefaultWidth },
        { "dock": root.centralDock, "dropLocation": Location.Right, "dropDistance": root.verticalPanelDefaultWidth }
    ]

    readonly property var horizontalPanelDropDestinations: [
        root.panelTopDropDestination,
        root.panelBottomDropDestination
    ]

    //! NOTE: unlike the other horizontal panels (Mixer, Piano keyboard, Percussion,
    //! Selection Filter), Video and Timeline also make sense as a side panel (Video's
    //! own internal sidebar can be moved below its timeline via its context menu to
    //! fit that width) -- so they get top/bottom PLUS left/right, instead of reusing
    //! horizontalPanelDropDestinations verbatim.
    readonly property var horizontalOrSidePanelDropDestinations: [
        root.panelTopDropDestination,
        root.panelBottomDropDestination,
        { "dock": root.centralDock, "dropLocation": Location.Left, "dropDistance": root.verticalPanelDefaultWidth },
        { "dock": root.centralDock, "dropLocation": Location.Right, "dropDistance": root.verticalPanelDefaultWidth }
    ]

    //! NOTE: the side panels with a fixed width (verticalPanelDefaultWidth). A resizable side panel (Track list,
    //! Video) takes their width when it's tabbed or stacked with one of them in their column: it would otherwise set
    //! the whole column's width. Anywhere else (alone at a side, with each other, floating...), it stays resizable
    function resizableSidePanelMaximumWidth(panel) {
        const fixedSidePanels = [ palettesPanel, layoutPanel, propertiesPanel, selectionFilterPanel, undoHistoryPanel ]
        const withFixedPanel = fixedSidePanels.some(other => other.visible
                                                    && (panel.isInSameFrame(other)
                                                        || (typeof panel.isInSameColumn === "function" && panel.isInSameColumn(other))))

        return withFixedPanel ? root.verticalPanelDefaultWidth : root.panelMaxDimension
    }

    //! NOTE: a fixed width, narrower than the other side panels' (the instruments' names and its strip's two columns
    //! still fit) when alone in its column; theirs beside them (a column has one width)
    readonly property int trackListPanelWidth: 256

    function updateTrackListPanelWidthLimits() {
        const width = resizableSidePanelMaximumWidth(trackListPanel) === root.verticalPanelDefaultWidth
                    ? root.verticalPanelDefaultWidth : root.trackListPanelWidth
        trackListPanel.minimumWidth = Math.min(trackListPanel.minimumWidth, width)
        trackListPanel.maximumWidth = width
        trackListPanel.minimumWidth = width
    }

    function updateResizableSidePanelsMaximumWidth() {
        updateTrackListPanelWidthLimits()
        for (const panel of [ videoPanel, timelinePanel ]) {
            if (panel.dockPlacementUpdater) {
                panel.dockPlacementUpdater()
            }
        }
    }

    //! NOTE: whether a panel (its content item) is docked at the left or right of the score - the framework only
    //! knows the declared location (see DockPageView::actualLocation()), so this compares the geometries: true/false,
    //! or undefined when it can't be known yet (no score view while a project opens, no size for a moment while
    //! resizing its column, or as a tab not shown) - it stays where it was then
    function isDockedAtSide(panel, item) {
        if (!notationView) {
            return undefined
        }

        if (panel.floating || item.Window.window !== notationView.Window.window) {
            return false
        }

        if (item.width <= 0 || item.height <= 0 || notationView.width <= 0 || notationView.height <= 0) {
            return undefined
        }

        //! NOTE: beside the score, not only past its left/right edge: a bottom panel may span further
        const pos = item.mapToItem(notationView, 0, 0)
        const besideVertically = pos.y < notationView.height && pos.y + item.height > 0
        return besideVertically && (pos.x + item.width <= 1 || pos.x >= notationView.width - 1)
    }

    property var notationView: null

    mainToolBars: [
        DockToolBar {
            id: notationToolBar

            objectName: "notationToolBar"
            title: qsTrc("appshell", "Notation toolbar")

            floatable: false
            closable: false
            resizable: false
            separatorsVisible: false

            alignment: DockToolBarAlignment.Center
            contentBottomPadding: 2

            compactPriorityOrder: 1

            navigationSection: root.topToolbarKeyNavSec

            NotationToolBar {
                isCompactMode: notationToolBar.isCompact

                navigationPanel.section: notationToolBar.navigationSection
                navigationPanel.order: 2
            }
        },

        DockToolBar {
            id: playbackToolBar

            objectName: root.pageModel.playbackToolBarName()
            title: qsTrc("appshell", "Playback controls")

            separatorsVisible: false
            alignment: DockToolBarAlignment.Right
            resizable: !floating

            contentBottomPadding: floating ? 8 : 2
            contentTopPadding: floating ? 8 : 0

            dropDestinations: [
                { "dock": notationToolBar, "dropLocation": Location.Right }
            ]

            navigationSection: root.topToolbarKeyNavSec

            PlaybackToolBar {
                navigationPanelSection: playbackToolBar.navigationSection
                navigationPanelOrder: 3

                floating: playbackToolBar.floating
            }
        },

        DockToolBar {
            id: extDockToolBar

            objectName: root.pageModel.extensionsToolBarName()
            title: qsTrc("appshell", "Extensions toolbar")

            separatorsVisible: false
            orientation: Qt.Horizontal
            alignment: DockToolBarAlignment.Right

            contentBottomPadding: floating ? 8 : 2
            contentTopPadding: floating ? 8 : 0

            //! NOTE: Opened by page model when there are extensions with toolbar actions
            visible: false

            dropDestinations: [
                { "dock": notationToolBar, "dropLocation": Location.Right },
                { "dock": playbackToolBar, "dropLocation": Location.Right }
            ]

            navigationSection: root.topToolbarKeyNavSec

            ExtensionsToolBar {
                id: extToolBar

                navigationPanel.section: extDockToolBar.navigationSection
                navigationPanel.order: 4
            }
        },

        DockToolBar {
            id: undoRedoToolBar

            objectName: root.pageModel.undoRedoToolBarName()
            title: qsTrc("appshell", "Undo/redo")

            floatable: false
            closable: false
            resizable: false
            separatorsVisible: false

            alignment: DockToolBarAlignment.Right
            contentBottomPadding: 2

            navigationSection: root.topToolbarKeyNavSec

            UndoRedoToolBar {
                navigationPanel.section: undoRedoToolBar.navigationSection
                navigationPanel.order: 5
            }
        }
    ]

    toolBars: [
        DockToolBar {
            id: noteInputBar

            objectName: root.pageModel.noteInputBarName()
            title: qsTrc("appshell", "Note input")

            dropDestinations: [
                root.toolBarTopDropDestination,
                root.toolBarBottomDropDestination,
                root.toolBarLeftDropDestination,
                root.toolBarRightDropDestination
            ]

            thickness: orientation === Qt.Horizontal ? 40 : 76
            resizable: !floating

            navigationSection: root.noteInputKeyNavSec

            NoteInputBar {
                orientation: noteInputBar.orientation
                floating: noteInputBar.floating

                maximumWidth: noteInputBar.width
                maximumHeight: noteInputBar.height

                navigationPanel.section: noteInputBar.navigationSection
                navigationPanel.order: 1
            }
        }
    ]

    panels: [
        DockPanel {
            id: palettesPanel

            objectName: root.pageModel.palettesPanelName()
            title: qsTrc("appshell", "Palettes")

            navigationSection: root.navigationPanelSec(palettesPanel.location)

            width: root.verticalPanelDefaultWidth
            minimumWidth: root.verticalPanelDefaultWidth
            maximumWidth: root.verticalPanelDefaultWidth

            minimumHeight: root.panelMinDimension
            maximumHeight: root.panelMaxDimension

            groupName: root.verticalPanelsGroup

            dropDestinations: root.verticalPanelDropDestinations

            PalettesPanel {
                navigationSection: palettesPanel.navigationSection
                navigationOrderStart: palettesPanel.contentNavigationPanelOrderStart

                Component.onCompleted: {
                    palettesPanel.contextMenuModel = contextMenuModel
                }
            }
        },

        DockPanel {
            id: secondaryPalettesPanel

            objectName: root.pageModel.secondaryPalettesPanelName()
            //: The secondary palettes panel's title: short, the panel is narrow
            title: qsTrc("appshell", "2nd Palettes")

            navigationSection: root.navigationPanelSec(secondaryPalettesPanel.location)

            //! NOTE: narrow, its content's width (see updateWidth() below): not in the side panels' group, which would
            //! tab it with the wide ones
            width: 150
            minimumWidth: 150
            maximumWidth: 150

            minimumHeight: root.panelMinDimension
            maximumHeight: root.panelMaxDimension

            location: Location.Left
            secondaryWindowAllowed: true

            //! NOTE: hidden by default
            visible: false

            dropDestinations: root.verticalPanelDropDestinations

            SecondaryPalettesPanel {
                navigationSection: secondaryPalettesPanel.navigationSection
                navigationOrderStart: secondaryPalettesPanel.contentNavigationPanelOrderStart

                //! NOTE: from here: the panel's content is a component, out of reach of the panel's bindings.
                //! A fixed width: the minimum lowered first when narrowing, the maximum raised first when widening
                function updateWidth() {
                    const width = implicitWidth
                    if (width <= 0) {
                        return
                    }

                    if (width < secondaryPalettesPanel.minimumWidth) {
                        secondaryPalettesPanel.minimumWidth = width
                        secondaryPalettesPanel.maximumWidth = width
                    } else {
                        secondaryPalettesPanel.maximumWidth = width
                        secondaryPalettesPanel.minimumWidth = width
                    }
                }

                onImplicitWidthChanged: updateWidth()

                Component.onCompleted: {
                    secondaryPalettesPanel.contextMenuModel = contextMenuModel
                    updateWidth()
                }

                Component.onDestruction: {
                    secondaryPalettesPanel.contextMenuModel = null
                }
            }
        },

        DockPanel {
            id: layoutPanel

            objectName: root.pageModel.layoutPanelName()
            title: qsTrc("appshell", "Layout")

            navigationSection: root.navigationPanelSec(layoutPanel.location)

            width: root.verticalPanelDefaultWidth
            minimumWidth: root.verticalPanelDefaultWidth
            maximumWidth: root.verticalPanelDefaultWidth

            minimumHeight: root.panelMinDimension
            maximumHeight: root.panelMaxDimension

            groupName: root.verticalPanelsGroup

            dropDestinations: root.verticalPanelDropDestinations

            LayoutPanel {
                navigationSection: layoutPanel.navigationSection
                navigationOrderStart: layoutPanel.contentNavigationPanelOrderStart

                Component.onCompleted: {
                    layoutPanel.contextMenuModel = contextMenuModel
                }
            }
        },

        DockPanel {
            id: propertiesPanel

            objectName: root.pageModel.propertiesPanelName()
            title: qsTrc("appshell", "Properties")

            navigationSection: root.navigationPanelSec(propertiesPanel.location)

            width: root.verticalPanelDefaultWidth
            minimumWidth: root.verticalPanelDefaultWidth
            maximumWidth: root.verticalPanelDefaultWidth

            minimumHeight: root.panelMinDimension
            maximumHeight: root.panelMaxDimension

            groupName: root.verticalPanelsGroup

            dropDestinations: root.verticalPanelDropDestinations

            PropertiesPanel {
                navigationSection: propertiesPanel.navigationSection
                navigationOrderStart: propertiesPanel.contentNavigationPanelOrderStart
                notationView: root.notationView

                Component.onCompleted: {
                    propertiesPanel.contextMenuModel = contextMenuModel
                }
            }
        },

        DockPanel {
            id: selectionFilterPanel

            objectName: root.pageModel.selectionFiltersPanelName()
            title: qsTrc("appshell", "Selection filter")

            navigationSection: root.navigationPanelSec(selectionFilterPanel.location)

            width: root.verticalPanelDefaultWidth
            minimumWidth: root.verticalPanelDefaultWidth
            maximumWidth: root.verticalPanelDefaultWidth

            minimumHeight: root.panelMinDimension
            maximumHeight: root.panelMaxDimension

            groupName: root.verticalPanelsGroup

            //! NOTE: hidden by default
            visible: false

            dropDestinations: root.verticalPanelDropDestinations

            SelectionFilterPanel {
                navigationSection: selectionFilterPanel.navigationSection
                navigationOrderStart: selectionFilterPanel.contentNavigationPanelOrderStart
            }
        },

        DockPanel {
            id: undoHistoryPanel

            objectName: root.pageModel.undoHistoryPanelName()
            title: qsTrc("notation", "History")

            navigationSection: root.navigationPanelSec(undoHistoryPanel.location)

            width: root.verticalPanelDefaultWidth
            minimumWidth: root.verticalPanelDefaultWidth
            maximumWidth: root.verticalPanelDefaultWidth

            minimumHeight: root.panelMinDimension
            maximumHeight: root.panelMaxDimension

            groupName: root.verticalPanelsGroup
            location: Location.Right

            //! NOTE: hidden by default
            visible: false

            dropDestinations: root.verticalPanelDropDestinations

            UndoHistoryPanel {
                navigationSection: undoHistoryPanel.navigationSection
                navigationOrderStart: undoHistoryPanel.contentNavigationPanelOrderStart
            }
        },

        DockPanel {
            id: trackListPanel

            objectName: root.pageModel.trackListPanelName()
            title: qsTrc("appshell", "Track list")

            navigationSection: root.navigationPanelSec(trackListPanel.location)

            width: root.trackListPanelWidth
            //! NOTE: fixed, see updateTrackListPanelWidthLimits()
            minimumWidth: root.trackListPanelWidth
            maximumWidth: root.trackListPanelWidth

            minimumHeight: root.panelMinDimension
            maximumHeight: root.panelMaxDimension

            groupName: root.verticalPanelsGroup
            secondaryWindowAllowed: true

            //! NOTE: hidden by default
            visible: false

            dropDestinations: root.verticalPanelDropDestinations

            TrackListPanel {
                navigationSection: trackListPanel.navigationSection
                contentNavigationPanelOrderStart: trackListPanel.contentNavigationPanelOrderStart

                //! NOTE: like the Video panel's (see resizableSidePanelMaximumWidth()): a move always changes the
                //! panel's size (or window)
                function updateMaximumWidth() {
                    root.updateTrackListPanelWidthLimits()
                }

                readonly property var hostWindow: Window.window
                onHostWindowChanged: Qt.callLater(updateMaximumWidth)
                onWidthChanged: Qt.callLater(updateMaximumWidth)
                onHeightChanged: Qt.callLater(updateMaximumWidth)

                //! NOTE: also when a fixed side panel opens or closes (its column may be this one's)
                Instantiator {
                    model: [ palettesPanel, layoutPanel, propertiesPanel, selectionFilterPanel, undoHistoryPanel ]

                    delegate: Connections {
                        required property var modelData
                        target: modelData

                        function onVisibleChanged() {
                            Qt.callLater(root.updateResizableSidePanelsMaximumWidth)
                        }
                    }
                }

                Component.onCompleted: {
                    trackListPanel.contextMenuModel = contextMenuModel
                    updateMaximumWidth()
                }

                Component.onDestruction: {
                    trackListPanel.contextMenuModel = null
                }
            }
        },
        
        // =============================================
        // Horizontal Panels
        // =============================================

        DockPanel {
            id: mixerPanel

            objectName: root.pageModel.mixerPanelName()
            title: qsTrc("appshell", "Mixer")

            height: 368
            minimumHeight: root.horizontalPanelMinHeight
            //! NOTE: unlike the other horizontal panels, Mixer can be floated and
            //! then full-screened (see MixerPanel.qml/mixerpanelcontextmenumodel) --
            //! the shared 520px cap would otherwise clamp the floating window well
            //! below screen height, leaving it unable to actually fill the screen
            //! and forcing a scrollbar on its channel strips instead. This also
            //! covers docked use: taller than the shared cap alone, to fit up to
            //! 5 FX slots and up to 5 aux-send slots per channel without a scrollbar.
            maximumHeight: root.panelMaxDimension

            minimumWidth: root.panelMinDimension
            maximumWidth: root.panelMaxDimension

            groupName: root.horizontalPanelsGroup
            secondaryWindowAllowed: true

            //! NOTE: hidden by default
            visible: false

            location: Location.Bottom

            dropDestinations: root.horizontalPanelDropDestinations

            navigationSection: root.navigationPanelSec(mixerPanel.location)

            MixerPanel {
                id: mixerPanelComponent

                navigationSection: mixerPanel.navigationSection
                contentNavigationPanelOrderStart: mixerPanel.contentNavigationPanelOrderStart
                floating: mixerPanel.floating

                Component.onCompleted: {
                    mixerPanel.contextMenuModel = contextMenuModel
                    mixerPanel.toolbarComponent = toolbarComponent
                }

                Component.onDestruction: {
                    mixerPanel.contextMenuModel = null
                    mixerPanel.toolbarComponent = null
                }

                onResizeRequested: function(newWidth, newHeight) {
                    mixerPanel.resize(newWidth, newHeight)
                }

                Connections {
                    target: mixerPanel
                    function onPanelShown() {
                        mixerPanelComponent.resizePanelToContentHeight()
                    }
                }
            }
        },

        DockPanel {
            id: pianoKeyboardPanel

            objectName: root.pageModel.pianoKeyboardPanelName()
            title: qsTrc("appshell", "Piano keyboard")

            height: 200
            minimumHeight: root.horizontalPanelMinHeight
            maximumHeight: root.horizontalPanelMaxHeight

            minimumWidth: root.panelMinDimension
            maximumWidth: root.panelMaxDimension

            groupName: root.horizontalPanelsGroup
            secondaryWindowAllowed: true

            //! NOTE: hidden by default
            visible: false

            location: Location.Bottom

            dropDestinations: root.horizontalPanelDropDestinations

            navigationSection: root.navigationPanelSec(pianoKeyboardPanel.location)

            PianoKeyboardPanel {
                navigationSection: pianoKeyboardPanel.navigationSection
                contentNavigationPanelOrderStart: pianoKeyboardPanel.contentNavigationPanelOrderStart

                Component.onCompleted: {
                    pianoKeyboardPanel.contextMenuModel = contextMenuModel
                }
            }
        },

        DockPanel {
            id: audioSyncPanel

            objectName: root.pageModel.audioSyncPanelName()
            title: qsTrc("appshell", "Audio sync")

            height: 240
            minimumHeight: root.horizontalPanelMinHeight
            maximumHeight: root.horizontalPanelMaxHeight

            minimumWidth: root.panelMinDimension
            maximumWidth: root.panelMaxDimension

            groupName: root.horizontalPanelsGroup
            secondaryWindowAllowed: true

            //! NOTE: hidden by default
            visible: false

            location: Location.Bottom

            dropDestinations: root.horizontalPanelDropDestinations

            navigationSection: root.navigationPanelSec(audioSyncPanel.location)

            AudioSyncPanel {
                navigationSection: audioSyncPanel.navigationSection
                contentNavigationPanelOrderStart: audioSyncPanel.contentNavigationPanelOrderStart
            }
        },

        DockPanel {
            id: timelinePanel

            objectName: root.pageModel.timelinePanelName()

            //! NOTE: its content's updateDockPlacement(), while it's loaded - the ids inside the content can't be
            //! reached from the page: the panel loads it as a component of its own. Called through this property,
            //! the function gets the panel as `this`: it names its content by id
            property var dockPlacementUpdater: null
            title: qsTrc("appshell", "Timeline")

            height: 200
            minimumHeight: root.horizontalPanelMinHeight
            maximumHeight: root.horizontalPanelMaxHeight

            minimumWidth: root.panelMinDimension
            maximumWidth: root.panelMaxDimension

            groupName: root.horizontalPanelsGroup
            //! NOTE: at the left or right, it goes with the side panels instead (e.g. below Palettes, or tabbed with them)
            sideGroupName: root.verticalPanelsGroup
            secondaryWindowAllowed: true

            //! NOTE: hidden by default
            visible: false

            location: Location.Bottom

            dropDestinations: root.horizontalOrSidePanelDropDestinations

            navigationSection: root.navigationPanelSec(timelinePanel.location)

            Timeline {
                id: timelineInstance

                navigationSection: timelinePanel.navigationSection
                contentNavigationPanelOrderStart: timelinePanel.contentNavigationPanelOrderStart

                //! NOTE: like the Video panel's: at a side, it takes the side panels' fixed width when tabbed or
                //! stacked with one of them (see resizableSidePanelMaximumWidth()), and is as tall as its column -
                //! elsewhere it keeps the horizontal panels' maximum height. A move always changes the panel's size
                //! (or window)
                function updateDockPlacement() {
                    timelinePanel.maximumWidth = root.resizableSidePanelMaximumWidth(timelinePanel)

                    const atSide = root.isDockedAtSide(timelinePanel, timelineInstance)
                    if (atSide !== undefined) {
                        timelinePanel.maximumHeight = atSide ? root.panelMaxDimension : root.horizontalPanelMaxHeight
                    }
                }

                readonly property var hostWindow: Window.window
                onHostWindowChanged: Qt.callLater(updateDockPlacement)
                onWidthChanged: Qt.callLater(updateDockPlacement)
                onHeightChanged: Qt.callLater(updateDockPlacement)

                Connections {
                    target: timelinePanel

                    function onFloatingChanged() {
                        Qt.callLater(timelineInstance.updateDockPlacement)
                    }
                }

                Component.onCompleted: {
                    timelinePanel.contextMenuModel = contextMenuModel
                    timelinePanel.dockPlacementUpdater = updateDockPlacement
                    Qt.callLater(updateDockPlacement)
                }

                Component.onDestruction: {
                    timelinePanel.contextMenuModel = null
                    timelinePanel.dockPlacementUpdater = null
                }
            }
        },

        DockPanel {
            id: percussionPanel

            objectName: root.pageModel.percussionPanelName()
            title: qsTrc("appshell", "Percussion")

            height: 200
            minimumHeight: root.horizontalPanelMinHeight
            maximumHeight: root.horizontalPanelMaxHeight

            minimumWidth: root.panelMinDimension
            maximumWidth: root.panelMaxDimension

            groupName: root.horizontalPanelsGroup

            //! NOTE: hidden by default
            visible: false

            location: Location.Bottom

            dropDestinations: root.horizontalPanelDropDestinations

            navigationSection: root.navigationPanelSec(percussionPanel.location)

            PercussionPanel {
                id: percussionComponent

                navigationSection: percussionPanel.navigationSection
                contentNavigationPanelOrderStart: percussionPanel.contentNavigationPanelOrderStart

                Component.onCompleted: {
                    percussionPanel.toolbarComponent = toolbarComponent
                }

                Component.onDestruction: {
                    percussionPanel.toolbarComponent = null
                }

                onResizeRequested: function(newWidth, newHeight) {
                    percussionPanel.resize(newWidth, newHeight)
                }

                Connections {
                    target: percussionPanel
                    function onPanelShown() {
                        percussionComponent.resizePanelToContentHeight()
                    }
                }
            }
        },

        DockPanel {
            id: videoPanel

            objectName: root.pageModel.videoPanelName()

            //! NOTE: see timelinePanel's
            property var dockPlacementUpdater: null
            title: qsTrc("appshell", "Video")

            height: 360
            //! NOTE: the real floor of its content is pushed below once loaded (see onContentMinimumHeightChanged)
            minimumHeight: root.horizontalPanelMinHeight
            //! NOTE: unlike other horizontal panels (Timeline, Selection Filter), the
            //! Video panel benefits from being resized/maximized much larger when
            //! floated -- so it gets its own ceiling instead of the shared 520px one.
            maximumHeight: root.panelMaxDimension

            //! NOTE: unlike other horizontal panels, Video has an internal SplitView
            //! (preview+transport | hit-points panel) whose two sides have their own
            //! minimums (320 + 240 = 560px); too narrow a window/floating panel makes
            //! that split collapse/overlap, so it needs a real floor instead of
            //! panelMinDimension. The extra margin above 560 covers panel chrome
            //! (title bar, borders) that isn't part of the SplitView's own content area.
            //! With the sidebar moved below the timeline instead (chosen via the
            //! panel's own "..." menu), that internal split becomes vertical and no
            //! longer eats into the width budget -- the floor drops to roughly the
            //! preview+transport column's own minimum (VideoPanel.qml's
            //! previewPaneMinWidth) plus the same chrome margin.
            //! NOTE: the side panels' fixed width: docked in their column, it fits without widening it (the
            //! sidebar always goes below the timeline there, see dockedAtSide below)
            //! NOTE: 300 here is the narrower, stacked-layout floor -- the smaller
            //! of the two possible floors. The wider 640 floor for the non-stacked
            //! layout can't be bound declaratively at this level -- like
            //! contextMenuModel below, it depends on videoPanelLoaderInstance's
            //! lazily-instantiated content, so a binding referencing it here
            //! silently never fires (same reasoning as the contextMenuModel
            //! comment below). It's pushed down imperatively instead once that
            //! content is ready and whenever it changes (see
            //! onHitPointsPanelBelowTimelineChanged below). Defaulting to the
            //! *smaller* floor here (rather than 640) matters: applySizeConstraints()
            //! (dockbase.cpp) applies minimumWidth to the underlying dock widget
            //! immediately, and Qt/KDDockWidgets forcibly grows a widget to meet a
            //! minimum that exceeds its current size -- so defaulting to 640 would
            //! immediately blow up a persisted width narrower than that (e.g. one
            //! saved under the stacked layout) the instant this panel is restored
            //! and loaded, well before the real 300 floor gets pushed down.
            minimumWidth: root.verticalPanelDefaultWidth
            maximumWidth: root.panelMaxDimension

            groupName: root.horizontalPanelsGroup
            //! NOTE: at the left or right, it goes with the side panels instead (e.g. below Palettes, or tabbed with them)
            sideGroupName: root.verticalPanelsGroup
            secondaryWindowAllowed: true

            //! NOTE: hidden by default
            visible: false

            location: Location.Bottom

            dropDestinations: root.horizontalOrSidePanelDropDestinations

            navigationSection: root.navigationPanelSec(videoPanel.location)

            VideoPanelLoader {
                id: videoPanelLoaderInstance

                anchors.fill: parent
                navigationSection: videoPanel.navigationSection
                contentNavigationPanelOrderStart: videoPanel.contentNavigationPanelOrderStart
                floating: videoPanel.floating

                // NOTE: this panel's content (including this Loader) is the
                // DockPanel's default-property content, which the framework
                // wraps into a Component and only instantiates once the panel
                // is actually shown (DockPanel.qml: `Loader { active: root.visible
                // && root.inited }`) -- so a plain declarative binding at the
                // DockPanel level (`videoPanel.contextMenuModel: videoPanelLoaderInstance.
                // contextMenuModel`) referenced an id that didn't exist yet and
                // silently never fired. Pushing it up imperatively here, once
                // this content itself finishes loading, is what Mixer/Piano
                // keyboard already do for exactly this reason (see their own
                // Component.onCompleted below in this same file).
                Component.onCompleted: {
                    videoPanel.contextMenuModel = contextMenuModel
                    videoPanel.dockPlacementUpdater = updateDockPlacement
                    Qt.callLater(updateDockPlacement)
                }

                //! NOTE: whether it's docked at the left or right of the score - the framework only knows the declared
                //! location (see DockPageView::actualLocation()), so this compares the geometries the same way - and
                //! whether it's tabbed with the side panels. A move to or from the side, or into or out of their tabs,
                //! always changes the panel's size (or floating state, or window)
                //! NOTE: tabbed with the side panels (same group), it gets their fixed width: the group gets the
                //! constraints of the dock that applied them last, so its maximum width could become the whole
                //! column's. Anywhere else, alone at a side included, it stays resizable (stacked below them in a
                //! group of its own, their column stays at their width anyway)
                function updateDockPlacement() {
                    //! NOTE: first, and every time: no geometry needed (e.g. restored as a tab not shown, with no size),
                    //! and the maximum width outlives this content when the panel is closed - reopened elsewhere,
                    //! it must not keep the tabbed one
                    videoPanel.maximumWidth = root.resizableSidePanelMaximumWidth(videoPanel)
                    updateMinimumWidth()

                    //! NOTE: not known (see root.isDockedAtSide()): it stays where it was - flipping would push the wide
                    //! floor for that moment, and widen a side column for good
                    const atSide = root.isDockedAtSide(videoPanel, videoPanelLoaderInstance)
                    if (atSide === undefined) {
                        return
                    }

                    dockedAtSide = atSide
                    //! NOTE: after dockedAtSide: the sidebar's placement (and so the floor) follows it first
                    placementKnown = true
                }

                //! NOTE: until the placement is known (see updateDockPlacement()), the content lays out as if it weren't
                //! at a side: its wide floor would widen a side column it's in for good (the layout is saved so)
                property bool placementKnown: false
                onPlacementKnownChanged: updateMinimumWidth()

                readonly property var hostWindow: Window.window
                onHostWindowChanged: Qt.callLater(updateDockPlacement)
                onWidthChanged: Qt.callLater(updateDockPlacement)
                onHeightChanged: Qt.callLater(updateDockPlacement)
                onFloatingChanged: Qt.callLater(updateDockPlacement)

                Connections {
                    target: notationView

                    function onWidthChanged() {
                        Qt.callLater(videoPanelLoaderInstance.updateDockPlacement)
                    }

                    function onHeightChanged() {
                        Qt.callLater(videoPanelLoaderInstance.updateDockPlacement)
                    }
                }

                //! NOTE: these are only defaults until contentReady (see VideoPanelLoader.qml), so the first
                //! read of the real values happens on panelReady below instead - pushing a default could
                //! widen (or heighten) the restored layout for good. These handlers cover the values changing
                //! again later, e.g. the user live-toggling the sidebar via the panel's own "..." menu.
                //! NOTE: a hidden sidebar takes no width either, so it gets the narrow floor too
                //! NOTE: the wide floor is the content's own: it follows the panel's zoom. The narrow one is the
                //! side panels' column width - also when the panel takes their fixed width (never above its maximum)
                function minimumWidthFloor() {
                    const wide = needsWideMinimumWidth && placementKnown && videoPanel.maximumWidth > root.verticalPanelDefaultWidth
                    return wide ? Math.ceil(640 * contentZoom) : root.verticalPanelDefaultWidth
                }

                function updateMinimumWidth() {
                    if (contentReady) {
                        videoPanel.minimumWidth = minimumWidthFloor()
                    }
                }

                onNeedsWideMinimumWidthChanged: updateMinimumWidth()
                onContentZoomChanged: updateMinimumWidth()

                //! NOTE: stacked with other panels in a column, it could otherwise be made shorter than its picture
                onContentMinimumHeightChanged: {
                    if (contentReady) {
                        videoPanel.minimumHeight = contentMinimumHeight
                    }
                }

                onPanelReady: {
                    //! NOTE: right away, not later: restored at the side with the sidebar chosen beside the video,
                    //! the wide floor would widen the side panels' column for a moment
                    updateDockPlacement()
                    videoPanel.minimumWidth = minimumWidthFloor()
                    videoPanel.minimumHeight = contentMinimumHeight
                }

                Component.onDestruction: {
                    videoPanel.contextMenuModel = null
                    videoPanel.dockPlacementUpdater = null
                }
            }
        }
    ]

    central: NotationView {
        id: notationView
        name: "MainNotationView"

        isNavigatorVisible: root.pageModel.isNavigatorVisible
        isBraillePanelVisible: root.pageModel.isBraillePanelVisible
        isMainView: true

        Component.onCompleted: {
            root.notationView = notationView.paintView

            root.setDefaultNavigationControl(notationView.defaultNavigationControl)
        }

        Component.onDestruction: {
            root.setDefaultNavigationControl(null)
        }
    }

    statusBar: DockStatusBar {
        objectName: root.pageModel.statusBarName()

        navigationSection: content.navigationSection

        NotationStatusBar {
            id: content
        }
    }

    tours: [
        {
            "eventCode": "online_sounds_added",
            "tour": {
                "id": "online-sounds-first-use",
                "steps": [
                    {
                        "title": qsTrc("playback", "This sound processes online"),
                        "description": qsTrc("playback", "Audio is processed in the background while you work. To trigger processing yourself, turn off automatic processing in Preferences > Audio & MIDI > Online sounds."),
                        "controlUri": "control://NotationStatusBar/NotationStatusBar/OnlineSoundsStatusView",
                        "previewImageOrGifUrl": "qrc:/resources/OnlineSoundsPreview.gif",
                        "videoExplanationUrl": "https://youtu.be/hQ4YqmHM3BE?utm_source=mss-app-yt-4.6-cantai&utm_medium=mss-app-yt-4.6-cantai&utm_campaign=mss-app-yt-4.6-cantai"
                    }
                ]
            }
        },
        {
            "eventCode": "online_sounds_manual_processing_allowed",
            "tour": {
                "id": "online-sounds-manual-process",
                "steps": [
                    {
                        "title": qsTrc("playback", "Online sounds"),
                        "description": qsTrc("playback", "Click to manually process online sounds."),
                        "controlUri": "control://NotationStatusBar/NotationStatusBar/OnlineSoundsStatusView",
                    }
                ]
            }
        },
    ]
}
