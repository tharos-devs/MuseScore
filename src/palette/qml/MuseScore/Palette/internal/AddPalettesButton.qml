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

import Muse.Ui
import Muse.UiComponents
import MuseScore.Palette

//! NOTE: "Add palettes" and its popups (the palettes to add, then a custom palette's name): the Palettes panel shows
//! it as text, the Secondary palettes panel as an icon
PopupButton {
    id: root

    property PaletteProvider paletteProvider: null

    property int popupAvailableWidth: 0
    property int popupMaxHeight: 400

    signal addCustomPaletteRequested(var paletteName)

    function closePopups() {
        root.close()
        createCustomPalettePopupLoader.close()
    }

    text: qsTrc("palette", "Add palettes")

    popupComponent: AddPalettesPopup {
        paletteProvider: root.paletteProvider
        model: root.paletteProvider ? root.paletteProvider.availableExtraPalettesModel() : null
        popupAvailableWidth: root.popupAvailableWidth
        maxHeight: root.popupMaxHeight

        onAddCustomPaletteRequested: {
            root.close()
            createCustomPalettePopupLoader.toggleOpened()
        }
    }

    StyledPopupLoader {
        id: createCustomPalettePopupLoader

        popupAnchorItem: root.popupAnchorItem

        sourceComponent: CreateCustomPalettePopup {
            popupAvailableWidth: root.popupAvailableWidth

            onAddCustomPaletteRequested: function(paletteName) {
                root.addCustomPaletteRequested(paletteName)
            }
        }
    }
}
