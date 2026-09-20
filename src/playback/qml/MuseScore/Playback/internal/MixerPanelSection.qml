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
import MuseScore.Playback

Loader {
    id: root

    property string headerTitle: ""
    property bool headerVisible: true
    property int headerWidth: 98
    property int headerHeight: implicitHeight - spacingAbove - spacingBelow

    //! NOTE: lets a section (e.g. MixerMuteAndSoloSection) replace the plain header label
    //! with custom content (e.g. global toggle buttons) while every other section keeps
    //! the default StyledTextLabel below
    property Component headerComponent: null

    //! NOTE: fed in from MixerPanel.qml as the enclosing Flickable's contentX --
    //! binding the header's own x to this exactly cancels the Flickable's
    //! internal -contentX shift (screen position = header.x - contentX =
    //! contentX - contentX = 0), so the header always renders pinned at the
    //! Flickable's own left edge no matter how far the channels have been
    //! scrolled, while still moving normally with vertical scroll since y is
    //! untouched.
    property real headerPinOffsetX: 0

    property int channelItemWidth: 108

    property real spacingAbove: 4
    property real spacingBelow: 4

    property var model: undefined

    property int navigationRowStart: 0

    property bool needReadChannelName: false

    default property Component delegateComponent

    signal navigateControlIndexChanged(var index)

    active: visible

    sourceComponent: Item {
        id: sectionRow

        readonly property int contentStartX: root.headerVisible ? root.headerWidth + 1 : 0

        width: sectionRow.contentStartX + sectionContentList.width
        height: root.spacingAbove + sectionContentList.contentHeight + root.spacingBelow

        //! NOTE: opaque backdrop for the pinned header below -- without it, whatever
        //! channel content has scrolled underneath would show through while the
        //! header stays put over it. Spans the row's FULL height (not just the
        //! header label's own spacingAbove..spacingAbove+headerHeight span) --
        //! otherwise the spacingAbove/spacingBelow margins above and below the
        //! label are left uncovered, letting slivers of scrolled content bleed
        //! through at every row boundary.
        Rectangle {
            visible: root.headerVisible
            z: 2

            x: root.headerPinOffsetX
            y: 0

            width: root.headerWidth
            height: parent.height

            color: ui.theme.backgroundPrimaryColor
        }

        Loader {
            visible: root.headerVisible
            z: 2

            x: root.headerPinOffsetX
            y: root.spacingAbove

            width: root.headerWidth
            height: root.headerHeight

            sourceComponent: root.headerComponent ? root.headerComponent : defaultHeaderLabel
        }

        Component {
            id: defaultHeaderLabel

            StyledTextLabel {
                width: root.headerWidth
                height: root.headerHeight

                leftPadding: 12
                rightPadding: 12

                horizontalAlignment: Qt.AlignRight
                text: root.headerTitle
            }
        }

        ListView {
            id: sectionContentList

            x: sectionRow.contentStartX
            y: root.spacingAbove
            width: contentItem.childrenRect.width
            height: Math.max(1, contentHeight) // HACK: if the height is 0, the listview won't create any delegates
            contentHeight: contentItem.childrenRect.height

            interactive: false
            orientation: Qt.Horizontal
            spacing: 1 // for separators (will be rendered in MixerPanel.qml)

            model: root.model
            delegate: root.delegateComponent
        }
    }
}
