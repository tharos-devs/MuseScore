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

#include "articulationmapeditormodel.h"

#include "articulationmapcolors.h"

#include "engraving/articulationmap/articulationmapwriter.h"
#include "notation/imasternotation.h"
#include "notation/inotationarticulationmaps.h"
#include "global/io/fileinfo.h"
#include "translation.h"
#include "log.h"

using namespace mu::notation;
using namespace mu::engraving;
using namespace muse;

static const QString FOLDER_SEPARATOR(" > ");
static constexpr int MIN_MIDDLE_C_OCTAVE = 3;
static constexpr int MAX_MIDDLE_C_OCTAVE = 4;

struct ArticulationMapEditorModel::Node {
    bool isFolder = false;
    QString name;
    Node* parent = nullptr;
    std::vector<std::unique_ptr<Node> > children;
    bool expanded = true;

    // articulation only
    std::vector<mpe::MidiMessage> messages;
    std::vector<mpe::ArticulationType> aliases;
    bool hasImplicitAliases = true; // re-derived from the name when saving, see ArticulationMapParser::implicitAliases
    std::optional<int> keyswitchOffsetMs;
    int notesOffsetMs = 0;
    std::optional<int> channel; // 0-based, unset = the track's
    std::optional<uint32_t> color;
    bool disabled = false;

    Node* addChild(std::unique_ptr<Node> child, size_t index)
    {
        child->parent = this;
        index = std::min(index, children.size());
        return children.insert(children.begin() + index, std::move(child))->get();
    }

    std::unique_ptr<Node> takeChild(const Node* child)
    {
        for (auto it = children.begin(); it != children.end(); ++it) {
            if (it->get() == child) {
                std::unique_ptr<Node> taken = std::move(*it);
                children.erase(it);
                taken->parent = nullptr;
                return taken;
            }
        }

        return nullptr;
    }

    size_t indexOf(const Node* child) const
    {
        for (size_t i = 0; i < children.size(); ++i) {
            if (children[i].get() == child) {
                return i;
            }
        }

        return children.size();
    }

    bool contains(const Node* node) const
    {
        for (const Node* n = node; n; n = n->parent) {
            if (n == this) {
                return true;
            }
        }

        return false;
    }

    bool hasArticulation() const
    {
        for (const auto& child : children) {
            if (!child->isFolder || child->hasArticulation()) {
                return true;
            }
        }

        return false;
    }
};

static QString sanitizedName(const QString& name)
{
    // ';' starts a comment, '>' separates folders and '=' starts the score articulations
    QString result = name;
    result.remove(';').remove('>').remove('=').replace('\n', ' ').replace('\t', ' ');
    return result.simplified();
}

static mpe::MidiMessage defaultNote(int middleCOctave)
{
    std::vector<mpe::MidiMessage> messages;
    ArticulationMapParser::parseMessages(u"C0", middleCOctave, messages);
    return messages.empty() ? mpe::MidiMessage { mpe::MidiMessage::Type::Note, 24, ArticulationMapParser::DEFAULT_KEYSWITCH_VELOCITY }
           : messages.front();
}

//! NOTE: the last articulation of a subtree, in display order (nullptr if it has none)
template<typename NodeT>
static const NodeT* lastArticulationIn(const NodeT* node)
{
    if (!node->isFolder) {
        return node;
    }

    for (auto it = node->children.rbegin(); it != node->children.rend(); ++it) {
        if (const NodeT* found = lastArticulationIn<NodeT>(it->get())) {
            return found;
        }
    }

    return nullptr;
}

//! NOTE: the articulation displayed right before position `index` of `parent` (nullptr if none)
template<typename NodeT>
static const NodeT* articulationBefore(const NodeT* parent, size_t index)
{
    while (parent) {
        for (size_t i = std::min(index, parent->children.size()); i > 0; --i) {
            if (const NodeT* found = lastArticulationIn<NodeT>(parent->children[i - 1].get())) {
                return found;
            }
        }

        if (!parent->parent) {
            break;
        }

        index = parent->parent->indexOf(parent);
        parent = parent->parent;
    }

    return nullptr;
}

//! NOTE: its name and triggers - not what makes it unique in its map (explicit aliases, default)
std::unique_ptr<ArticulationMapEditorModel::Node> ArticulationMapEditorModel::copyOfArticulation(const Node& entry)
{
    auto copy = std::make_unique<Node>();
    copy->name = entry.name;
    copy->messages = entry.messages;
    copy->keyswitchOffsetMs = entry.keyswitchOffsetMs;
    copy->notesOffsetMs = entry.notesOffsetMs;
    copy->channel = entry.channel;
    copy->color = entry.color;
    copy->disabled = entry.disabled;
    return copy;
}

