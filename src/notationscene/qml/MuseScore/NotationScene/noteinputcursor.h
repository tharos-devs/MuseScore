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
#ifndef MU_NOTATION_NOTEINPUTCURSOR_H
#define MU_NOTATION_NOTEINPUTCURSOR_H

#include <QColor>

#include "modularity/ioc.h"
#include "project/iprojectsession.h"
#include "notation/inotationconfiguration.h"

namespace mu::notation {
class NoteInputCursor : public muse::Contextable
{
    muse::GlobalInject<INotationConfiguration> configuration;
    muse::ContextInject<project::IProjectSession> projectSession = { this };

public:
    NoteInputCursor(const muse::modularity::ContextPtr& iocCtx, bool isThinLine = false);

    void paint(muse::draw::Painter* painter);
    //! Fill and left line of the cursor, for any rect (e.g. the hover on tab staves)
    void paintRect(muse::draw::Painter* painter, const muse::RectF& rect);

private:
    INotationNoteInputPtr currentNoteInput() const;

    bool m_isThinLine = false;
};
}

#endif // MU_NOTATION_NOTEINPUTCURSOR_H
