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

StyledDialogView {
    id: root

    title: qsTrc("playback", "Save as track preset")

    contentWidth: 440
    contentHeight: contentColumn.implicitHeight + 32

    property string partId: ""
    property string instrumentId: ""

    Component.onCompleted: {
        saveModel.load(partId, instrumentId)
    }

    onOpened: {
        nameField.ensureActiveFocus()
    }

    SaveTrackPresetModel {
        id: saveModel

        onSaved: root.hide()
    }

    //! NOTE: one panel per field: Tab goes from one panel to the next
    NavigationPanel {
        id: nameNavigationPanel
        name: "SaveTrackPresetName"
        section: root.navigationSection
        order: 1
    }

    NavigationPanel {
        id: tagsNavigationPanel
        name: "SaveTrackPresetTags"
        section: root.navigationSection
        order: 2
    }

    ColumnLayout {
        id: contentColumn

        anchors.fill: parent
        anchors.margins: 16
        spacing: 12

        //! NOTE: the sound's plugin (or MuseSounds), not edited
        StyledTextLabel {
            text: qsTrc("playback", "Plugin")
        }

        TextInputField {
            Layout.fillWidth: true
            enabled: false
            currentText: saveModel.pluginName
        }

        StyledTextLabel {
            text: qsTrc("playback", "Name")
        }

        TextInputField {
            id: nameField

            Layout.fillWidth: true
            navigation.panel: nameNavigationPanel
            navigation.order: 0
            currentText: saveModel.name
            onTextChanged: function(newTextValue) {
                saveModel.name = newTextValue
            }
            onAccepted: saveModel.save()
        }

        StyledTextLabel {
            text: qsTrc("playback", "Tags")
        }

        TextInputField {
            Layout.fillWidth: true
            navigation.panel: tagsNavigationPanel
            navigation.order: 0
            currentText: saveModel.tagsText
            hint: qsTrc("playback", "Keywords separated by commas")
            onTextChanged: function(newTextValue) {
                saveModel.tagsText = newTextValue
            }
            onAccepted: saveModel.save()
        }

        //! NOTE: tags of their own, from the instrument and the sound
        StyledTextLabel {
            Layout.fillWidth: true
            horizontalAlignment: Text.AlignLeft
            wrapMode: Text.WordWrap
            opacity: 0.7
            text: qsTrc("playback", "Also tagged: %1").arg(saveModel.automaticTagsText)
        }

        ButtonBox {
            Layout.fillWidth: true
            Layout.topMargin: 8

            buttons: [ ButtonBoxModel.Cancel ]

            navigationPanel.section: root.navigationSection
            navigationPanel.order: 3

            FlatButton {
                text: qsTrc("global", "Save")
                buttonRole: ButtonBoxModel.AcceptRole
                buttonId: ButtonBoxModel.Save
                accentButton: true
                enabled: saveModel.canSave
                onClicked: saveModel.save()
            }

            onStandardButtonClicked: function(buttonId) {
                if (buttonId === ButtonBoxModel.Cancel) {
                    root.reject()
                }
            }
        }
    }
}
