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
#include <vector>

#include <QObject>
#include <QPointF>
#include <QStringList>
#include <QVariantList>
#include <qqmlintegration.h>

namespace mu::notation {
//! NOTE: what the articulation lane offers for a note besides the articulations themselves
//! (scope, reset, remove...), shown by both the menu and the column picker
struct ArticulationChoiceAction {
    QString title;
    bool checkable = false;
    bool checked = false;
    bool enabled = true;
    std::function<void()> trigger; // checkable: toggles it
};

struct ArticulationChoices {
    QStringList entryIds; // in map order, with their folders, e.g. "Legato > Fast"
    QString currentEntryId;
    QString lastPlacedEntryId; // empty: none
    QStringList placedEntryIds; // those already given to notes of the track, the last one first
    std::vector<ArticulationChoiceAction> actions;
    std::function<void(const QString& entryId)> choose;
    std::function<void()> editMap;
    std::function<void()> openInstrumentEditor; // none: the track plays no VST instrument with its own window
};

//! NOTE: the articulation lane's column picker, a funnel through the map's folders: a folder chosen
//! in a column fills the next one, an articulation chosen anywhere is placed. Up to 7 columns
//! (6 folder levels, the articulation map editor's limit): deeper folders of a hand-written map are
//! shown flattened in the last one, e.g. "Fast > Bold". Opening it or a folder opens the folders the
//! articulation last chosen in there was in (or the first rows), so a choice rarely needs every column clicked
class ArticulationPickerModel : public QObject
{
    Q_OBJECT
    QML_ELEMENT;
    QML_UNCREATABLE("Owned by NotationArticulationMapController")

    Q_PROPERTY(bool isOpen READ isOpen NOTIFY isOpenChanged)
    Q_PROPERTY(QPointF position READ position NOTIFY positionChanged)
    Q_PROPERTY(QString lastPlacedTitle READ lastPlacedTitle NOTIFY isOpenChanged)
    Q_PROPERTY(bool hasInstrumentEditor READ hasInstrumentEditor NOTIFY isOpenChanged)
    Q_PROPERTY(QVariantList actions READ actions NOTIFY isOpenChanged)
    Q_PROPERTY(QVariantList columns READ columns NOTIFY columnsChanged)

public:
    static constexpr int MAX_COLUMNS = 7;

    explicit ArticulationPickerModel(QObject* parent = nullptr);

    bool isOpen() const;
    QPointF position() const;
    QString lastPlacedTitle() const;
    bool hasInstrumentEditor() const;
    QVariantList actions() const;
    QVariantList columns() const;

    //! NOTE: position is in the notation view's coordinates; closed is called once it closes, however it does
    void open(ArticulationChoices choices, const QPointF& position, std::function<void()> closed);

    Q_INVOKABLE void openFolder(int column, const QString& name);
    Q_INVOKABLE void chooseEntry(const QString& entryId);
    Q_INVOKABLE void chooseLastPlaced();
    Q_INVOKABLE void triggerAction(int index);
    Q_INVOKABLE void editMap();
    Q_INVOKABLE void openInstrumentEditor();
    Q_INVOKABLE void close();

signals:
    void isOpenChanged();
    void positionChanged();
    void columnsChanged();

private:
    //! NOTE: an entry id cut into at most MAX_COLUMNS names, the last one being the articulation's
    static QStringList pathOf(const QString& entryId);

    //! NOTE: closes it first, as choosing does
    void closeAndCall(const std::function<void()>& func);

    //! NOTE: the folders to open from the given ones on: those of the articulation last chosen in there,
    //! or else each first row as long as it is a folder
    QStringList expandedFolders(const QStringList& folders) const;

    bool m_isOpen = false;
    QPointF m_position;
    ArticulationChoices m_choices;
    std::function<void()> m_closed;
    QStringList m_openedFolders; // one per column, from the first
};
}
