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

#include <QAbstractListModel>
#include <QList>

namespace mu::playback {
class AuxSendItem;

//! NOTE: exposes one channel's aux-send slots as a real incremental list model (row-level
//! insert/remove signals via sync()) instead of a plain QList Qt property. A plain-list
//! property forces Qt Quick's Repeater to destroy and recreate EVERY delegate whenever the
//! property is reassigned - even for a change that only appends/retargets one slot - which
//! can misdirect an in-flight click on an already-open dropdown menu for an unrelated,
//! untouched slot (see MixerChannelItem::reassignAuxSend/addAuxSendBlankSlots/blankAuxSend).
//! sync() diffs against the model's current contents and only touches the rows that
//! actually differ, so unaffected delegates (and any menu popup bound to them) survive.
class AuxSendItemListModel : public QAbstractListModel
{
    Q_OBJECT

public:
    explicit AuxSendItemListModel(QObject* parent = nullptr);

    enum Roles {
        AuxSendItemRole = Qt::UserRole + 1
    };

    int rowCount(const QModelIndex& parent = QModelIndex()) const override;
    QVariant data(const QModelIndex& index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    void sync(const QList<AuxSendItem*>& items);

private:
    QList<AuxSendItem*> m_items;
};
}
