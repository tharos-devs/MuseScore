/*
 * SPDX-License-Identifier: GPL-3.0-only
 * MuseScore-Studio-CLA-applies
 *
 * MuseScore Studio
 * Music Composition & Notation
 *
 * Copyright (C) 2022 MuseScore Limited and others
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

import Muse.Ui
import Muse.UiComponents

import MuseScore.PropertiesPanel
import MuseScore.NotationScene
import MuseScore.Playback

Item {
    id: container

    property AbstractElementPopup popup: loader.item as AbstractElementPopup
    property bool isPopupOpened: Boolean(popup) && popup.isOpened

    property NavigationSection notationViewNavigationSection: null
    property int navigationOrderStart: 0
    property int navigationOrderEnd: Boolean(loader.item)
                                        ? loader.item.navigationOrderEnd
                                        : navigationOrderStart

    signal opened(var popupType)
    signal closed()

    QtObject {
        id: prv

        //! NOTE: the union of every itemRect seen by the currently open popup instance,
        //! reset in loadPopup() each time a new popup is opened. The popup is anchored to
        //! this instead of the live itemRect directly so that swapping to a SMALLER glyph
        //! (e.g. Accent -> Staccato) never pulls it in closer to the note than it already
        //! was: since the popup is wider than a single note, moving closer to the staff can
        //! slide it over a neighboring note's own mark, blocking the next click when working
        //! through a run of several notes sharing an articulation. Growing (a bigger glyph)
        //! still repositions as needed, since anchoring to a smaller rect would let the new,
        //! bigger glyph render half-hidden underneath the popup.
        property rect trackedElementRect: Qt.rect(0, 0, 0, 0)

        function unitedRect(a, b) {
            if (a.width <= 0 && a.height <= 0) {
                return b
            }

            //! NOTE: a degenerate incoming rect (e.g. the element's canvasBoundingRect()
            //! is momentarily empty - mid-removal, not yet laid out) has no real area to
            //! union in - unioning it anyway would drag the tracked rect's corner down to
            //! b's (0-sized) position, which can be far from where the real content is.
            //! Keep the current tracked rect untouched instead of corrupting it.
            if (b.width <= 0 && b.height <= 0) {
                return a
            }

            const left = Math.min(a.x, b.x)
            const top = Math.min(a.y, b.y)
            const right = Math.max(a.x + a.width, b.x + b.width)
            const bottom = Math.max(a.y + a.height, b.y + b.height)

            return Qt.rect(left, top, right - left, bottom - top)
        }

        function componentByType(type) {
            switch (type) {
            case AbstractElementPopupModel.TYPE_HARP_DIAGRAM: return harpPedalComp
            case AbstractElementPopupModel.TYPE_CAPO: return capoComp
            case AbstractElementPopupModel.TYPE_STRING_TUNINGS: return stringTuningsComp
            case AbstractElementPopupModel.TYPE_SOUND_FLAG: return soundFlagComp
            case AbstractElementPopupModel.TYPE_STAFF_VISIBILITY: return staffVisibilityComp
            case AbstractElementPopupModel.TYPE_DYNAMIC: return dynamicComp
            case AbstractElementPopupModel.TYPE_TEXT: return textStyleComp
            case AbstractElementPopupModel.TYPE_PARTIAL_TIE: return partialTieComp
            case AbstractElementPopupModel.TYPE_SHADOW_NOTE: return shadowNoteComp
            case AbstractElementPopupModel.TYPE_ARTICULATION: return articulationComp
            }

            return null
        }

        function updateContainerPosition() {
            if (!Boolean(container.popup)) {
                return
            }

            prv.trackedElementRect = prv.unitedRect(prv.trackedElementRect, container.popup.elementRect)
            const elementRect = prv.trackedElementRect

            container.x = elementRect.x
            container.y = elementRect.y
            container.height = elementRect.height
            container.width = elementRect.width

            container.popup.updatePosition()
        }
    }

    function show(popupType) {
        close()

        var popup = loader.loadPopup(popupType)
        popup.open()
    }

    function close() {
        if (Boolean(container.popup) && container.popup.isOpened) {
            container.popup.close()
        }
    }

    Loader {
        id: loader

        anchors.fill: parent
        active: false

        function loadPopup(popupType) {
            loader.sourceComponent = prv.componentByType(popupType)
            loader.active = true

            const popup = loader.item as AbstractElementPopup
            console.assert(popup)

            popup.parent = container

            popup.opened.connect(function() {
                container.opened(popupType)
            })

            popup.closed.connect(function() {
                loader.unloadPopup()
                container.closed()
            })

            prv.trackedElementRect = Qt.rect(0, 0, 0, 0)
            prv.updateContainerPosition()
            popup.elementRectChanged.connect(prv.updateContainerPosition)

            //! NOTE: All navigation panels in popups must be in the notation view section.
            //        This is necessary so that popups do not activate navigation in the new section,
            //        but at the same time, when clicking on the component (text input), the focus in popup's window should be activated
            popup.navigationSection = null
            popup.openPolicies = PopupView.NoActivateFocus

            popup.notationViewNavigationSection = container.notationViewNavigationSection
            popup.navigationOrderStart = container.navigationOrderStart

            return popup
        }

        function unloadPopup() {
            loader.active = false
            loader.sourceComponent = null
        }
    }

    Component {
        id: harpPedalComp
        HarpPedalPopup {
        }
    }

    Component {
        id: capoComp
        CapoPopup {
        }
    }

    Component {
        id: stringTuningsComp
        StringTuningsPopup {
        }
    }

    Component {
        id: soundFlagComp
        SoundFlagPopup {
        }
    }

    Component {
        id: staffVisibilityComp
        StaffVisibilityPopup {
        }
    }

    Component {
        id: dynamicComp
        DynamicPopup {
        }
    }

    Component {
        id: textStyleComp
        TextStylePopup {
        }
    }

    Component {
        id: partialTieComp
        PartialTiePopup {
        }
    }

    Component {
        id: shadowNoteComp
        ShadowNotePopup {
        }
    }

    Component {
        id: articulationComp
        ArticulationPopup {
        }
    }
}
