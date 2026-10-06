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

import Muse.Ui
import Muse.UiComponents
import MuseScore.Playback

//! NOTE: shown while a project opens, until it's really ready (see ProjectLoadingModel): its loading can block the
//! application for a while (e.g. VST3 instruments restoring their state)
StyledDialogView {
    id: root

    property string projectName: ""

    title: root.projectName

    contentWidth: 360
    contentHeight: 72

    closeOnEscape: false

    ProjectLoadingModel {
        id: loadingModel

        onLoadingFinished: {
            root.close()
        }
    }

    Component.onCompleted: {
        loadingModel.load()
    }

    Column {
        anchors.fill: parent
        anchors.margins: 16

        spacing: 8

        StyledTextLabel {
            width: parent.width
            text: qsTrc("playback", "Loading…")
            horizontalAlignment: Text.AlignLeft
        }

        //! NOTE: the plugin being loaded, e.g. "3/9 <plugin name>"
        StyledTextLabel {
            width: parent.width
            text: loadingModel.message
            horizontalAlignment: Text.AlignLeft
            elide: Text.ElideRight
        }
    }
}
