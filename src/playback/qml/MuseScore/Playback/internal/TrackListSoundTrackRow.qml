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

import Muse.Ui
import Muse.UiComponents
import MuseScore.Playback

//! NOTE: a sound track's row of the Track list (the video's, the reference audio's, see SoundTrackChannel): its
//! buttons in the parts' columns (Load in the sound one), there even without its source, to load one
Rectangle {
    id: root

    property MixerChannelItem channelItem: null
    property bool hasSource: false
    property bool categoryVisible: true

    property string title: ""
    property string noSourceTitle: ""
    property string loadTitle: ""

    property bool hasArticulationMapColumn: false
    property bool hasEditorColumn: false

    property NavigationPanel navigationPanel: null
    property string navigationName: ""
    property int navigationRow: 0

    readonly property color trackColor: channelItem && channelItem.hasCustomColor ? channelItem.color : ui.theme.accentColor

    signal colorSelected(color color)
    signal resetColorRequested()
    signal loadRequested()

    height: visible ? 28 : 0
    visible: Boolean(channelItem) && categoryVisible

    color: ui.theme.backgroundPrimaryColor

    ChannelColorStrip {
        id: colorStrip

        anchors.left: parent.left
        anchors.leftMargin: 4
        anchors.verticalCenter: parent.verticalCenter

        trackColor: root.trackColor
        resetEnabled: root.channelItem ? root.channelItem.hasCustomColor : false

        onColorSelected: function(color) {
            root.colorSelected(color)
        }

        onResetRequested: {
            root.resetColorRequested()
        }
    }

    ChannelMeters {
        id: meter

        anchors.right: parent.right
        anchors.rightMargin: 4
        anchors.verticalCenter: parent.verticalCenter

        meterLength: 22
        meterThickness: 3
        overloadLength: 2
        gap: 1

        leftChannelPressure: root.channelItem ? root.channelItem.leftChannelPressure : -60
        rightChannelPressure: root.channelItem ? root.channelItem.rightChannelPressure : -60
    }

    StyledTextLabel {
        anchors.left: colorStrip.right
        anchors.leftMargin: 6
        anchors.right: buttonsRow.left
        anchors.rightMargin: 6
        anchors.verticalCenter: parent.verticalCenter

        horizontalAlignment: Text.AlignLeft
        text: root.hasSource ? root.title : root.noSourceTitle
        opacity: root.hasSource ? 1 : 0.5
    }

    Row {
        id: buttonsRow

        anchors.right: meter.left
        anchors.rightMargin: 6
        anchors.verticalCenter: parent.verticalCenter

        spacing: 4

        FlatButton {
            width: 20
            height: 20

            transparent: true
            icon: IconCode.OPEN_FILE

            navigation.panel: root.navigationPanel
            navigation.name: "Load" + root.navigationName + "Button"
            navigation.row: root.navigationRow
            navigation.column: 0
            navigation.accessible.name: root.loadTitle

            onClicked: {
                root.loadRequested()
            }
        }

        ChannelMuteSoloButtons {
            spacing: 4

            enabled: root.hasSource
            channelItem: root.channelItem

            navigationPanel: root.navigationPanel
            navigationName: root.navigationName + "Row"
            muteNavigationRow: root.navigationRow
            muteNavigationColumn: 1
            soloNavigationRow: root.navigationRow
            soloNavigationColumn: 2
            accessibleName: root.title

            onMuteToggled: function(muted) {
                root.channelItem.muted = muted
            }

            onSoloToggled: function(solo) {
                root.channelItem.solo = solo
            }
        }

        //! NOTE: the parts' articulation map and editor columns
        Item {
            width: 20
            height: 20
            visible: root.hasArticulationMapColumn
        }

        Item {
            width: 20
            height: 20
            visible: root.hasEditorColumn
        }

        //! NOTE: the rows' bus label room (see the rows' own)
        Item {
            width: 20
            height: 20
            visible: !root.hasArticulationMapColumn && !root.hasEditorColumn
        }
    }

    SeparatorLine {
        anchors.bottom: parent.bottom
    }
}