ArticulationMapEditorModel::ArticulationMapEditorModel(QObject* parent)
    : QAbstractListModel(parent), muse::Contextable(muse::iocCtxForQmlObject(this)), m_root(std::make_unique<Node>())
{
    m_root->isFolder = true;
}

ArticulationMapEditorModel::~ArticulationMapEditorModel() = default;

// ---- list model

QVariant ArticulationMapEditorModel::data(const QModelIndex& index, int role) const
{
    const Node* node = nodeAt(index.row());
    if (!node) {
        return QVariant();
    }

    switch (role) {
    case NameRole: return node->name;
    case IsFolderRole: return node->isFolder;
    case DepthRole: return m_rows.at(index.row()).depth;
    case IsExpandedRole: return node->expanded;
    case HasChildrenRole: return !node->children.empty();
    case NumberRole: return numberOf(node);
    case ColorRole: {
        if (node->isFolder) {
            return QColor();
        }
        return node->color ? QColor::fromRgb(*node->color) : artMapPaletteColor(numberOf(node) - 1);
    }
    case HasColorRole: return node->color.has_value();
    case SequenceRole: return sequenceText(node);
    case IsDefaultRole: return node == m_defaultNode;
    case IsDisabledRole: return node->disabled;
    }

    return QVariant();
}

int ArticulationMapEditorModel::rowCount(const QModelIndex&) const
{
    return static_cast<int>(m_rows.size());
}

QHash<int, QByteArray> ArticulationMapEditorModel::roleNames() const
{
    return {
        { NameRole, "name" },
        { IsFolderRole, "isFolder" },
        { DepthRole, "depth" },
        { IsExpandedRole, "isExpanded" },
        { HasChildrenRole, "hasChildren" },
        { NumberRole, "number" },
        { ColorRole, "articulationColor" },
        { HasColorRole, "hasOwnColor" },
        { SequenceRole, "sequence" },
        { IsDefaultRole, "isDefault" },
        { IsDisabledRole, "isDisabled" },
    };
}

void ArticulationMapEditorModel::rebuildRows()
{
    beginResetModel();

    m_rows.clear();
    m_numbers.clear();
    int number = 0;

    std::function<void(const Node*, int, bool)> visit = [&](const Node* parent, int depth, bool visible) {
        for (const auto& child : parent->children) {
            if (visible) {
                m_rows.push_back({ child.get(), depth });
            }

            if (child->isFolder) {
                visit(child.get(), depth + 1, visible && child->expanded);
            } else {
                m_numbers[child.get()] = ++number;
            }
        }
    };
    visit(m_root.get(), 0, true);

    endResetModel();

    emit selectionChanged();
}

ArticulationMapEditorModel::Node* ArticulationMapEditorModel::nodeAt(int row) const
{
    if (row < 0 || row >= static_cast<int>(m_rows.size())) {
        return nullptr;
    }

    return m_rows.at(row).node;
}

int ArticulationMapEditorModel::rowOf(const Node* node) const
{
    for (size_t i = 0; i < m_rows.size(); ++i) {
        if (m_rows[i].node == node) {
            return static_cast<int>(i);
        }
    }

    return -1;
}

int ArticulationMapEditorModel::numberOf(const Node* node) const
{
    auto it = m_numbers.find(node);
    return it == m_numbers.end() ? 0 : it->second;
}

void ArticulationMapEditorModel::notifyRowChanged(const Node* node)
{
    const int row = rowOf(node);
    if (row >= 0) {
        emit dataChanged(index(row), index(row));
    }
}

QString ArticulationMapEditorModel::pathOf(const Node* node) const
{
    QStringList names;
    for (const Node* n = node; n && n != m_root.get(); n = n->parent) {
        names.prepend(n->name);
    }

    return names.join(FOLDER_SEPARATOR);
}

QString ArticulationMapEditorModel::sequenceText(const Node* node) const
{
    QStringList parts;
    for (const mpe::MidiMessage& message : node->messages) {
        switch (message.type) {
        case mpe::MidiMessage::Type::Note: {
            QString text = ArticulationMapWriter::noteName(message.number, m_middleCOctave).toQString();
            if (message.value != ArticulationMapParser::DEFAULT_KEYSWITCH_VELOCITY) {
                text += QString(" (v%1)").arg(message.value);
            }
            parts << text;
            break;
        }
        case mpe::MidiMessage::Type::ControlChange:
            parts << QString("CC%1:%2").arg(message.number).arg(message.value);
            break;
        case mpe::MidiMessage::Type::ProgramChange:
            parts << QString("PC%1").arg(message.number);
            break;
        }
    }

    if (node->channel) {
        //: %1 is a MIDI channel number (1-16), shown after the activation sequence of an articulation
        parts << muse::qtrc("notation", "Ch %1").arg(*node->channel + 1);
    }

    return parts.join(" | ");
}

