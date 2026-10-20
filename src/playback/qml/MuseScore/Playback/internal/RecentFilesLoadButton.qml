/*
 * SPDX-License-Identifier: GPL-3.0-only
 * MuseScore-CLA-applies
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

import Muse.Ui
import Muse.UiComponents

//! NOTE A file's Load button with the list of the recently loaded ones ("Clear list" at its end): the Video
//! window's, the Audio sync window's
RowLayout {
    id: root

    property string path: ""
    property string dialogTitle: ""
    property string filter: ""
    property string recentFilesTitle: ""
    //! NOTE Returns the recent files, most recent first
    property var recentFiles: function() { return [] }

    property NavigationPanel navigationPanel: null
    property int navigationOrderStart: 0

    signal pathChosen(string newPath)
    signal clearRecentFilesRequested()

    spacing: 4

    FilePicker {
        Layout.preferredWidth: 30
        Layout.preferredHeight: 30
        showPathField: false
        path: root.path
        dialogTitle: root.dialogTitle
        filter: root.filter
        buttonType: FlatButton.IconOnly
        navigation: root.navigationPanel
        navigationRowOrderStart: root.navigationOrderStart

        onPathEdited: function(newPath) {
            root.pathChosen(newPath)
        }
    }

    FlatButton {
        Layout.preferredWidth: 22
        Layout.preferredHeight: 22
        Layout.alignment: Qt.AlignVCenter
        icon: IconCode.SMALL_ARROW_DOWN
        buttonType: FlatButton.IconOnly
        transparent: true
        toolTipTitle: root.recentFilesTitle
        navigation.panel: root.navigationPanel
        navigation.order: root.navigationOrderStart + 3

        onClicked: {
            var files = root.recentFiles()
            var menuItems = files.map(function(path) {
                return { id: path, title: path }
            })
            if (files.length > 0) {
                menuItems.push({ id: "", title: "" }) // separator
                menuItems.push({ id: "__clearRecent__", title: qsTrc("playback", "Clear list") })
            }
            recentFilesMenu.items = menuItems
            recentFilesMenu.show(Qt.point(0, height))
        }

        ContextMenuLoader {
            id: recentFilesMenu

            onHandleMenuItem: function(itemId) {
                if (itemId === "__clearRecent__") {
                    root.clearRecentFilesRequested()
                    return
                }

                root.pathChosen(itemId)
            }
        }
    }
}
