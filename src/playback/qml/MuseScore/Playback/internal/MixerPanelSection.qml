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

    //! NOTE: fed in from MixerPanel.qml as flickable.contentX + flickable.width -
    //! channelItemWidth -- the right-edge analogue of headerPinOffsetX above.
    //! Pins the Master channel's own strip so it stays visible at the
    //! Flickable's right edge no matter how far the other channels have been
    //! scrolled, same reasoning and same contentX-cancelling trick.
    property real masterPinOffsetX: 0

    property int channelItemWidth: 108

    //! NOTE: fed in from MixerPanel.qml (contextMenuModel.condensedViewEnabled) -- lets
    //! individual sections (Gain/Balance/Fader so far) trade precision for width when the
    //! user has turned on the Mixer's "Condensed view" option, on top of the narrower
    //! channelItemWidth they already get from MixerPanel.qml in that mode.
    property bool condensedView: false

    //! NOTE: the Master channel's own width, independent from channelItemWidth above --
    //! condensed view narrows regular channels (channelItemWidth) but always keeps Master
    //! at its normal width, so it defaults to matching channelItemWidth here and only
    //! diverges when MixerPanel.qml explicitly feeds a different value in.
    property int masterChannelItemWidth: channelItemWidth

    //! NOTE: centralizes the Master-vs-regular-channel width rule every concrete section
    //! (MixerGainSection.qml etc.) needs for its own per-channel content -- previously
    //! each of the 9 section files re-derived this ternary independently.
    function rowWidthFor(channelItem) {
        return channelItem.type === MixerChannelItem.Master ? root.masterChannelItemWidth : root.channelItemWidth
    }

    //! NOTE: centralizes the "does condensed view apply to THIS row" rule (excluding
    //! Master, which never narrows) -- previously redefined identically in each section
    //! that needed it (Gain/Balance/Fader/AuxSends).
    function isCondensedRow(channelItem) {
        return root.condensedView && channelItem.type !== MixerChannelItem.Master
    }

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

            //! NOTE: wraps root.delegateComponent instead of assigning it directly so the
            //! master channel (always the model's last row -- see
            //! MixerPanelModel::masterChannelIndex()'s documented invariant) can be
            //! excluded here (zero width, no content instantiated) and rendered exactly
            //! once instead, in the pinned masterPinContent below. Without this, master
            //! would exist as two simultaneous live, interactive delegates (this one plus
            //! the pinned one) both bound to the same navigation.row/navigation.panel
            //! coordinates, which the keyboard-navigation system has no defined behavior
            //! for.
            delegate: Item {
                id: rowWrapper

                required property int index
                required property MixerChannelItem channelItem

                //! NOTE: deliberately type-based (channelItem.type), not the
                //! previous index === ListView.view.count - 1 - that positional
                //! check is live/reactive, but it's only ever CONSULTED once, in
                //! Component.onCompleted below, to decide whether to build this
                //! delegate's content at all. During a multi-item drag reorder
                //! (MixerPanelModel::reorderAuxChannels() fires several back-to-back
                //! beginRemoveRows()/beginInsertRows() pairs synchronously, one per
                //! dragged channel), a row sitting right at the boundary being
                //! repeatedly disturbed - e.g. Metronome, immediately after the
                //! dragged type's own section - gets its delegate destroyed and
                //! recreated multiple times in quick succession, shifting between
                //! its real index and index count-1 on every pass. If a recreation
                //! happens to land on a pass where it transiently reads as "last
                //! row", isMaster is true just for that one onCompleted call, its
                //! content is never built, and - since onCompleted never re-runs -
                //! it stays permanently empty even once the index settles back to
                //! its real value afterward: the channel just vanishes. channelItem
                //! (bound to this row's actual model DATA, not its transient
                //! position) can never misidentify a non-Master row this way.
                readonly property bool isMaster: rowWrapper.channelItem
                                                  && rowWrapper.channelItem.type === MixerChannelItem.Master

                width: rowWrapper.isMaster ? 0 : (rowWrapper.delegateItem ? rowWrapper.delegateItem.width : 0)
                height: rowWrapper.delegateItem ? rowWrapper.delegateItem.height : 0
                visible: !rowWrapper.isMaster

                property Item delegateItem: null

                Component.onCompleted: {
                    if (!rowWrapper.isMaster) {
                        rowWrapper.delegateItem = root.delegateComponent.createObject(rowWrapper, { channelItem: rowWrapper.channelItem })
                    }
                }
            }
        }

        //! NOTE: opaque backdrop for the pinned master channel below, same reasoning as
        //! the header's own backdrop above.
        Rectangle {
            visible: root.model && root.model.count > 0
            z: 2

            x: root.masterPinOffsetX
            y: 0

            width: root.masterChannelItemWidth
            height: parent.height

            color: ui.theme.backgroundPrimaryColor
        }

        //! NOTE: the single live instance of the master channel's strip -- excluded from
        //! sectionContentList above and rendered here instead, pinned at the Flickable's
        //! right edge. Rebuilt whenever the underlying MixerChannelItem reference itself
        //! changes (e.g. a new project is loaded and MixerPanelModel::reloadItems()
        //! rebuilds the whole channel list) -- ordinary property changes on the SAME
        //! object propagate live through the instance's own bindings and don't need this.
        Item {
            id: masterPinContent

            visible: root.model && root.model.count > 0
            z: 2

            x: root.masterPinOffsetX
            y: root.spacingAbove

            width: root.masterChannelItemWidth
            height: root.headerHeight

            property MixerChannelItem masterChannelItem: root.model && root.model.count > 0
                                                           ? root.model.get(root.model.count - 1).channelItem : null
            property Item instance: null

            function rebuildInstance() {
                if (masterPinContent.instance) {
                    masterPinContent.instance.destroy()
                    masterPinContent.instance = null
                }
                if (masterPinContent.masterChannelItem) {
                    masterPinContent.instance = root.delegateComponent.createObject(masterPinContent, { channelItem: masterPinContent.masterChannelItem })
                }
            }

            onMasterChannelItemChanged: masterPinContent.rebuildInstance()
            Component.onCompleted: masterPinContent.rebuildInstance()
        }
    }
}