QString ArticulationMapEditorModel::uniqueName(const Node* parent, const QString& name, const Node* except) const
{
    auto isTaken = [parent, except](const QString& candidate) {
        for (const auto& child : parent->children) {
            if (child.get() != except && child->name == candidate) {
                return true;
            }
        }
        return false;
    };

    if (!isTaken(name)) {
        return name;
    }

    for (int i = 2;; ++i) {
        const QString candidate = QString("%1 %2").arg(name).arg(i);
        if (!isTaken(candidate)) {
            return candidate;
        }
    }
}

// ---- file

QString ArticulationMapEditorModel::filePath() const
{
    return m_filePath.toQString();
}

QString ArticulationMapEditorModel::fileName() const
{
    return m_filePath.empty() ? QString() : io::completeBasename(m_filePath).toQString();
}

bool ArticulationMapEditorModel::isDirty() const
{
    return m_isDirty;
}

void ArticulationMapEditorModel::markDirty()
{
    if (!m_isDirty) {
        m_isDirty = true;
        emit dirtyChanged();
    }
}

io::path_t ArticulationMapEditorModel::mapsDir() const
{
    const io::path_t dir = globalConfiguration()->userDataPath() + "/ArticulationMaps";
    if (!fileSystem()->exists(dir)) {
        fileSystem()->makePath(dir);
    }

    return dir;
}

static std::vector<std::string> fileFilter()
{
    return { muse::trc("notation", "Articulation map") + " (*.txt)" };
}

void ArticulationMapEditorModel::confirmDiscardChanges(std::function<void()> proceed)
{
    if (!m_isDirty) {
        proceed();
        return;
    }

    interactive()->question(muse::trc("notation", "Do you want to save the changes to this articulation map?"), "", {
        IInteractive::Button::DontSave, IInteractive::Button::Cancel, IInteractive::Button::Save
    }, IInteractive::Button::Save)
    .onResolve(this, [this, proceed](const IInteractive::Result& res) {
        if (res.isButton(IInteractive::Button::Save)) {
            if (saveMap()) {
                proceed();
            }
        } else if (res.isButton(IInteractive::Button::DontSave)) {
            proceed();
        }
    });
}

void ArticulationMapEditorModel::newMap()
{
    confirmDiscardChanges([this]() {
        setFile(ArticulationMapParser::Result(), io::path_t());
    });
}

void ArticulationMapEditorModel::openMap()
{
    confirmDiscardChanges([this]() {
        const io::path_t path = interactive()->selectOpeningFileSync(muse::trc("notation", "Open articulation map"), mapsDir(),
                                                                     fileFilter());
        if (!path.empty()) {
            loadFile(path);
        }
    });
}

void ArticulationMapEditorModel::openMapFile(const QString& path)
{
    loadFile(io::path_t(path));
}

void ArticulationMapEditorModel::openMapText(const QString& text)
{
    setFile(ArticulationMapParser::parse(String::fromQString(text)), io::path_t());
}

void ArticulationMapEditorModel::loadFile(const io::path_t& path)
{
    const RetVal<ByteArray> file = fileSystem()->readFile(path);
    if (!file.ret) {
        interactive()->error(muse::trc("notation", "Cannot read the articulation map"), file.ret.text());
        return;
    }

    const ArticulationMapParser::Result result = ArticulationMapParser::parse(String::fromUtf8(file.val));

    if (!result.errors.empty()) {
        std::string details;
        for (const ArticulationMapParser::Error& error : result.errors) {
            details += muse::qtrc("notation", "Line %1: %2").arg(error.line).arg(error.message.toQString()).toStdString() + "\n";
        }
        interactive()->warning(muse::trc("notation", "Some lines of the articulation map were ignored, "
                                                     "they will be lost if you save it"), details);
    }

    setFile(result, path);
}

