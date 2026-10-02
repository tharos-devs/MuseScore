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
#include <memory>
#include <optional>
#include <unordered_map>
#include <vector>

#include <QAbstractListModel>
#include <QColor>
#include <qqmlintegration.h>

#include "async/asyncable.h"
#include "modularity/ioc.h"
#include "global/iglobalconfiguration.h"
#include "global/io/ifilesystem.h"
#include "interactive/iinteractive.h"
#include "context/iglobalcontext.h"

#include "engraving/articulationmap/articulationmapparser.h"

namespace mu::notation {
//! NOTE: edits an articulation map text file (see ArticulationMapParser) as a tree of
//! folders (the submenus of the articulation picker) and articulations, shown flattened
class ArticulationMapEditorModel : public QAbstractListModel, public muse::Contextable, public muse::async::Asyncable
{
    Q_OBJECT
    QML_ELEMENT;

    Q_PROPERTY(QString filePath READ filePath NOTIFY fileChanged)
    Q_PROPERTY(QString fileName READ fileName NOTIFY fileChanged)
    Q_PROPERTY(bool isDirty READ isDirty NOTIFY dirtyChanged)
    Q_PROPERTY(bool canReloadIntoTrack READ canReloadIntoTrack NOTIFY targetTrackChanged)

    Q_PROPERTY(QString mapName READ mapName WRITE setMapName NOTIFY headerChanged)
    Q_PROPERTY(int middleCOctave READ middleCOctave WRITE setMiddleCOctave NOTIFY headerChanged)
    Q_PROPERTY(int keyswitchOffsetMs READ keyswitchOffsetMs WRITE setKeyswitchOffsetMs NOTIFY headerChanged)

    Q_PROPERTY(int selectedRow READ selectedRow WRITE setSelectedRow NOTIFY selectionChanged)
    Q_PROPERTY(bool hasSelection READ hasSelection NOTIFY selectionChanged)
    Q_PROPERTY(bool selectedIsFolder READ selectedIsFolder NOTIFY selectionChanged)
    Q_PROPERTY(QString selectedName READ selectedName NOTIFY selectionChanged)
    Q_PROPERTY(int selectedNumber READ selectedNumber NOTIFY selectionChanged)
    Q_PROPERTY(bool selectedIsDefault READ selectedIsDefault NOTIFY selectionChanged)
    Q_PROPERTY(bool selectedIsDisabled READ selectedIsDisabled NOTIFY selectionChanged)
    Q_PROPERTY(bool selectedHasKeyswitchOffset READ selectedHasKeyswitchOffset NOTIFY selectionChanged)
    Q_PROPERTY(int selectedKeyswitchOffsetMs READ selectedKeyswitchOffsetMs NOTIFY selectionChanged)
    Q_PROPERTY(int selectedNotesOffsetMs READ selectedNotesOffsetMs NOTIFY selectionChanged)
    Q_PROPERTY(QVariantList selectedMessages READ selectedMessages NOTIFY selectionChanged)

    muse::ContextInject<muse::IInteractive> interactive = { this };
    muse::ContextInject<context::IGlobalContext> globalContext = { this };
    muse::GlobalInject<muse::IGlobalConfiguration> globalConfiguration;
    muse::GlobalInject<muse::io::IFileSystem> fileSystem;

public:
    enum class DropPosition {
        Before = 0,
        After,
        Into,
    };
    Q_ENUM(DropPosition)

    enum class MessageType {
        Note = 0,
        ControlChange,
        ProgramChange,
    };
    Q_ENUM(MessageType)

    explicit ArticulationMapEditorModel(QObject* parent = nullptr);
    ~ArticulationMapEditorModel() override;

    QVariant data(const QModelIndex& index, int role) const override;
    int rowCount(const QModelIndex& parent = QModelIndex()) const override;
    QHash<int, QByteArray> roleNames() const override;

    QString filePath() const;
    QString fileName() const;
    bool isDirty() const;
    bool canReloadIntoTrack() const;

    QString mapName() const;
    void setMapName(const QString& name);
    int middleCOctave() const;
    void setMiddleCOctave(int octave);
    int keyswitchOffsetMs() const;
    void setKeyswitchOffsetMs(int ms);

    int selectedRow() const;
    void setSelectedRow(int row);
    bool hasSelection() const;
    bool selectedIsFolder() const;
    QString selectedName() const;
    int selectedNumber() const;
    bool selectedIsDefault() const;
    bool selectedIsDisabled() const;
    bool selectedHasKeyswitchOffset() const;
    int selectedKeyswitchOffsetMs() const;
    int selectedNotesOffsetMs() const;
    QVariantList selectedMessages() const;

