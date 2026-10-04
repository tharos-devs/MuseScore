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

import Muse.Ui
import Muse.UiComponents
import MuseScore.NotationScene

Item {
    id: root

    property alias orientation: gridView.orientation

    property bool floating: false

    property int maximumWidth: 0
    property int maximumHeight: 0

    width: gridView.isHorizontal ? childrenRect.width : 76
    height: !gridView.isHorizontal ? childrenRect.height : 40

    property NavigationPanel navigationPanel: NavigationPanel {
        name: "NoteInputBar"
        enabled: root.enabled && root.visible
        accessible.name: qsTrc("notation", "Note input toolbar")
    }

    NoteInputBarModel {
        id: noteInputModel

        horizontal: root.orientation === Qt.Horizontal
    }

    QtObject {
        id: prv

        function resolveHorizontalGridViewWidth() {
            if (root.floating) {
                return gridView.contentWidth
            }

            var requiredFreeSpace = gridView.cellWidth * 3 + gridView.rowSpacing * 4

            if (root.maximumWidth - gridView.contentWidth < requiredFreeSpace) {
                return gridView.contentWidth - requiredFreeSpace
            }

            return gridView.contentWidth
        }

        function resolveVerticalGridViewHeight() {
            if (root.floating) {
                return gridView.contentHeight
            }

            var requiredFreeSpace = gridView.cellHeight * 3 + gridView.rowSpacing * 4

            if (root.maximumHeight - gridView.contentHeight < requiredFreeSpace) {
                return gridView.contentHeight - requiredFreeSpace
            }

            return gridView.contentHeight
        }
    }

    GridViewSectional {
        id: gridView

        sectionRole: "section"

        rowSpacing: 4
        columnSpacing: 4

        cellWidth: 32
        cellHeight: cellWidth

        sectionWidth: isHorizontal ? 1 : width
        sectionHeight: isHorizontal ? height : 1

        clip: true

        model: noteInputModel

        sectionDelegate: SeparatorLine {
            required property int itemIndex

            orientation: gridView.isHorizontal ? Qt.Vertical : Qt.Horizontal
            visible: itemIndex !== 0
        }

        itemDelegate: FlatButton {
            id: btn

            required property var itemModel

            readonly property MenuItem item: Boolean(itemModel) ? itemModel.item : null
            readonly property bool hasMenu: Boolean(item) && item.subitems.length !== 0
            // A command with an attached dropdown: the click runs the command, the arrow opens the menu
            readonly property bool isSplit: hasMenu && item.id !== ""
            // The caret input button also spans the following (invisible) spacer cell,
            // to show the grid resolution next to its icon
            readonly property bool isSpacer: Boolean(item) && item.id === "caret-grid-spacer"
            readonly property bool isWide: isSplit && gridView.isHorizontal

            visible: !isSpacer

            readonly property int wideWidth: 50
            readonly property int spannedWidth: gridView.cellWidth * 2 + gridView.columnSpacing

            // centered over the two spanned cells
            x: isWide ? (spannedWidth - wideWidth) / 2 : 0
            width: isWide ? wideWidth : gridView.cellWidth
            height: gridView.cellWidth

            enabled: noteInputModel.isInputAllowed

            accentButton: (Boolean(item) && item.checked) || menuLoader.isMenuOpened
            transparent: !accentButton

            icon: Boolean(item) && !isWide ? item.icon : IconCode.NONE
            iconFont: ui.theme.toolbarIconsFont

            contentItem: isWide ? caretContentComponent : null

            Component {
                id: caretContentComponent

                Item {
                    implicitWidth: btn.width
                    implicitHeight: btn.height

                    Row {
                        anchors.verticalCenter: parent.verticalCenter
                        anchors.left: parent.left
                        anchors.leftMargin: 7
                        spacing: 3

                        // the caret
                        Rectangle {
                            id: caretLine

                            anchors.verticalCenter: parent.verticalCenter
                            width: 1
                            height: 22
                            color: ui.theme.fontPrimaryColor
                        }

                        // the rhythmic grid resolution
                        // aligned with the bottom of the caret (the notehead glyph descends below its baseline)
                        StyledIconLabel {
                            anchors.baseline: caretLine.bottom
                            anchors.baselineOffset: -3
                            iconCode: noteInputModel.caretGridIcon
                            font.family: ui.theme.toolbarIconsFont.family
                            font.pixelSize: 10
                        }

                        Rectangle {
                            anchors.bottom: caretLine.bottom
                            anchors.bottomMargin: 1
                            visible: noteInputModel.caretGridDotted
                            width: 2
                            height: 2
                            radius: 1
                            color: ui.theme.fontPrimaryColor
                        }
                    }
                }
            }

            toolTipTitle: Boolean(item) ? item.title : ""
            toolTipDescription: Boolean(item) ? item.description : ""
            toolTipShortcut: Boolean(item) ? item.shortcuts : ""

            navigation.panel: root.navigationPanel
            navigation.name: Boolean(item) ? item.id : ""
            navigation.order: Boolean(itemModel) ? itemModel.order : 0
            isClickOnKeyNavTriggered: false
            navigation.onTriggered: {
                if (btn.hasMenu && !btn.isSplit) {
                    toggleMenuOpened()
                } else {
                    handleMenuItem()
                }
            }

            function toggleMenuOpened() {
                menuLoader.toggleOpened(item.subitems)
            }

            function handleMenuItem() {
                Qt.callLater(noteInputModel.handleMenuItem, item.id)
            }

            onClicked: {
                if (btn.hasMenu && !btn.isSplit) {
                    toggleMenuOpened()
                } else {
                    handleMenuItem()
                }
            }

            mouseArea.onPressAndHold: function(event) {
                if (menuLoader.isMenuOpened || !btn.hasMenu) {
                    event.accepted = false // do not suppress the click event
                    return
                }

                btn.toggleMenuOpened()
            }

            StyledIconLabel {
                anchors.right: parent.right
                anchors.bottom: btn.isWide ? undefined : parent.bottom
                anchors.verticalCenter: btn.isWide ? parent.verticalCenter : undefined
                anchors.rightMargin: btn.isWide ? 3 : 1
                anchors.bottomMargin: 1

                visible: btn.isSplit
                iconCode: IconCode.SMALL_ARROW_DOWN
                font.family: ui.theme.iconsFont.family
                // full size like other dropdown arrows, smaller in the single-cell (vertical toolbar) variant
                font.pixelSize: btn.isWide ? ui.theme.iconsFont.pixelSize : 10
                opacity: arrowMouseArea.containsMouse || menuLoader.isMenuOpened ? 1.0 : 0.7

                MouseArea {
                    id: arrowMouseArea

                    anchors.fill: parent
                    anchors.margins: -3

                    enabled: btn.isSplit && btn.enabled
                    hoverEnabled: true

                    onClicked: btn.toggleMenuOpened()
                }
            }

            StyledMenuLoader {
                id: menuLoader

                onHandleMenuItem: function(itemId) {
                    noteInputModel.handleMenuItem(itemId)
                }
            }
        }
    }

    PopupButton {
        id: customizeButton

        anchors.margins: 4

        width: gridView.cellWidth
        height: gridView.cellHeight

        icon: IconCode.SETTINGS_COG
        iconFont: ui.theme.toolbarIconsFont
        toolTipTitle: qsTrc("notation", "Customize toolbar")
        toolTipDescription: qsTrc("notation", "Show/hide toolbar buttons")
        transparent: true

        enabled: noteInputModel.isInputAllowed

        navigation.panel: root.navigationPanel
        navigation.order: 100
        navigation.accessible.name: qsTrc("notation", "Customize toolbar")

        popupAnchorItem: root.floating ? null : ui.rootItem
        popupComponent: NoteInputBarCustomisePopup {}
    }

    states: [
        State {
            when: gridView.isHorizontal

            PropertyChanges {
                target: gridView
                width: prv.resolveHorizontalGridViewWidth()
                height: root.height
                sectionWidth: 1
                sectionHeight: root.height
                rows: 1
                columns: gridView.noLimit
            }

            AnchorChanges {
                target: customizeButton
                anchors.left: gridView.right
                anchors.verticalCenter: root.verticalCenter
            }
        },
        State {
            when: !gridView.isHorizontal

            PropertyChanges {
                target: gridView
                width: root.width
                height: prv.resolveVerticalGridViewHeight()
                sectionWidth: root.width
                sectionHeight: 1
                rows: gridView.noLimit
                columns: 2
            }

            AnchorChanges {
                target: customizeButton
                anchors.top: gridView.bottom
                anchors.right: parent.right
            }
        }
    ]
}