void ArticulationMapEditorModel::setFile(const ArticulationMapParser::Result& file, const io::path_t& path)
{
    m_root = std::make_unique<Node>();
    m_root->isFolder = true;
    m_defaultNode = nullptr;
    m_selectedNode = nullptr;

    auto ensureFolder = [this](const QStringList& names) {
        Node* folder = m_root.get();
        for (const QString& name : names) {
            Node* found = nullptr;
            for (const auto& child : folder->children) {
                if (child->isFolder && child->name == name) {
                    found = child.get();
                    break;
                }
            }

            if (!found) {
                auto created = std::make_unique<Node>();
                created->isFolder = true;
                created->name = name;
                found = folder->addChild(std::move(created), folder->children.size());
            }

            folder = found;
        }
        return folder;
    };

    size_t folderIdx = 0;
    auto addFoldersBefore = [&](size_t entryIdx) {
        while (folderIdx < file.folders.size() && file.folders.at(folderIdx).entryIndex <= entryIdx) {
            ensureFolder(file.folders.at(folderIdx).path.toQString().split(FOLDER_SEPARATOR));
            ++folderIdx;
        }
    };

    for (size_t i = 0; i < file.map.entries.size(); ++i) {
        addFoldersBefore(i);

        const ExpressionMapEntry& entry = file.map.entries.at(i);
        QStringList names = entry.id.toQString().split(FOLDER_SEPARATOR);
        const QString leafName = names.takeLast();

        auto node = std::make_unique<Node>();
        node->name = leafName;
        node->messages = entry.messages;
        node->aliases = entry.aliases;
        node->hasImplicitAliases = entry.aliases == ArticulationMapParser::implicitAliases(entry.id);
        node->keyswitchOffsetMs = entry.keyswitchOffsetMs;
        node->notesOffsetMs = entry.notesOffsetMs;
        node->channel = entry.channel;
        node->color = entry.color;
        node->disabled = entry.disabled;

        Node* folder = ensureFolder(names);
        Node* added = folder->addChild(std::move(node), folder->children.size());
        if (entry.id == file.map.defaultEntryId) {
            m_defaultNode = added;
        }
    }
    addFoldersBefore(file.map.entries.size());

    m_filePath = path;
    m_mapName = file.map.name.toQString();
    m_middleCOctave = file.middleCOctave;
    m_keyswitchOffsetMs = file.map.keyswitchOffsetMs;
    m_isDirty = false;

    rebuildRows();

    if (!m_rows.empty()) {
        selectNode(m_rows.front().node);
    }

    emit fileChanged();
    emit headerChanged();
    emit dirtyChanged();
}

bool ArticulationMapEditorModel::writeFile(const io::path_t& path)
{
    ArticulationMapParser::Result file;
    file.map.name = String::fromQString(m_mapName);
    file.map.keyswitchOffsetMs = m_keyswitchOffsetMs;
    file.middleCOctave = m_middleCOctave;

    QStringList withoutMessages;

    std::function<void(const Node*)> visit = [&](const Node* parent) {
        for (const auto& child : parent->children) {
            const String path = String::fromQString(pathOf(child.get()));

            if (child->isFolder) {
                if (!child->hasArticulation()) {
                    file.folders.push_back({ path, file.map.entries.size() });
                }
                visit(child.get());
                continue;
            }

            // A channel alone is enough, e.g. one instrument per channel in Kontakt
            if (child->messages.empty() && !child->channel) {
                withoutMessages << pathOf(child.get());
                continue;
            }

            ExpressionMapEntry entry;
            entry.id = path;
            entry.messages = child->messages;
            entry.aliases = child->hasImplicitAliases ? ArticulationMapParser::implicitAliases(path) : child->aliases;
            entry.keyswitchOffsetMs = child->keyswitchOffsetMs;
            entry.notesOffsetMs = child->notesOffsetMs;
            entry.channel = child->channel;
            entry.color = child->color;
            entry.disabled = child->disabled;

            if (child.get() == m_defaultNode) {
                file.map.defaultEntryId = path;
            }

            file.map.entries.push_back(std::move(entry));
        }
    };
    visit(m_root.get());

    if (!withoutMessages.empty()) {
        interactive()->error(muse::trc("notation", "Some articulations have no activation sequence"),
                             withoutMessages.join("\n").toStdString());
        return false;
    }

    const String text = ArticulationMapWriter::write(file);
    const Ret ret = fileSystem()->writeFile(path, text.toUtf8());
    if (!ret) {
        interactive()->error(muse::trc("notation", "Cannot save the articulation map"), ret.text());
        return false;
    }

    m_filePath = path;
    m_isDirty = false;

    emit fileChanged();
    emit dirtyChanged();

    return true;
}

bool ArticulationMapEditorModel::saveMap()
{
    if (m_filePath.empty()) {
        return saveMapAs();
    }

    return writeFile(m_filePath);
}

bool ArticulationMapEditorModel::saveMapAs()
{
    QString baseName = fileName();
    if (baseName.isEmpty()) {
        baseName = m_mapName.isEmpty() ? muse::qtrc("notation", "Articulation map") : m_mapName;
    }

    io::path_t path = interactive()->selectSavingFileSync(muse::trc("notation", "Save articulation map"),
                                                          mapsDir() + "/" + baseName + ".txt", fileFilter());
    if (path.empty()) {
        return false;
    }

    if (io::suffix(path) != "txt") {
        path = path.appendingSuffix("txt");
    }

    return writeFile(path);
}

void ArticulationMapEditorModel::closeRequested()
{
    confirmDiscardChanges([this]() {
        emit closeAccepted();
    });
}

// ---- target track

bool ArticulationMapEditorModel::canReloadIntoTrack() const
{
    return m_targetTrack.has_value();
}

