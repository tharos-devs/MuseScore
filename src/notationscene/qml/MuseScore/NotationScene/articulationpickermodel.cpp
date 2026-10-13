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

#include "articulationpickermodel.h"

#include <optional>

#include <QSet>
#include <QVariantMap>

using namespace mu::notation;

ArticulationPickerModel::ArticulationPickerModel(QObject* parent)
    : QObject(parent)
{
}

bool ArticulationPickerModel::isOpen() const
{
    return m_isOpen;
}

QPointF ArticulationPickerModel::position() const
{
    return m_position;
}

QString ArticulationPickerModel::lastPlacedTitle() const
{
    return m_choices.lastPlacedEntryId;
}

bool ArticulationPickerModel::hasInstrumentEditor() const
{
    return bool(m_choices.openInstrumentEditor);
}

QVariantList ArticulationPickerModel::actions() const
{
    QVariantList result;
    for (const ArticulationChoiceAction& action : m_choices.actions) {
        QVariantMap item;
        item["title"] = action.title;
        item["checkable"] = action.checkable;
        item["checked"] = action.checked;
        item["enabled"] = action.enabled;
        result << item;
    }
    return result;
}

QStringList ArticulationPickerModel::pathOf(const QString& entryId)
{
    QStringList path;
    for (const QString& name : entryId.split('>')) {
        path << name.trimmed();
    }

    while (path.size() > MAX_COLUMNS) {
        const QString last = path.takeLast();
        path.last() += " > " + last;
    }

    return path;
}

//! NOTE: the first row of each next column comes from the first articulation in there in map order
QStringList ArticulationPickerModel::expandedFolders(const QStringList& folders) const
{
    const auto foldersOf = [&folders](const QStringList& entryIds) -> std::optional<QStringList> {
        for (const QString& entryId : entryIds) {
            QStringList path = pathOf(entryId);
            path.removeLast();
            if (path.mid(0, folders.size()) == folders) {
                return path;
            }
        }
        return std::nullopt;
    };

    return foldersOf(m_choices.placedEntryIds).value_or(foldersOf(m_choices.entryIds).value_or(folders));
}

//! NOTE: the folders and articulations of each opened folder, in map order (a folder where its first articulation is)
QVariantList ArticulationPickerModel::columns() const
{
    QVariantList result;
    if (!m_isOpen) {
        return result;
    }

    for (int column = 0; column <= m_openedFolders.size(); ++column) {
        QVariantList items;
        QSet<QString> folders;

        for (const QString& entryId : m_choices.entryIds) {
            const QStringList path = pathOf(entryId);
            if (path.size() <= column || path.mid(0, column) != m_openedFolders.mid(0, column)) {
                continue;
            }

            const QString& name = path.at(column);
            const bool isFolder = path.size() > column + 1;
            if (isFolder && folders.contains(name)) {
                continue;
            }

            QVariantMap item;
            item["name"] = name;
            item["isFolder"] = isFolder;
            if (isFolder) {
                folders.insert(name);
                item["isSelected"] = column < m_openedFolders.size() && m_openedFolders.at(column) == name;
            } else {
                item["entryId"] = entryId;
                item["isSelected"] = entryId == m_choices.currentEntryId;
            }
            items << item;
        }

        result << QVariant(items);
    }

    return result;
}

void ArticulationPickerModel::open(ArticulationChoices choices, const QPointF& position, std::function<void()> closed)
{
    close();

    m_choices = std::move(choices);
    m_position = position;
    m_closed = std::move(closed);

    m_openedFolders = expandedFolders({});

    // Before it opens: the popup is placed from it
    emit positionChanged();

    m_isOpen = true;
    emit isOpenChanged();
    emit columnsChanged();
}

void ArticulationPickerModel::openFolder(int column, const QString& name)
{
    if (!m_isOpen || column < 0 || column > m_openedFolders.size() || column >= MAX_COLUMNS - 1) {
        return;
    }

    m_openedFolders = expandedFolders(m_openedFolders.mid(0, column) << name);
    emit columnsChanged();
}

void ArticulationPickerModel::chooseEntry(const QString& entryId)
{
    if (!m_isOpen || !m_choices.entryIds.contains(entryId)) {
        return;
    }

    // Closed first: placing it rebuilds the lane. Copies: close() clears the choices, entryId may be one of them
    const auto choose = m_choices.choose;
    const QString chosenEntryId = entryId;
    close();
    if (choose) {
        choose(chosenEntryId);
    }
}

void ArticulationPickerModel::chooseLastPlaced()
{
    chooseEntry(m_choices.lastPlacedEntryId);
}

void ArticulationPickerModel::triggerAction(int index)
{
    if (!m_isOpen || index < 0 || index >= int(m_choices.actions.size())) {
        return;
    }

    const ArticulationChoiceAction& action = m_choices.actions.at(index);
    if (action.enabled) {
        closeAndCall(action.trigger);
    }
}

void ArticulationPickerModel::editMap()
{
    closeAndCall(m_choices.editMap);
}

void ArticulationPickerModel::openInstrumentEditor()
{
    closeAndCall(m_choices.openInstrumentEditor);
}

void ArticulationPickerModel::closeAndCall(const std::function<void()>& func)
{
    if (!m_isOpen) {
        return;
    }

    const std::function<void()> call = func; // close() clears the choices it comes from
    close();
    if (call) {
        call();
    }
}

void ArticulationPickerModel::close()
{
    if (!m_isOpen) {
        return;
    }

    m_isOpen = false;
    m_choices = {};
    m_openedFolders.clear();

    const auto closed = std::move(m_closed);
    m_closed = nullptr;

    emit isOpenChanged();
    emit columnsChanged();

    if (closed) {
        closed();
    }
}
