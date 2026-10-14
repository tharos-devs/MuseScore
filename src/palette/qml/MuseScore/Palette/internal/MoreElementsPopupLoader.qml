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

import MuseScore.Palette

//! NOTE: a palette's "More" popup, used by the Palettes and the Secondary palettes panels.
//! toggleOpened(model, control, parent): model is the palette's model data, control has its modelIndex, cellSize
//! and drawGrid, parent is the button opening it
Loader {
    id: palettePopup

    property PaletteProvider paletteProvider: null
    property var paletteModel: null
    property PaletteController paletteController: null

    property int maxHeight: 500

    property var control: null
    property var model: null
    property alias isOpened: palettePopup.active

    //! NOTE: the popup closed (it may stay loaded a moment, see onClosed below)
    signal popupClosed()

    active: false

    sourceComponent: MoreElementsPopup {
        id: moreElementsPopup

        property var control: palettePopup.control
        property var model: palettePopup.model

        maxHeight: palettePopup.maxHeight

        // TODO: change settings to "hidden" model?
        cellSize: control.cellSize
        drawGrid: control.drawGrid

        paletteName: model.display
        paletteIsCustom: model.custom
        paletteEditingEnabled: model.editable

        onIsOpenedChanged: {
            // build pool model on first popup appearance
            if (isOpened && !poolPalette) {
                poolPalette = palettePopup.paletteProvider.poolPaletteModel(control.modelIndex);
                poolPaletteRootIndex = palettePopup.paletteProvider.poolPaletteIndex(control.modelIndex, poolPalette);
                poolPaletteController = palettePopup.paletteProvider.poolPaletteController(poolPalette, control.modelIndex);

                customPalette = palettePopup.paletteProvider.customElementsPaletteModel
                customPaletteRootIndex = palettePopup.paletteProvider.customElementsPaletteIndex(control.modelIndex) // TODO: make a property binding? (but that works incorrectly)
                customPaletteController = palettePopup.paletteProvider.customElementsPaletteController
            }
            if (!isOpened) {
                palettePopup.popupClosed()
            }
        }

        property bool needScrollToBottom: false

        onOpened: {
            scrollToPopupBottom();
            needScrollToBottom = false;
            enablePaletteAnimations = true;
        }

        onClosed: {
            enablePaletteAnimations = false;
            // Deactivating the loader while dragging will cause a crash (#25848), so defer the deactivation...
            if (!moreElementsPopup.isDragInProgress) {
                palettePopup.active = false;
            }
        }

        onIsDragInProgressChanged: {
            const deactivationWasDeferred = !moreElementsPopup.isOpened && palettePopup.active
            if (!moreElementsPopup.isDragInProgress && deactivationWasDeferred) {
                // We're now safe to deactivate the loader...
                palettePopup.active = false
            }
        }

        function scrollToPopupBottom() {
            //! FIXME Not worked as should
//                        const popupBottom = implicitHeight + y + control.y + 14; // 14 for DropShadow in StyledPopup: depends on blur radius and vertical offset
//                        paletteTree.ensureYVisible(popupBottom);
        }

        onAddElementsRequested: function(mimeDataList) {
            const parentIndex = control.modelIndex;
            var idx = palettePopup.paletteModel.rowCount(parentIndex);

            for (var i = 0; i < mimeDataList.length; i++) {
                const mimeData = mimeDataList[i];

                if (palettePopup.paletteController.insert(parentIndex, idx, mimeData, Qt.MoveAction)) {
                    idx++;
                }
            }
        }
    }

    function close() {
        if (palettePopup.active) {
            (palettePopup.item as MoreElementsPopup).close();
        }
    }

    function toggleOpened(model, control, parent) {
        if (palettePopup.active) {
            palettePopup.close();
            return;
        }

        palettePopup.parent = parent;
        palettePopup.model = model;
        palettePopup.control = control;

        palettePopup.active = true;
        (palettePopup.item as MoreElementsPopup).setParentItem(parent);
        (palettePopup.item as MoreElementsPopup).toggleOpened();
    }
}