void ArticulationMapEditorModel::setTargetTrack(const QString& partId, const QString& instrumentId)
{
    bool ok = false;
    const uint64_t part = partId.toULongLong(&ok);
    if (!ok || instrumentId.isEmpty()) {
        return;
    }

    m_targetTrack = engraving::InstrumentTrackId { muse::ID(part), String::fromQString(instrumentId) };
    emit targetTrackChanged();
}

void ArticulationMapEditorModel::reloadIntoTrack()
{
    if (!m_targetTrack) {
        return;
    }

    if ((m_isDirty || m_filePath.empty()) && !saveMap()) {
        return;
    }

    const IMasterNotationPtr masterNotation = globalContext()->currentMasterNotation();
    const INotationArticulationMapsPtr maps = masterNotation ? masterNotation->articulationMaps() : nullptr;
    if (!maps || !maps->data()) {
        return;
    }

    //! NOTE: a track without a map yet (the editor opened from its "New…" menu item) gets it attached
    const bool trackHasMap = maps->data()->map(*m_targetTrack) != nullptr;

    const RetVal<ByteArray> file = fileSystem()->readFile(m_filePath);
    if (!file.ret) {
        interactive()->error(muse::trc("notation", "Cannot read the articulation map"), file.ret.text());
        return;
    }

    ArticulationMapParser::Result result = ArticulationMapParser::parse(String::fromUtf8(file.val));
    if (result.map.name.empty()) {
        result.map.name = io::completeBasename(m_filePath).toString();
    }
    result.map.sourcePath = m_filePath.toString();

    EditArticulationMapChanges changes;
    changes.maps.emplace(*m_targetTrack, std::move(result.map));
    maps->edit(changes, trackHasMap ? muse::TranslatableString("undoableAction", "Reload articulation map")
               : muse::TranslatableString("undoableAction", "Load articulation map"));
}

// ---- header

QString ArticulationMapEditorModel::mapName() const
{
    return m_mapName;
}

void ArticulationMapEditorModel::setMapName(const QString& name)
{
    const QString sanitized = name.simplified().remove(';');
    if (m_mapName == sanitized) {
        return;
    }

    m_mapName = sanitized;
    markDirty();
    emit headerChanged();
}

int ArticulationMapEditorModel::middleCOctave() const
{
    return m_middleCOctave;
}

void ArticulationMapEditorModel::setMiddleCOctave(int octave)
{
    octave = std::clamp(octave, MIN_MIDDLE_C_OCTAVE, MAX_MIDDLE_C_OCTAVE);
    if (m_middleCOctave == octave) {
        return;
    }

    //! NOTE: the messages keep their MIDI numbers, only their names change (C0 becomes C1...)
    m_middleCOctave = octave;
    markDirty();
    emit headerChanged();

    if (!m_rows.empty()) {
        emit dataChanged(index(0), index(static_cast<int>(m_rows.size()) - 1));
    }
    emit selectionChanged();
}

int ArticulationMapEditorModel::keyswitchOffsetMs() const
{
    return m_keyswitchOffsetMs;
}

void ArticulationMapEditorModel::setKeyswitchOffsetMs(int ms)
{
    if (m_keyswitchOffsetMs == ms) {
        return;
    }

    m_keyswitchOffsetMs = ms;
    markDirty();
    emit headerChanged();
    emit selectionChanged();
}

// ---- selection

int ArticulationMapEditorModel::selectedRow() const
{
    return rowOf(m_selectedNode);
}

void ArticulationMapEditorModel::setSelectedRow(int row)
{
    selectNode(nodeAt(row));
}

void ArticulationMapEditorModel::selectNode(const Node* node)
{
    if (m_selectedNode == node) {
        return;
    }

    m_selectedNode = node;
    emit selectionChanged();
}

ArticulationMapEditorModel::Node* ArticulationMapEditorModel::selectedEntry() const
{
    const int row = rowOf(m_selectedNode);
    Node* node = nodeAt(row);
    return node && !node->isFolder ? node : nullptr;
}

bool ArticulationMapEditorModel::hasSelection() const
{
    return rowOf(m_selectedNode) >= 0;
}

bool ArticulationMapEditorModel::selectedIsFolder() const
{
    return hasSelection() && m_selectedNode->isFolder;
}

QString ArticulationMapEditorModel::selectedName() const
{
    return hasSelection() ? m_selectedNode->name : QString();
}

int ArticulationMapEditorModel::selectedNumber() const
{
    return hasSelection() ? numberOf(m_selectedNode) : 0;
}

bool ArticulationMapEditorModel::selectedIsDefault() const
{
    return hasSelection() && m_selectedNode == m_defaultNode;
}

bool ArticulationMapEditorModel::selectedIsDisabled() const
{
    const Node* entry = selectedEntry();
    return entry && entry->disabled;
}

