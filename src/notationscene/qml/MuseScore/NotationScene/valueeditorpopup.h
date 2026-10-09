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

#include <functional>

#include <QPointer>
#include <QPointF>
#include <QString>

namespace mu::notation {
struct ValueEditorPopupParams {
    double min = 0.0;
    double max = 1.0;
    double value = 0.0;
    int decimals = 0;
    QString prefix;
    QString suffix;
};

//! NOTE: a small field next to globalPos to type a value (Cmd+click on an automation point or a velocity bar).
//! Return/Enter or a click elsewhere commits the typed value (clamped to [min, max]) once the field was edited -
//! even back to the shown value - Escape (or nothing typed) closes it without calling commit. editor holds the popup while it's shown: an already open one is closed first, without committing
void showValueEditorPopup(QPointer<QObject>& editor, const ValueEditorPopupParams& params, const QPointF& globalPos,
                          std::function<void(double)> commit);

//! NOTE: without committing anything
void closeValueEditorPopup(QPointer<QObject>& editor);
}
