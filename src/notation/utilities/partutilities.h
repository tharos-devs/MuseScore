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

#pragma once

#include <QString>

namespace mu::engraving {
class Part;
}

namespace mu::notation {
class PartUtilities
{
public:
    //! NOTE: the same name as the Layout panel shows (see PartTreeItem): the part name, which includes the
    //! instrument's number and transposition (e.g. "Horn in F 1"), and for the combined part of "Enable stave
    //! sharing" the parts it combines ("Horn in F 1-2", see SharedPart::partName()), as plain text. Falls back
    //! to the instrument's long name, then its name.
    static QString displayName(const engraving::Part* part);
};
}