bool ArticulationMapEditorModel::selectedHasKeyswitchOffset() const
{
    const Node* entry = selectedEntry();
    return entry && entry->keyswitchOffsetMs.has_value();
}

int ArticulationMapEditorModel::selectedKeyswitchOffsetMs() const
{
    const Node* entry = selectedEntry();
    return entry ? entry->keyswitchOffsetMs.value_or(m_keyswitchOffsetMs) : 0;
}

int ArticulationMapEditorModel::selectedNotesOffsetMs() const
{
    const Node* entry = selectedEntry();
    return entry ? entry->notesOffsetMs : 0;
}

int ArticulationMapEditorModel::selectedChannel() const
{
    const Node* entry = selectedEntry();
    return entry && entry->channel ? *entry->channel + 1 : 0;
}

QVariantList ArticulationMapEditorModel::selectedMessages() const
{
    QVariantList result;

    const Node* entry = selectedEntry();
    if (!entry) {
        return result;
    }

    for (const mpe::MidiMessage& message : entry->messages) {
        QVariantMap item;
        switch (message.type) {
        case mpe::MidiMessage::Type::Note:
            item["type"] = static_cast<int>(MessageType::Note);
            item["data"] = ArticulationMapWriter::noteName(message.number, m_middleCOctave).toQString();
            item["data2"] = message.value;
            item["hasData2"] = true;
            break;
        case mpe::MidiMessage::Type::ControlChange:
            item["type"] = static_cast<int>(MessageType::ControlChange);
            item["data"] = QString::number(message.number);
            item["data2"] = message.value;
            item["hasData2"] = true;
            break;
        case mpe::MidiMessage::Type::ProgramChange:
            item["type"] = static_cast<int>(MessageType::ProgramChange);
            item["data"] = QString::number(message.number);
            item["data2"] = 0;
            item["hasData2"] = false;
            break;
        }
        result << item;
    }

    return result;
}

// ---- structure

void ArticulationMapEditorModel::addArticulation()
{
    Node* parent = m_root.get();
    size_t index = parent->children.size();

    if (hasSelection()) {
        Node* selected = nodeAt(rowOf(m_selectedNode));
        if (selected->isFolder) {
            parent = selected;
            parent->expanded = true;
            index = parent->children.size();
        } else {
            parent = selected->parent;
            index = parent->indexOf(selected) + 1;
        }
    }

    auto node = std::make_unique<Node>();
    node->name = uniqueName(parent, muse::qtrc("notation", "New articulation"));
    node->messages = { defaultNote(m_middleCOctave) };

    //! NOTE: keyswitches usually follow each other chromatically - continue from the
    //! articulation right before this one, the one after its first keyswitch note
    if (const Node* previous = articulationBefore(parent, index)) {
        auto it = std::find_if(previous->messages.cbegin(), previous->messages.cend(), [](const mpe::MidiMessage& m) {
            return m.type == mpe::MidiMessage::Type::Note;
        });
        if (it != previous->messages.cend() && it->number < 127) {
            node->messages = { mpe::MidiMessage { mpe::MidiMessage::Type::Note, uint8_t(it->number + 1), it->value } };
        }
    }

    const Node* added = parent->addChild(std::move(node), index);
    if (!m_defaultNode) {
        m_defaultNode = added;
    }

    m_selectedNode = added;
    rebuildRows();
    markDirty();
}

//! NOTE: always at the end of the map, outside of any folder - drag it into a folder to make it a subfolder
void ArticulationMapEditorModel::addFolder()
{
    Node* parent = m_root.get();
    const size_t index = parent->children.size();

    auto node = std::make_unique<Node>();
    node->isFolder = true;
    node->name = uniqueName(parent, muse::qtrc("notation", "New folder"));

    m_selectedNode = parent->addChild(std::move(node), index);
    rebuildRows();
    markDirty();
}

void ArticulationMapEditorModel::copySelectedArticulation()
{
    Node* entry = selectedEntry();
    if (!entry || !entry->parent) {
        return;
    }

    Node* parent = entry->parent;
    std::unique_ptr<Node> copy = copyOfArticulation(*entry);
    copy->name = uniqueName(parent, entry->name);

    m_selectedNode = parent->addChild(std::move(copy), parent->indexOf(entry) + 1);
    rebuildRows();
    markDirty();
}