    Q_INVOKABLE void newMap();
    Q_INVOKABLE void openMap();
    Q_INVOKABLE void openMapFile(const QString& path);
    //! NOTE: e.g. the copy of a map stored in a score, whose file is missing: saving asks for a file
    Q_INVOKABLE void openMapText(const QString& text);
    Q_INVOKABLE bool saveMap();
    Q_INVOKABLE bool saveMapAs();
    //! NOTE: offers to save unsaved changes, then emits closeAccepted unless cancelled
    Q_INVOKABLE void closeRequested();

    //! NOTE: set when opened from a Mixer track, see reloadIntoTrack
    Q_INVOKABLE void setTargetTrack(const QString& partId, const QString& instrumentId);
    //! NOTE: saves the changes if needed, then loads the file into the track, like the Mixer's "Reload"
    Q_INVOKABLE void reloadIntoTrack();

    Q_INVOKABLE void addArticulation();
    Q_INVOKABLE void addFolder();
    Q_INVOKABLE void removeSelected();
    //! NOTE: duplicates the selected articulation right below it, at the same level
    Q_INVOKABLE void copySelectedArticulation();
    Q_INVOKABLE bool canMove(int fromRow, int toRow, DropPosition position) const;
    Q_INVOKABLE void move(int fromRow, int toRow, DropPosition position);
    Q_INVOKABLE void toggleExpanded(int row);

    Q_INVOKABLE void rename(int row, const QString& name);
    Q_INVOKABLE void setColor(int row, const QColor& color);
    Q_INVOKABLE void resetColor(int row);

    Q_INVOKABLE void setSelectedIsDefault(bool isDefault);
    Q_INVOKABLE void setSelectedIsDisabled(bool isDisabled);
    Q_INVOKABLE void setSelectedHasKeyswitchOffset(bool has);
    Q_INVOKABLE void setSelectedKeyswitchOffsetMs(int ms);
    Q_INVOKABLE void setSelectedNotesOffsetMs(int ms);

    Q_INVOKABLE void addMessage();
    Q_INVOKABLE void removeMessage(int index);
    Q_INVOKABLE void setMessageType(int index, int type);
    //! NOTE: a note is typed by name (C0, F#-1...) or number, a controller/program by number
    Q_INVOKABLE bool setMessageData(int index, const QString& text);
    Q_INVOKABLE void setMessageData2(int index, int value);

signals:
    void fileChanged();
    void dirtyChanged();
    void headerChanged();
    void selectionChanged();
    void targetTrackChanged();
    void closeAccepted();

private:
    enum Roles {
        NameRole = Qt::UserRole + 1,
        IsFolderRole,
        DepthRole,
        IsExpandedRole,
        HasChildrenRole,
        NumberRole,
        ColorRole,
        HasColorRole,
        SequenceRole,
        IsDefaultRole,
        IsDisabledRole,
    };

    struct Node;
    struct Row {
        Node* node = nullptr;
        int depth = 0;
    };

    void loadFile(const muse::io::path_t& path);
    void setFile(const engraving::ArticulationMapParser::Result& file, const muse::io::path_t& path);
    bool writeFile(const muse::io::path_t& path);
    muse::io::path_t mapsDir() const;

    void confirmDiscardChanges(std::function<void()> proceed);

    void rebuildRows();
    Node* nodeAt(int row) const;
    int rowOf(const Node* node) const;
    int numberOf(const Node* node) const;
    Node* selectedEntry() const;
    void selectNode(const Node* node);

    QString uniqueName(const Node* parent, const QString& name, const Node* except = nullptr) const;
    QString pathOf(const Node* node) const;
    QString sequenceText(const Node* node) const;

    void markDirty();
    void notifyRowChanged(const Node* node);

    static std::unique_ptr<Node> copyOfArticulation(const Node& entry);

    std::unique_ptr<Node> m_root;
    std::vector<Row> m_rows;
    std::unordered_map<const Node*, int> m_numbers; // 1-based articulation numbers, folders excluded
    const Node* m_defaultNode = nullptr;
    const Node* m_selectedNode = nullptr;

    std::optional<engraving::InstrumentTrackId> m_targetTrack;

    muse::io::path_t m_filePath;
    QString m_mapName;
    int m_middleCOctave = engraving::ArticulationMapParser::DEFAULT_MIDDLE_C_OCTAVE;
    int m_keyswitchOffsetMs = 0;
    bool m_isDirty = false;
};
}
