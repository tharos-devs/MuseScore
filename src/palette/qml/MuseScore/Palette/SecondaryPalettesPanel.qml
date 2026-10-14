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

pragma ComponentBehavior: Bound

import QtQuick
import QtQml.Models

import Muse.Ui
import Muse.UiComponents
import MuseScore.Palette

import "internal"

//! NOTE: the palettes of the Palettes panel (same model: what's added, hidden or customized there shows here too) in
//! two narrow columns: an icon per palette, then the cells of the clicked one
Item {
    id: root

    property NavigationSection navigationSection: null
    property int navigationOrderStart: 1

    property alias contextMenuModel: panelModel

    readonly property PaletteProvider paletteProvider: paletteRootModel.paletteProvider
    readonly property var paletteModel: paletteProvider ? paletteProvider.mainPaletteModel : null
    readonly property PaletteController paletteController: paletteProvider ? paletteProvider.mainPaletteController : null

    readonly property int iconColumnWidth: 40
    //! NOTE: the widest palette's cells: one per row, narrower ones several per row (as many as fit)
    property int cellsColumnWidth: 56
    readonly property bool cellsColumnVisible: !panelModel.collapsed && currentPaletteRow >= 0

    //! NOTE: the panel's width (zoomed): the dock panel takes it as its fixed width
    implicitWidth: Math.ceil((iconColumnWidth + (cellsColumnVisible ? 1 + cellsColumnWidth : 0)) * panelModel.zoom)

    //! NOTE: the palette shown in the second column: the one named in panelModel, or the first one
    property int currentPaletteRow: -1
    property var currentPaletteIndex: null
    //! NOTE: its model data, null for none: a new value rebuilds the second column's grid (see below)
    property var currentPaletteData: null

    SecondaryPalettesPanelModel {
        id: panelModel

        onCurrentPaletteNameChanged: root.updateCurrentPalette()
    }

    PaletteRootModel {
        id: paletteRootModel
    }

    Component.onCompleted: {
        panelModel.load()
        updateCurrentPalette()
    }

    onPaletteModelChanged: updateCurrentPalette()

    function updateCurrentPalette() {
        const model = root.paletteModel
        let row = -1

        if (model) {
            const count = model.rowCount()
            for (let i = 0; i < count; ++i) {
                if (model.data(model.index(i, 0), Qt.DisplayRole) === panelModel.currentPaletteName) {
                    row = i
                    break
                }
            }

            if (row < 0 && count > 0) {
                row = 0
            }
        }

        //! NOTE: the "More" popup belongs to the palette shown
        if (row !== root.currentPaletteRow) {
            moreElementsPopup.close()
        }

        root.currentPaletteRow = row
        root.currentPaletteIndex = row >= 0 ? model.index(row, 0) : null
        updateCurrentPaletteData()
    }

    function updateCellsColumnWidth() {
        const model = root.paletteModel
        let width = 56

        if (model) {
            const count = model.rowCount()
            for (let i = 0; i < count; ++i) {
                const gridSize = model.data(model.index(i, 0), PaletteTreeModel.GridSizeRole)
                width = Math.max(width, gridSize ? gridSize.width : 0)
            }
        }

        root.cellsColumnWidth = width
    }

    function updateCurrentPaletteData() {
        const model = root.paletteModel
        const index = root.currentPaletteIndex

        root.updateCellsColumnWidth()

        if (!model || !index) {
            moreElementsPopup.close()
            root.currentPaletteData = null
            return
        }

        const data = {
            display: model.data(index, Qt.DisplayRole),
            custom: model.data(index, PaletteTreeModel.CustomRole),
            editable: model.data(index, PaletteTreeModel.EditableRole),
            cellSize: model.data(index, PaletteTreeModel.GridSizeRole),
            drawGrid: model.data(index, PaletteTreeModel.DrawGridRole),
            modelIndex: index
        }

        //! NOTE: unchanged (e.g. a palette expanded in the Palettes panel): keep the grid as it is
        const old = root.currentPaletteData
        if (old && old.display === data.display && old.custom === data.custom && old.editable === data.editable
                && old.cellSize.width === data.cellSize.width && old.cellSize.height === data.cellSize.height
                && old.drawGrid === data.drawGrid && old.modelIndex.row === data.modelIndex.row) {
            return
        }

        //! NOTE: the "More" popup's button belongs to the grid about to be rebuilt
        moreElementsPopup.close()
        root.currentPaletteData = data
    }

    //! NOTE: a click on a palette's icon: shows it, or folds the second column when it's the one shown
    function selectPalette(row) {
        if (row === root.currentPaletteRow && !panelModel.collapsed) {
            panelModel.collapsed = true
            return
        }

        root.showPalette(row)
    }

    function showPalette(row) {
        panelModel.collapsed = false
        panelModel.currentPaletteName = root.paletteModel.data(root.paletteModel.index(row, 0), Qt.DisplayRole)
        root.updateCurrentPalette()
    }

    function insertCustomPalette(paletteName) {
        if (root.paletteController.insertNewItem(paletteIconsDelegateModel.rootIndex, 0, paletteName)) {
            root.showPalette(0)
        }
    }

    //! NOTE: changes when some palette's cells change: the first column's icons showing a palette's first cell
    //! re-read it (revision: only for their bindings to depend on it)
    property int cellsRevision: 0

    function firstCellIcon(paletteIndex, revision) {
        if (!root.paletteModel || root.paletteModel.rowCount(paletteIndex) === 0) {
            return null
        }

        return root.paletteModel.data(root.paletteModel.index(0, 0, paletteIndex), Qt.DecorationRole)
    }

    Connections {
        target: root.paletteModel

        //! NOTE: only the palettes (not their cells) move the current one
        function onRowsInserted(parent, first, last) {
            if (parent.valid) {
                root.cellsRevision++
                return
            }

            //! NOTE: a palette added from "Add palettes" gets shown
            if (addPalettesButton.isPopupOpened && first === last) {
                root.showPalette(first)
                return
            }

            Qt.callLater(root.updateCurrentPalette)
        }

        function onRowsRemoved(parent) {
            if (parent.valid) {
                root.cellsRevision++
            } else {
                Qt.callLater(root.updateCurrentPalette)
            }
        }

        function onRowsMoved(parent) {
            if (parent.valid) {
                root.cellsRevision++
            } else {
                Qt.callLater(root.updateCurrentPalette)
            }
        }

        function onModelReset() {
            root.cellsRevision++
            Qt.callLater(root.updateCurrentPalette)
        }

        function onLayoutChanged() {
            root.cellsRevision++
            Qt.callLater(root.updateCurrentPalette)
        }

        function onDataChanged(topLeft, bottomRight) {
            if (topLeft.parent.valid) {
                root.cellsRevision++
                return
            }

            //! NOTE: the current palette renamed: keep it
            if (root.currentPaletteRow >= topLeft.row && root.currentPaletteRow <= bottomRight.row) {
                const name = root.paletteModel.data(root.currentPaletteIndex, Qt.DisplayRole)
                if (root.currentPaletteData && name !== root.currentPaletteData.display
                    && Boolean(panelModel.currentPaletteName)) {
                    panelModel.currentPaletteName = name
                }
            }

            Qt.callLater(root.updateCurrentPaletteData)
        }
    }

    onCellsColumnVisibleChanged: {
        if (!cellsColumnVisible) {
            moreElementsPopup.close()
        }
    }

    NavigationPanel {
        id: iconsNavPanel
        name: "SecondaryPalettesIcons"
        section: root.navigationSection
        order: root.navigationOrderStart
        direction: NavigationPanel.Vertical
        enabled: root.enabled && root.visible
    }

    NavigationPanel {
        id: cellsNavPanel
        name: "SecondaryPalettesCells"
        section: root.navigationSection
        order: root.navigationOrderStart + 1
        direction: NavigationPanel.Vertical
        enabled: root.enabled && root.visible && root.cellsColumnVisible
    }

    PaletteSelectionModel {
        id: paletteSelectionModel
        model: root.paletteModel
    }

    MoreElementsPopupLoader {
        id: moreElementsPopup

        paletteProvider: root.paletteProvider
        paletteModel: root.paletteModel
        paletteController: root.paletteController

        maxHeight: Math.min(0.75 * root.height, 500)
    }

    ZoomContainer {
        anchors.fill: parent

        zoom: panelModel.zoom

        Row {
            anchors.fill: parent

            Item {
                id: iconColumn

                width: root.iconColumnWidth
                height: parent.height

                AddPalettesButton {
                    id: addPalettesButton
                    objectName: "SecondaryAddPalettesBtn"

                    anchors.top: parent.top
                    anchors.topMargin: 4
                    anchors.horizontalCenter: parent.horizontalCenter
                    width: 32
                    height: 32

                    text: ""
                    icon: IconCode.PLUS
                    transparent: !isPopupOpened
                    toolTipTitle: qsTrc("palette", "Add palettes")

                    navigation.panel: iconsNavPanel
                    navigation.row: 0

                    paletteProvider: root.paletteProvider
                    popupAnchorItem: root
                    popupAvailableWidth: 300
                    popupMaxHeight: root.height

                    onAddCustomPaletteRequested: function(paletteName) {
                        root.insertCustomPalette(paletteName)
                    }
                }

                StyledListView {
                    id: paletteIcons

                    anchors.top: addPalettesButton.bottom
                    anchors.topMargin: 4
                    anchors.bottom: parent.bottom
                    anchors.left: parent.left
                    anchors.right: parent.right

                    spacing: 2

                    //! NOTE: reordering the palettes by dragging an icon (like the Palettes panel, when allowed there):
                    //! the dragged row, and the row it would be inserted before (count: at the end)
                    property int dragRow: -1
                    property int dropRow: -1
                    property real dragY: 0 // in the list's coordinates
                    readonly property real rowPitch: 32 + spacing
                    readonly property int autoScrollMargin: 24

                    function updateDrag(yInList) {
                        dragY = yInList
                        const yInContent = yInList + contentY - originY
                        dropRow = Math.max(0, Math.min(count, Math.round(yInContent / rowPitch)))
                    }

                    function finishDrag() {
                        const from = dragRow
                        const to = dropRow > dragRow ? dropRow - 1 : dropRow

                        dragRow = -1
                        dropRow = -1

                        if (from >= 0 && to >= 0 && to !== from) {
                            root.paletteController.move(paletteIconsDelegateModel.rootIndex, from,
                                                        paletteIconsDelegateModel.rootIndex, to)
                        }
                    }

                    Timer {
                        interval: 30
                        repeat: true
                        running: paletteIcons.dragRow >= 0
                                 && (paletteIcons.dragY < paletteIcons.autoScrollMargin
                                     || paletteIcons.dragY > paletteIcons.height - paletteIcons.autoScrollMargin)

                        onTriggered: {
                            const step = paletteIcons.dragY < paletteIcons.autoScrollMargin ? -8 : 8
                            const maxY = Math.max(paletteIcons.originY,
                                                  paletteIcons.originY + paletteIcons.contentHeight - paletteIcons.height)
                            paletteIcons.contentY = Math.max(paletteIcons.originY, Math.min(maxY, paletteIcons.contentY + step))
                            paletteIcons.updateDrag(paletteIcons.dragY)
                        }
                    }

                    Rectangle {
                        parent: paletteIcons.contentItem
                        z: 1

                        visible: paletteIcons.dragRow >= 0
                        x: 2
                        width: paletteIcons.width - 4
                        height: 2
                        y: paletteIcons.originY + paletteIcons.dropRow * paletteIcons.rowPitch - paletteIcons.spacing / 2 - height / 2
                        radius: 1
                        color: ui.theme.accentColor
                    }

                    model: DelegateModel {
                        id: paletteIconsDelegateModel
                        model: root.paletteModel

                        //! NOTE: the list sets its items' x: the button centered in a full width item
                        delegate: Item {
                            id: paletteIconItem

                            required property var model
                            required property int index

                            width: paletteIcons.width
                            height: paletteButton.height

                            FlatButton {
                                id: paletteButton

                                readonly property var modelIndex: paletteIconsDelegateModel.modelIndex(paletteIconItem.index)
                                readonly property bool isCurrent: paletteIconItem.index === root.currentPaletteRow && root.cellsColumnVisible
                                readonly property int iconCode: panelModel.paletteIconCode(modelIndex)
                                readonly property string iconText: panelModel.paletteIconText(modelIndex)

                                anchors.horizontalCenter: parent.horizontalCenter
                                width: 32
                                height: 32

                                opacity: paletteIcons.dragRow === paletteIconItem.index ? 0.4 : 1

                                property real pressY: 0
                                property bool dragged: false

                                //! NOTE: or the list takes a vertical drag to scroll
                                mouseArea.preventStealing: root.paletteProvider.isPaletteDragEnabled

                                mouseArea.onPressed: function(mouse) {
                                    pressY = mouse.y
                                    dragged = false
                                }

                                mouseArea.onPositionChanged: function(mouse) {
                                    if (!mouseArea.pressed || !root.paletteProvider.isPaletteDragEnabled) {
                                        return
                                    }

                                    if (!dragged && Math.abs(mouse.y - pressY) < 6) {
                                        return
                                    }

                                    dragged = true
                                    paletteIcons.dragRow = paletteIconItem.index
                                    paletteIcons.updateDrag(mapToItem(paletteIcons, mouse.x, mouse.y).y)
                                }

                                mouseArea.onReleased: {
                                    if (dragged) {
                                        paletteIcons.finishDrag()
                                    }
                                }

                                mouseArea.onCanceled: {
                                    dragged = false
                                    paletteIcons.dragRow = -1
                                    paletteIcons.dropRow = -1
                                }

                                icon: iconCode
                                iconFont: ui.theme.toolbarIconsFont
                                transparent: !isCurrent
                                accentButton: isCurrent

                                toolTipTitle: paletteIconItem.model.display

                                navigation.panel: iconsNavPanel
                                navigation.row: paletteIconItem.index + 1
                                navigation.accessible.name: paletteIconItem.model.accessibleText

                                //! NOTE: no fitting icon: a word, or the palette's first cell
                                contentItem: Boolean(iconText) ? iconTextComponent
                                                               : iconCode === IconCode.NONE ? firstCellComponent : null

                                Component {
                                    id: iconTextComponent

                                    StyledTextLabel {
                                        text: paletteButton.iconText
                                        font.family: ui.theme.bodyBoldFont.family
                                        font.pixelSize: 9
                                        font.bold: true
                                        color: ui.theme.fontPrimaryColor
                                    }
                                }

                                Component {
                                    id: firstCellComponent

                                    Item {
                                        implicitWidth: 28
                                        implicitHeight: 28

                                        readonly property var firstCell: root.firstCellIcon(paletteButton.modelIndex, root.cellsRevision)
                                        readonly property bool hasCells: Boolean(firstCell)

                                        IconView {
                                            anchors.fill: parent
                                            visible: parent.hasCells
                                            icon: parent.firstCell
                                        }

                                        StyledTextLabel {
                                            anchors.fill: parent
                                            visible: !parent.hasCells
                                            text: paletteIconItem.model.display.charAt(0)
                                            font: ui.theme.largeBodyFont
                                        }
                                    }
                                }

                                onClicked: {
                                    //! NOTE: not at the end of a drag
                                    if (dragged) {
                                        dragged = false
                                        return
                                    }

                                    root.selectPalette(paletteIconItem.index)
                                }

                                MouseArea {
                                    anchors.fill: parent
                                    acceptedButtons: Qt.RightButton

                                    onClicked: function(mouse) {
                                        contextMenuLoader.show(Qt.point(mouse.x, mouse.y))
                                    }

                                    ContextMenuLoader {
                                        id: contextMenuLoader

                                        items: [
                                            { id: "hide", title: paletteIconItem.model.custom ? qsTrc("palette", "Hide/Delete palette")
                                                                                              : qsTrc("palette", "Hide palette") },
                                            { id: "reset", title: qsTrc("palette", "Reset palette") }
                                        ]

                                        onHandleMenuItem: function(itemId) {
                                            //! NOTE: later: hiding it destroys this menu
                                            switch (itemId) {
                                            case "hide": Qt.callLater(root.paletteController.remove, paletteButton.modelIndex); break
                                            case "reset": root.paletteProvider.resetPalette(paletteButton.modelIndex); break
                                            }
                                        }
                                    }
                                }
                            }
                        }
                    }
                }
            }

            SeparatorLine {
                orientation: Qt.Vertical
                visible: root.cellsColumnVisible
            }

            Item {
                id: cellsColumn

                width: root.cellsColumnWidth
                height: parent.height
                visible: root.cellsColumnVisible

                //! NOTE: a new grid for each palette shown: changing a grid's palette may leave some of the previous
                //! palette's cells drawn over the new ones
                Repeater {
                    model: root.currentPaletteData ? [ root.currentPaletteData ] : []

                    delegate: PaletteGridView {
                        required property var modelData

                        width: cellsColumn.width
                        height: Math.min(implicitHeight, cellsColumn.height)

                        narrow: true
                        clip: true

                        navigationPanel: cellsNavPanel
                        navigationRow: 0

                        cellSize: modelData.cellSize
                        drawGrid: modelData.drawGrid

                        paletteModel: root.paletteModel
                        paletteRootIndex: modelData.modelIndex
                        paletteController: root.paletteController
                        selectionModel: paletteSelectionModel

                        showMoreButton: true
                        onMoreButtonClicked: function(btn) {
                            moreElementsPopup.toggleOpened(modelData, modelData, btn)
                        }
                    }
                }
            }
        }
    }
}