void ArticulationMapEditorModel::removeSelected()
{
    const int row = rowOf(m_selectedNode);
    Node* node = nodeAt(row);
    if (!node) {
        return;
    }

    auto doRemove = [this, node]() {
        Node* parent = node->parent;
        const size_t index = parent->indexOf(node);

        if (m_defaultNode && node->contains(m_defaultNode)) {
            m_defaultNode = nullptr;
        }

        std::unique_ptr<Node> removed = parent->takeChild(node);

        // select what takes its place
        if (index < parent->children.size()) {
            m_selectedNode = parent->children[index].get();
        } else if (index > 0) {
            m_selectedNode = parent->children[index - 1].get();
        } else {
            m_selectedNode = parent == m_root.get() ? nullptr : parent;
        }

        rebuildRows();
        markDirty();
    };

    if (!node->isFolder || node->children.empty()) {
        doRemove();
        return;
    }

    interactive()->question("", muse::qtrc("notation", "Remove the folder “%1” and everything in it?").arg(node->name).toStdString(), {
        IInteractive::Button::Cancel, IInteractive::Button::Yes
    }, IInteractive::Button::Yes)
    .onResolve(this, [doRemove](const IInteractive::Result& res) {
        if (res.isButton(IInteractive::Button::Yes)) {
            doRemove();
        }
    });
}

//! NOTE: toRow -1 = at the end of the map, outside of any folder
bool ArticulationMapEditorModel::canMove(int fromRow, int toRow, DropPosition position) const
{
    const Node* from = nodeAt(fromRow);
    if (toRow < 0) {
        return from && position == DropPosition::After;
    }

    const Node* to = nodeAt(toRow);
    if (!from || !to || from == to) {
        return false;
    }

    // a folder can't go into itself
    if (from->isFolder && from->contains(to)) {
        return false;
    }

    return position != DropPosition::Into || to->isFolder;
}

void ArticulationMapEditorModel::move(int fromRow, int toRow, DropPosition position)
{
    if (!canMove(fromRow, toRow, position)) {
        return;
    }

    Node* from = nodeAt(fromRow);
    Node* to = toRow >= 0 ? nodeAt(toRow) : nullptr;

    std::unique_ptr<Node> taken = from->parent->takeChild(from);

    Node* parent = nullptr;
    size_t index = 0;
    if (!to) {
        parent = m_root.get();
        index = parent->children.size();
    } else if (position == DropPosition::Into) {
        parent = to;
        parent->expanded = true;
        index = parent->children.size();
    } else {
        parent = to->parent;
        index = parent->indexOf(to) + (position == DropPosition::After ? 1 : 0);
    }

    taken->name = uniqueName(parent, taken->name);
    parent->addChild(std::move(taken), index);

    rebuildRows();
    markDirty();
}

void ArticulationMapEditorModel::toggleExpanded(int row)
{
    Node* node = nodeAt(row);
    if (!node || !node->isFolder) {
        return;
    }

    node->expanded = !node->expanded;
    rebuildRows();
}

// ---- articulation properties

void ArticulationMapEditorModel::rename(int row, const QString& name)
{
    Node* node = nodeAt(row);
    const QString sanitized = sanitizedName(name);
    if (!node || sanitized.isEmpty() || sanitized == node->name) {
        return;
    }

    node->name = uniqueName(node->parent, sanitized, node);
    notifyRowChanged(node);
    markDirty();

    if (node == m_selectedNode) {
        emit selectionChanged();
    }
}

void ArticulationMapEditorModel::setColor(int row, const QColor& color)
{
    Node* node = nodeAt(row);
    if (!node || node->isFolder || !color.isValid()) {
        return;
    }

    node->color = color.rgb() & 0xFFFFFF;
    notifyRowChanged(node);
    markDirty();
}

void ArticulationMapEditorModel::resetColor(int row)
{
    Node* node = nodeAt(row);
    if (!node || !node->color) {
        return;
    }

    node->color.reset();
    notifyRowChanged(node);
    markDirty();
}

void ArticulationMapEditorModel::setSelectedIsDisabled(bool isDisabled)
{
    Node* entry = selectedEntry();
    if (!entry || entry->disabled == isDisabled) {
        return;
    }

    entry->disabled = isDisabled;

    // a disabled articulation is never played, it can't be the one played by default
    if (isDisabled && entry == m_defaultNode) {
        m_defaultNode = nullptr;
    }

    notifyRowChanged(entry);
    markDirty();
    emit selectionChanged();
}

void ArticulationMapEditorModel::setSelectedIsDefault(bool isDefault)
{
    Node* entry = selectedEntry();
    if (!entry || isDefault == (entry == m_defaultNode) || (isDefault && entry->disabled)) {
        return;
    }

    const Node* previous = m_defaultNode;
    m_defaultNode = isDefault ? entry : nullptr;

    if (previous) {
        notifyRowChanged(previous);
    }
    notifyRowChanged(entry);
    markDirty();
    emit selectionChanged();
}

void ArticulationMapEditorModel::setSelectedHasKeyswitchOffset(bool has)
{
    Node* entry = selectedEntry();
    if (!entry || has == entry->keyswitchOffsetMs.has_value()) {
        return;
    }

    if (has) {
        entry->keyswitchOffsetMs = m_keyswitchOffsetMs;
    } else {
        entry->keyswitchOffsetMs.reset();
    }

    markDirty();
    emit selectionChanged();
}

