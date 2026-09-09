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

#include "auxsenditemlistmodel.h"

#include "auxsenditem.h"

using namespace mu::playback;

AuxSendItemListModel::AuxSendItemListModel(QObject* parent)
    : QAbstractListModel(parent)
{
}

int AuxSendItemListModel::rowCount(const QModelIndex& parent) const
{
    if (parent.isValid()) {
        return 0;
    }

    return m_items.size();
}

QVariant AuxSendItemListModel::data(const QModelIndex& index, int role) const
{
    if (!index.isValid() || index.row() < 0 || index.row() >= m_items.size() || role != AuxSendItemRole) {
        return QVariant();
    }

    return QVariant::fromValue(m_items.at(index.row()));
}

QHash<int, QByteArray> AuxSendItemListModel::roleNames() const
{
    static const QHash<int, QByteArray> roles = {
        { AuxSendItemRole, "auxSendItem" }
    };

    return roles;
}

void AuxSendItemListModel::sync(const QList<AuxSendItem*>& newItems)
{
    if (m_items == newItems) {
        return;
    }

    int oldSize = m_items.size();
    int newSize = newItems.size();

    //! NOTE: find the common leading/trailing run, so only the differing middle range is
    //! actually removed/inserted - the common case (a pure append, or a pure removal from
    //! the end) ends up touching only the rows that truly changed
    int prefix = 0;
    while (prefix < oldSize && prefix < newSize && m_items.at(prefix) == newItems.at(prefix)) {
        ++prefix;
    }

    int oldEnd = oldSize;
    int newEnd = newSize;
    while (oldEnd > prefix && newEnd > prefix && m_items.at(oldEnd - 1) == newItems.at(newEnd - 1)) {
        --oldEnd;
        --newEnd;
    }

    if (oldEnd > prefix) {
        beginRemoveRows(QModelIndex(), prefix, oldEnd - 1);
        m_items.erase(m_items.begin() + prefix, m_items.begin() + oldEnd);
        endRemoveRows();
    }

    if (newEnd > prefix) {
        QList<AuxSendItem*> insertion = newItems.mid(prefix, newEnd - prefix);
        beginInsertRows(QModelIndex(), prefix, prefix + insertion.size() - 1);
        for (int i = 0; i < insertion.size(); ++i) {
            m_items.insert(prefix + i, insertion.at(i));
        }
        endInsertRows();
    }
}
