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
import QtQuick.Layouts

import Muse.Ui
import Muse.UiComponents

Item {
    id: root

    property AbstractAudioResourceItem resourceItemModel

    property color accentColor: ui.theme.accentColor

    readonly property string title: root.resourceItemModel ? root.resourceItemModel.title : ""
    readonly property bool isActive: root.resourceItemModel ? root.resourceItemModel.isActive : false
    readonly property bool isBlank: root.resourceItemModel ? root.resourceItemModel.isBlank : true

    property bool supportsByPassing: !isBlank
    readonly property bool supportsTitle: !isBlank || !showAdditionalButtons
    readonly property bool supportsMenu: true

    readonly property color separatorColor: ui.theme.borderWidth > 0 ? ui.theme.strokeColor : Utils.colorWithAlpha(ui.theme.fontPrimaryColor, 0.3)

    property bool resourcePickingActive: false

    readonly property bool showAdditionalButtons: rootMouseArea.containsMouse || (navigationPanel ? navigationPanel.highlight : false) || resourcePickingActive

    //! NOTE Named separately from the title button's `enabled` below -- when
    //! `reorderable`, the title button must stay input-enabled even in this
    //! case (so a drag can still start from it), but it should still LOOK
    //! disabled and still refuse to open a nonexistent editor on a plain
    //! click, same as when not reorderable.
    readonly property bool nativeEditorUnavailable: showAdditionalButtons
                                                     && !(resourceItemModel ? resourceItemModel.hasNativeEditorSupport : false)

    property NavigationPanel navigationPanel: null
    property int navigationRowStart: 0
    readonly property int navigationRowEnd: navigationRowStart + 2
    property string navigationName: ""
    property string accessibleName: ""

    //! NOTE Opt-in (off by default, so MixerSoundSection's use of this same
    //! control is unaffected): lets a press-and-drag on the title button --
    //! the big, obvious "grab" area covering most of the control -- reorder
    //! the slot instead of opening its native editor. A plain click (no
    //! significant movement) still opens the editor as before; only a real
    //! drag (movement past a small threshold) is reinterpreted, via
    //! reorderPressed/reorderPositionChanged/reorderReleased below, which
    //! report positions in scene (global) coordinates -- this control has no
    //! way to know the stable ancestor its caller wants to measure movement
    //! against, so it leaves that mapping to the caller.
    property bool reorderable: false

    //! NOTE A blank slot has nothing loaded to reorder -- excluded explicitly
    //! here rather than relying on supportsTitle's own hover-driven collapse
    //! (`!isBlank || !showAdditionalButtons`) to incidentally prevent it: that
    //! collapse only happens to block a blank slot's title from ever being
    //! pressable while hovered (hovering is what triggers the collapse, and a
    //! press requires already hovering), which is a side effect of unrelated
    //! logic, not a deliberate invariant this could keep relying on.
    readonly property bool reorderingActive: reorderable && !isBlank

    signal turnedOn()
    signal turnedOff()

    signal titleClicked()

    signal navigateControlIndexChanged(var index)

    signal reorderPressed(real globalX, real globalY)
    signal reorderPositionChanged(real globalX, real globalY)
    signal reorderReleased()

    //! NOTE For a caller to restore keyboard/accessibility focus onto this
    //! specific slot's title after a reorder -- reordering (like any other
    //! outputResourceItemList change) recreates every delegate from scratch,
    //! which otherwise silently drops whatever had navigation focus.
    function requestTitleFocus() {
        if (titleLoader.item) {
            titleLoader.item.navigation.requestActiveByInteraction()
        }
    }

    height: 24
    width: 96

    QtObject {
        id: prv

        // We can't just check the containsMouse property of the mouseAreas of the buttons themselves,
        // because that property will always be false because this MouseArea is (and must be) on top
        readonly property bool isActivityButtonHovered: rootMouseArea.containsMouse && activityLoader.visible && rootMouseArea.mouseX < root.height // activityLoader.visible && activityLoader.contains(rootMouseArea.mapToItem(activityLoader, rootMouseArea.mouseX, rootMouseArea.mouseY))
        readonly property bool isTitleButtonHovered: rootMouseArea.containsMouse && titleLoader.visible && titleLoader.contains(rootMouseArea.mapToItem(titleLoader, rootMouseArea.mouseX, rootMouseArea.mouseY))
        readonly property bool isSelectorButtonHovered: rootMouseArea.containsMouse && selectorLoader.visible && selectorLoader.contains(rootMouseArea.mapToItem(selectorLoader, rootMouseArea.mouseX, rootMouseArea.mouseY))

        property bool dragThresholdExceeded: false
        property point pressGlobalPos
    }

    RowLayout {
        anchors.fill: parent

        spacing: 0

        Loader {
            id: activityLoader

            Layout.fillHeight: true
            Layout.preferredWidth: root.height
            Layout.alignment: Qt.AlignLeft

            visible: root.supportsByPassing && root.showAdditionalButtons
            active: visible

            sourceComponent: FlatButton {
                id: activityButton

                icon: IconCode.BYPASS

                navigation.panel: root.navigationPanel
                navigation.name: root.navigationName + "ActivityButton"
                navigation.row: root.navigationRowStart + 1
                navigation.accessible.name: root.accessibleName + " " + root.title + " " + qsTrc("playback", "Bypass")
                navigation.onActiveChanged: {
                    if (navigation.active) {
                        root.navigateControlIndexChanged({row: navigation.row, column: navigation.column})
                    }
                }

                backgroundItem: RoundedRectangle {
                    id: activityButtonBackground

                    property real backgroundOpacity: ui.theme.buttonOpacityNormal
                    color: Utils.colorWithAlpha(root.isActive ? root.accentColor : ui.theme.buttonColor, backgroundOpacity)

                    topLeftRadius: 3
                    topRightRadius: 0
                    bottomLeftRadius: 3
                    bottomRightRadius: 0

                    NavigationFocusBorder {
                        navigationCtrl: activityButton.navigation
                    }

                    states: [
                        State {
                            name: "PRESSED"
                            when: activityButton.mouseArea.pressed

                            PropertyChanges {
                                target: activityButtonBackground
                                backgroundOpacity: ui.theme.buttonOpacityHit
                            }
                        },

                        State {
                            name: "HOVERED"
                            when: !activityButton.mouseArea.pressed && prv.isActivityButtonHovered

                            PropertyChanges {
                                target: activityButtonBackground
                                backgroundOpacity: ui.theme.buttonOpacityHover
                            }
                        }
                    ]

                    SeparatorLine {
                        anchors.right: parent.right
                        orientation: Qt.Vertical
                        color: root.separatorColor
                    }
                }

                onClicked: {
                    if (root.isActive) {
                        root.turnedOff()
                    } else {
                        root.turnedOn()
                    }
                }
            }
        }

        Loader {
            id: titleLoader

            Layout.fillWidth: true
            Layout.fillHeight: true

            visible: root.supportsTitle
            active: visible

            sourceComponent: FlatButton {
                id: titleButton

                anchors.fill: parent

                // NOTE: stays input-enabled even when nativeEditorUnavailable, as
                // long as reorderingActive -- otherwise a plugin without a native
                // editor would also be undraggable while hovering, which is
                // exactly when a drag has to start. The "looks/acts disabled"
                // part of that case is instead handled by the DISABLED state
                // and the onClicked guard below, keyed on nativeEditorUnavailable
                // directly rather than on this enabled property.
                enabled: root.reorderingActive || !root.nativeEditorUnavailable

                navigation.panel: root.navigationPanel
                navigation.name: root.navigationName + "TitleButton"
                navigation.row: root.navigationRowStart + 2
                navigation.accessible.name: root.accessibleName + " " + root.title
                navigation.onActiveChanged: {
                    if (navigation.active) {
                        root.navigateControlIndexChanged({row: navigation.row, column: navigation.column})
                    }
                }

                backgroundItem: RoundedRectangle {
                    id: titleButtonBackground

                    property real backgroundOpacity: ui.theme.buttonOpacityNormal
                    color: Utils.colorWithAlpha(root.isActive ? root.accentColor : ui.theme.buttonColor, backgroundOpacity)

                    topLeftRadius: activityLoader.visible ? 0 : 3
                    topRightRadius: selectorLoader.visible ? 0 : 3
                    bottomLeftRadius: topLeftRadius
                    bottomRightRadius: topRightRadius

                    NavigationFocusBorder {
                        navigationCtrl: titleButton.navigation
                    }

                    states: [
                        State {
                            name: "PRESSED"
                            when: titleButton.mouseArea.pressed

                            PropertyChanges {
                                target: titleButtonBackground
                                backgroundOpacity: ui.theme.buttonOpacityHit
                            }
                        },

                        State {
                            name: "HOVERED"
                            when: !titleButton.mouseArea.pressed && prv.isTitleButtonHovered

                            PropertyChanges {
                                target: titleButtonBackground
                                backgroundOpacity: ui.theme.buttonOpacityHover
                            }
                        },

                        // NOTE: listed last so it wins over PRESSED/HOVERED above
                        // when both apply -- needed now that reorderable keeps
                        // titleButton input-enabled (so HOVERED/PRESSED can
                        // legitimately become true) even while nativeEditorUnavailable.
                        State {
                            name: "DISABLED"
                            when: !titleButton.enabled || root.nativeEditorUnavailable

                            PropertyChanges {
                                target: titleButton
                                opacity: ui.theme.itemOpacityDisabled
                            }
                        }
                    ]
                }

                contentItem: StyledTextLabel {
                    width: titleButton.width - 8 // 4px padding on each side
                    height: titleButton.height

                    text: root.title
                }

                mouseArea.cursorShape: root.reorderingActive ? Qt.SizeVerCursor : Qt.ArrowCursor

                onClicked: {
                    if (!prv.dragThresholdExceeded && !root.nativeEditorUnavailable) {
                        root.titleClicked()
                    }
                }

                //! NOTE See root.reorderingActive above -- distinguishes a plain
                //! click (opens the native editor, via onClicked above) from a
                //! real press-and-drag (reorders the slot instead), by threshold
                //! on total movement since press. Positions are reported in scene
                //! coordinates (mapToItem(null, ...)) since titleButton itself
                //! is expected to move as a result of the drag it's reporting --
                //! measuring in its own local coordinates would be self-referential.
                Connections {
                    target: titleButton.mouseArea
                    enabled: root.reorderingActive

                    function onPressed(mouse) {
                        prv.dragThresholdExceeded = false
                        prv.pressGlobalPos = titleButton.mouseArea.mapToItem(null, mouse.x, mouse.y)
                    }

                    function onPositionChanged(mouse) {
                        var globalPos = titleButton.mouseArea.mapToItem(null, mouse.x, mouse.y)

                        if (!prv.dragThresholdExceeded) {
                            var dx = globalPos.x - prv.pressGlobalPos.x
                            var dy = globalPos.y - prv.pressGlobalPos.y

                            if (Math.sqrt(dx * dx + dy * dy) < 4) {
                                return
                            }

                            prv.dragThresholdExceeded = true
                            root.reorderPressed(prv.pressGlobalPos.x, prv.pressGlobalPos.y)
                        }

                        root.reorderPositionChanged(globalPos.x, globalPos.y)
                    }

                    function onReleased(mouse) {
                        if (prv.dragThresholdExceeded) {
                            root.reorderReleased()
                        }
                    }
                }
            }
        }

        Loader {
            id: selectorLoader

            Layout.fillWidth: !titleLoader.visible
            Layout.fillHeight: true
            Layout.preferredWidth: root.height
            Layout.alignment: Qt.AlignRight

            visible: root.showAdditionalButtons && root.supportsMenu
            active: visible

            sourceComponent: FlatButton {
                id: menuButton

                navigation.panel: root.navigationPanel
                navigation.name: root.navigationName + "MenuButton"
                navigation.row: root.navigationRowStart + 3
                navigation.accessible.name: root.accessibleName + " " + qsTrc("playback", "Menu")
                navigation.onActiveChanged: {
                    if (navigation.active) {
                        root.navigateControlIndexChanged({row: navigation.row, column: navigation.column})
                    }
                }

                contentItem: Item {
                    width: menuButton.width
                    height: menuButton.height

                    StyledIconLabel {
                        anchors.right: parent.right
                        width: titleLoader.visible ? parent.width : parent.height
                        height: parent.height
                        iconCode: IconCode.SMALL_ARROW_DOWN
                    }
                }

                backgroundItem: RoundedRectangle {
                    id: menuButtonBackground

                    property real backgroundOpacity: ui.theme.buttonOpacityNormal
                    color: Utils.colorWithAlpha(root.isActive ? root.accentColor : ui.theme.buttonColor, backgroundOpacity)

                    topLeftRadius: titleLoader.visible ? 0 : 3
                    bottomLeftRadius: topLeftRadius
                    topRightRadius: 3
                    bottomRightRadius: topRightRadius

                    NavigationFocusBorder {
                        navigationCtrl: menuButton.navigation
                    }

                    states: [
                        State {
                            name: "PRESSED"
                            when: menuButton.mouseArea.pressed || menuLoader.isMenuOpened

                            PropertyChanges {
                                target: menuButtonBackground
                                backgroundOpacity: ui.theme.buttonOpacityHit
                            }
                        },

                        State {
                            name: "HOVERED"
                            when: !menuButton.mouseArea.pressed && prv.isSelectorButtonHovered

                            PropertyChanges {
                                target: menuButtonBackground
                                backgroundOpacity: ui.theme.buttonOpacityHover
                            }
                        }
                    ]

                    SeparatorLine {
                        visible: titleLoader.visible
                        anchors.left: parent.left
                        orientation: Qt.Vertical
                        color: root.separatorColor
                    }
                }

                StyledMenuLoader {
                    id: menuLoader

                    isSearchable: true

                    onHandleMenuItem: function(itemId) {
                        if (root.resourceItemModel) {
                            Qt.callLater(root.resourceItemModel.handleMenuItem, itemId)
                        }
                    }

                    onOpened: {
                        root.resourcePickingActive = true
                    }

                    onClosed: {
                        root.resourcePickingActive = false
                    }
                }

                Connections {
                    target: root.resourceItemModel
                    function onAvailableResourceListResolved(resources) {
                        menuLoader.toggleOpened(resources)
                    }
                }

                onClicked: {
                    if (root.resourceItemModel) {
                        root.resourceItemModel.requestAvailableResources()
                    }
                }
            }
        }
    }

    Rectangle {
        anchors.fill: parent
        color: "transparent"
        border.color: ui.theme.strokeColor
        border.width: root.isBlank ? 1 : ui.theme.borderWidth
        radius: 3
    }

    MouseArea {
        id: rootMouseArea
        anchors.fill: parent

        enabled: parent.enabled
        acceptedButtons: Qt.NoButton
        hoverEnabled: true
    }
}