void ArticulationMapEditorModel::setSelectedKeyswitchOffsetMs(int ms)
{
    Node* entry = selectedEntry();
    if (!entry || entry->keyswitchOffsetMs == ms) {
        return;
    }

    entry->keyswitchOffsetMs = ms;
    markDirty();
    emit selectionChanged();
}

void ArticulationMapEditorModel::setSelectedChannel(int channel)
{
    Node* entry = selectedEntry();
    const std::optional<int> newChannel = channel >= 1 && channel <= ArticulationMapParser::MIDI_CHANNEL_COUNT
                                          ? std::optional<int>(channel - 1) : std::nullopt;
    if (!entry || entry->channel == newChannel) {
        return;
    }

    entry->channel = newChannel;
    notifyRowChanged(entry);
    markDirty();
    emit selectionChanged();
}

void ArticulationMapEditorModel::setSelectedNotesOffsetMs(int ms)
{
    Node* entry = selectedEntry();
    if (!entry || entry->notesOffsetMs == ms) {
        return;
    }

    entry->notesOffsetMs = ms;
    markDirty();
    emit selectionChanged();
}

// ---- activation sequence

void ArticulationMapEditorModel::addMessage()
{
    Node* entry = selectedEntry();
    if (!entry) {
        return;
    }

    entry->messages.push_back(defaultNote(m_middleCOctave));
    notifyRowChanged(entry);
    markDirty();
    emit selectionChanged();
}

void ArticulationMapEditorModel::removeMessage(int index)
{
    Node* entry = selectedEntry();
    if (!entry || index < 0 || index >= static_cast<int>(entry->messages.size())) {
        return;
    }

    entry->messages.erase(entry->messages.begin() + index);
    notifyRowChanged(entry);
    markDirty();
    emit selectionChanged();
}

void ArticulationMapEditorModel::setMessageType(int index, int type)
{
    Node* entry = selectedEntry();
    if (!entry || index < 0 || index >= static_cast<int>(entry->messages.size())) {
        return;
    }

    mpe::MidiMessage& message = entry->messages[index];
    switch (static_cast<MessageType>(type)) {
    case MessageType::Note:
        if (message.type != mpe::MidiMessage::Type::Note) {
            message = defaultNote(m_middleCOctave);
        }
        break;
    case MessageType::ControlChange:
        if (message.type != mpe::MidiMessage::Type::ControlChange) {
            message = { mpe::MidiMessage::Type::ControlChange, 1, 127 };
        }
        break;
    case MessageType::ProgramChange:
        if (message.type != mpe::MidiMessage::Type::ProgramChange) {
            message = { mpe::MidiMessage::Type::ProgramChange, 0, 0 };
        }
        break;
    }

    notifyRowChanged(entry);
    markDirty();
    emit selectionChanged();
}

bool ArticulationMapEditorModel::setMessageData(int index, const QString& text)
{
    Node* entry = selectedEntry();
    if (!entry || index < 0 || index >= static_cast<int>(entry->messages.size())) {
        return false;
    }

    mpe::MidiMessage& message = entry->messages[index];
    const QString trimmed = text.trimmed();

    bool isNumber = false;
    const int number = trimmed.toInt(&isNumber);
    int newNumber = -1;

    if (isNumber) {
        newNumber = number;
    } else if (message.type == mpe::MidiMessage::Type::Note) {
        std::vector<mpe::MidiMessage> parsed;
        if (ArticulationMapParser::parseMessages(String::fromQString(trimmed), m_middleCOctave, parsed) && parsed.size() == 1
            && parsed.front().type == mpe::MidiMessage::Type::Note) {
            newNumber = parsed.front().number;
        }
    }

    if (newNumber < 0 || newNumber > 127) {
        emit selectionChanged(); // restores the previous value in the field
        return false;
    }

    message.number = static_cast<uint8_t>(newNumber);
    notifyRowChanged(entry);
    markDirty();
    emit selectionChanged();
    return true;
}

void ArticulationMapEditorModel::setMessageData2(int index, int value)
{
    Node* entry = selectedEntry();
    if (!entry || index < 0 || index >= static_cast<int>(entry->messages.size())) {
        return;
    }

    mpe::MidiMessage& message = entry->messages[index];
    if (message.type == mpe::MidiMessage::Type::ProgramChange) {
        return;
    }

    // a note-on with velocity 0 is a note-off
    const int minValue = message.type == mpe::MidiMessage::Type::Note ? 1 : 0;
    message.value = static_cast<uint8_t>(std::clamp(value, minValue, 127));

    notifyRowChanged(entry);
    markDirty();
    emit selectionChanged();
}
