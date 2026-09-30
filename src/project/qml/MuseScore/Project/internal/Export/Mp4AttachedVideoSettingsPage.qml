/*
 * SPDX-License-Identifier: GPL-3.0-only
 * MuseScore-Studio-CLA-applies
 *
 * MuseScore Studio
 * Music Composition & Notation
 *
 * Copyright (C) 2025 MuseScore Limited
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
import MuseScore.Project

ExportSettingsPage {
    id: root

    property bool isAvailable: Boolean(root.model) && root.model.isAttachedVideoExportAvailable

    isExportAvailable: root.isAvailable

    AudioSettings {
        visible: root.isAvailable

        model: root.model
        navigationPanel: root.navigationPanel
        navigationOrderStart: root.navigationOrder
        showBitRateControl: true

        // The video's audio is what this export is about: always included (the Mixer can still mute it)
        showIncludeVideoAudioControl: false
    }

    StyledTextLabel {
        width: parent.width
        text: root.isAvailable
              ? qsTrc("project/export", "The attached video is exported over the score's duration, placed by its offset, with the score's audio and the video's own audio as mixed in the Mixer. Its picture is kept as is, except when it starts after the score: it's then re-encoded, to add black frames before it.")
              : qsTrc("project/export", "The FFmpeg libraries needed to read and write the video could not be found.")
        horizontalAlignment: Text.AlignLeft
        wrapMode: Text.WordWrap
    }

    StyledTextLabel {
        visible: root.isAvailable
        width: parent.width
        text: qsTrc("project/export", "Each selected part will be exported as a separate video file.")
        horizontalAlignment: Text.AlignLeft
        wrapMode: Text.WordWrap
    }
}
