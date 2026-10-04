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
import QtQuick

import Muse.UiComponents

import MuseScore.NotationScene

StyledToolBarView {
    property alias isCompactMode: toolBarModel.isCompactMode

    navigationPanel.name: "NotationToolBar"
    navigationPanel.accessible.name: qsTrc("notation", "Notation toolbar")

    spacing: 2

    NotationToolBarModel {
        id: toolBarModel
    }

    AutomationTypeMenuModel {
        id: automationTypeMenuModel

        Component.onCompleted: automationTypeMenuModel.init()
    }

    ExpressionMenuModel {
        id: expressionMenuModel

        Component.onCompleted: expressionMenuModel.init()
    }

    model: toolBarModel

    // "toggle-automation" is marked ToolBarItemType.USER_TYPE (see notationtoolbarmodel.cpp) so it
    // stays a normal item in the model's own order (keeping it correctly positioned between
    // "toggle-mixer" and the Expression button - the two were fighting for that spot when
    // automation was instead rendered as a separate sibling appended after this whole view), while
    // still getting its own delegate: a SplitButton whose dropdown arrow picks the automation type
    // directly, instead of a plain toggle-only button.
    sourceComponentCallback: function(type) {
        if (type === ToolBarItemType.USER_TYPE) {
            return automationButtonComponent
        }

        //! NOTE: USER_TYPE + 1, see notationtoolbarmodel.cpp
        if (type === ToolBarItemType.USER_TYPE + 1) {
            return expressionButtonComponent
        }

        return null
    }

    // Groups the note offset, note velocity and articulation editors: the dropdown shows/hides
    // each of them (they can be combined), the button toggles the last one checked there
    Component {
        id: expressionButtonComponent

        SplitButton {
            id: expressionControl

            property var itemData

            icon: expressionMenuModel.currentIcon
            // The articulation glyph is drawn larger than the others by the icon font
            iconFont: expressionMenuModel.currentIconIsLarge
                      ? Qt.font({ family: ui.theme.iconsFont.family, pixelSize: Math.round(ui.theme.iconsFont.pixelSize * 0.8) })
                      : ui.theme.iconsFont
            text: Boolean(itemData) && itemData.showTitle ? expressionMenuModel.currentTitle : ""
            textWidth: Math.ceil(expressionTitleMetrics.advanceWidth)
            checked: Boolean(itemData) && itemData.checked
            enabled: Boolean(itemData) ? itemData.enabled : false

            // Sized for the longest name, so the button keeps its width whatever editor it shows
            TextMetrics {
                id: expressionTitleMetrics
                font: ui.theme.bodyFont
                text: expressionMenuModel.titles.reduce(function(longest, title) {
                    return title.length > longest.length ? title : longest
                }, "")
            }

            toolTipTitle: Boolean(itemData) ? itemData.title + " \u2013 " + expressionMenuModel.currentTitle : ""
            toolTipDescription: Boolean(itemData) ? itemData.description : ""

            menuItems: expressionMenuModel.items

            onClicked: {
                if (Boolean(itemData)) {
                    itemData.activate()
                }
            }

            onHandleMenuItem: function(itemId) {
                expressionMenuModel.handleMenuItem(itemId)
            }

            onAboutToOpenMenu: {
                expressionMenuModel.init()
            }
        }
    }

    Component {
        id: automationButtonComponent

        SplitButton {
            id: control

            property var itemData

            // The current automation type's icon ("=" after the note for a tempo)
            icon: automationTypeMenuModel.currentIcon
            iconSuffix: automationTypeMenuModel.currentIconSuffix
            // Shows which curve is being edited, in the space the "Automation" title would take
            // (so the button never changes size - a longer name is cut off)
            text: Boolean(itemData) && itemData.showTitle ? automationTypeMenuModel.currentTitle : ""
            textWidth: Math.ceil(titleMetrics.advanceWidth)
            checked: Boolean(itemData) && itemData.checked
            enabled: Boolean(itemData) ? itemData.enabled : false

            TextMetrics {
                id: titleMetrics
                font: ui.theme.bodyFont
                text: Boolean(control.itemData) ? control.itemData.title : ""
            }

            toolTipTitle: Boolean(itemData) ? itemData.title + " \u2013 " + automationTypeMenuModel.currentTitle : ""
            toolTipDescription: Boolean(itemData) ? itemData.description : ""

            menuItems: automationTypeMenuModel.items

            onClicked: {
                if (Boolean(itemData)) {
                    itemData.activate()
                }
            }

            onHandleMenuItem: function(itemId) {
                automationTypeMenuModel.handleMenuItem(itemId)
            }

            onAboutToOpenMenu: {
                // Refresh right before showing, rather than relying solely on the model's own
                // reactive updates - matches how the right-click "Automation type" submenu is
                // always rebuilt fresh on open, so it can't show a stale/no checkmark either.
                automationTypeMenuModel.init()
            }
        }
    }
}
