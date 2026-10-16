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

//! NOTE: a slot being dragged among a channel's slots (Audio FX, aux sends): the slots it passes make room for it
QtObject {
    property int draggedIndex: -1
    property int dropIndex: -1

    //! NOTE: where a slot is drawn meanwhile: its own index, shifted by one row while the dragged slot passed it,
    //! like the model will be once the move is made
    function displayIndex(index) {
        if (draggedIndex < 0 || index === draggedIndex) {
            return index
        }

        if (draggedIndex < dropIndex) {
            return (index > draggedIndex && index <= dropIndex) ? index - 1 : index
        }

        return (index >= dropIndex && index < draggedIndex) ? index + 1 : index
    }

    function clear() {
        draggedIndex = -1
        dropIndex = -1
    }
}
