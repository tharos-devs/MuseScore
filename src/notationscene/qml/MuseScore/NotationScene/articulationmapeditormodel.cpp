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

#include <algorithm>
#include <unordered_set>

#include <QRawFont>

#include "articulationmapcolors.h"

#include "engraving/articulationmap/articulationmapwriter.h"
#include "notation/imasternotation.h"
#include "notation/inotationplayback.h"
#include "notation/inotationarticulationmaps.h"
#include "project/inotationproject.h"
#include "project/iprojectaudiosettings.h"
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

//! NOTE: its name and triggers - not what makes it unique in its map (default); its score markings are set by the caller,
//! once it's named
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
    case IsSelectedRole: return isSelected(node);
    case ScoreMarkingsRole: return node->isFolder ? QVariantList() : scoreMarkingsOf(node);
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
        { IsSelectedRole, "isSelected" },
        { ScoreMarkingsRole, "scoreMarkings" },
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

    //! NOTE: only what's shown stays selected (not what's in a collapsed folder); a current row set alone
    //! (e.g. a new articulation) is selected alone
    std::unordered_set<const Node*> shown;
    for (const Row& row : m_rows) {
        shown.insert(row.node);
    }
    std::erase_if(m_selectedNodes, [&shown](const Node* node) { return !shown.contains(node); });

    if (m_selectedNode && shown.contains(m_selectedNode)) {
        if (!isSelected(m_selectedNode)) {
            m_selectedNodes = { m_selectedNode };
            m_selectionAnchor = m_selectedNode;
        }
    } else {
        m_selectedNode = m_selectedNodes.empty() ? nullptr : m_selectedNodes.back();
    }

    if (!m_selectionAnchor || !shown.contains(m_selectionAnchor)) {
        m_selectionAnchor = m_selectedNode;
    }

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

void ArticulationMapEditorModel::notifyAllRowsChanged()
{
    if (!m_rows.empty()) {
        emit dataChanged(index(0), index(static_cast<int>(m_rows.size()) - 1));
    }
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

//! NOTE: only the name's last segment counts for its own (see ArticulationMapParser::implicitAliases()): the node's name,
//! without building its path - this runs for every row and every row above it (see scoreMarkingsOf())
std::vector<mpe::ArticulationType> ArticulationMapEditorModel::scoreArticulationsOf(const Node* node) const
{
    return node->hasImplicitAliases ? ArticulationMapParser::implicitAliases(String::fromQString(node->name)) : node->aliases;
}

QVariantList ArticulationMapEditorModel::scoreMarkingsOf(const Node* node) const
{
    //! NOTE: like ExpressionMap::entryForArticulations(): the first enabled articulation of the map with the marking
    std::vector<mpe::ArticulationType> takenAbove;
    bool found = false;
    std::function<void(const Node*)> visit = [&](const Node* parent) {
        for (const auto& child : parent->children) {
            if (found) {
                return;
            }
            if (child.get() == node) {
                found = true;
                return;
            }
            if (child->isFolder) {
                visit(child.get());
            } else if (!child->disabled) {
                for (const mpe::ArticulationType type : scoreArticulationsOf(child.get())) {
                    takenAbove.push_back(type);
                }
            }
        }
    };
    visit(m_root.get());

    QVariantList result;
    for (const mpe::ArticulationType type : scoreArticulationsOf(node)) {
        QVariantMap marking;
        marking["name"] = ArticulationMapParser::scoreArticulationName(type).toQString();
        marking["shadowed"] = std::find(takenAbove.cbegin(), takenAbove.cend(), type) != takenAbove.cend();
        result << marking;
    }

    return result;
}

//! NOTE: back to the name's own when they're that: they follow a rename then, and aren't written in the file
void ArticulationMapEditorModel::setScoreArticulations(Node* node, const std::vector<mpe::ArticulationType>& types)
{
    node->hasImplicitAliases = types == ArticulationMapParser::implicitAliases(String::fromQString(node->name));
    node->aliases = node->hasImplicitAliases ? std::vector<mpe::ArticulationType> {} : types;
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
        m_editsTargetTrackMap = false;
        setFile(ArticulationMapParser::Result(), io::path_t());
    });
}

void ArticulationMapEditorModel::openMap()
{
    confirmDiscardChanges([this]() {
        const io::path_t path = interactive()->selectOpeningFileSync(muse::trc("notation", "Open articulation map"), mapsDir(),
                                                                     fileFilter());
        if (!path.empty()) {
            m_editsTargetTrackMap = false;
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
    m_selectedNodes.clear();
    m_selectionAnchor = nullptr;

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

//! NOTE: the tracks of the open score keep their own copy of the map, so they get the saved file right away
//! (an articulation disabled in the editor and saved was still played until "Reload")
bool ArticulationMapEditorModel::writeFile(const io::path_t& path, bool reloadTracks)
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

    if (reloadTracks) {
        reloadTracksUsingFile(path, targetTrackToUpdate());
    }

    return true;
}

//! NOTE The target track, if the edited map is still its own: its map may have been removed meanwhile (No map,
//! the part deleted...), then it isn't given one back
std::optional<InstrumentTrackId> ArticulationMapEditorModel::targetTrackToUpdate() const
{
    if (!m_editsTargetTrackMap || !m_targetTrack) {
        return std::nullopt;
    }

    const IMasterNotationPtr masterNotation = globalContext()->currentMasterNotation();
    const INotationArticulationMapsPtr maps = masterNotation ? masterNotation->articulationMaps() : nullptr;
    if (!maps || !maps->data() || !maps->data()->map(*m_targetTrack)) {
        return std::nullopt;
    }

    return m_targetTrack;
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

    const bool subscribed = m_targetTrack.has_value();
    m_targetTrack = engraving::InstrumentTrackId { muse::ID(part), String::fromQString(instrumentId) };

    // a track without a map yet (New…) gets the edited one only through Reload into track
    const IMasterNotationPtr masterNotation = globalContext()->currentMasterNotation();
    const INotationArticulationMapsPtr maps = masterNotation ? masterNotation->articulationMaps() : nullptr;
    m_editsTargetTrackMap = maps && maps->data() && maps->data()->map(*m_targetTrack);

    emit targetTrackChanged();

    if (!subscribed) {
        //! NOTE: the track's sound changed, or its window was opened/closed (from here or elsewhere)
        playbackController()->trackAdded().onReceive(this, [this](muse::audio::TrackId) {
            updateInstrumentEditorState();
        });
        playbackController()->trackRemoved().onReceive(this, [this](muse::audio::TrackId) {
            updateInstrumentEditorState();
        });
        if (const project::INotationProjectPtr project = globalContext()->currentProject()) {
            project->audioSettings()->trackInputParamsChanged().onReceive(this, [this](const engraving::InstrumentTrackId&) {
                updateInstrumentEditorState();
            });
        }
#ifdef MUSE_MODULE_VST
        if (vstPluginStateProvider()) {
            vstPluginStateProvider()->editorsOpenedChanged().onNotify(this, [this]() {
                updateInstrumentEditorState();
            });
        }
#endif
    }

    updateInstrumentEditorState();
}

std::optional<ArticulationMapEditorModel::InstrumentEditorTarget> ArticulationMapEditorModel::instrumentEditorTarget() const
{
    const project::INotationProjectPtr project = globalContext()->currentProject();
    if (!m_targetTrack || !project || !project->audioSettings()) {
        return std::nullopt;
    }

    const audio::AudioInputParams& params = project->audioSettings()->trackInputParams(*m_targetTrack);
    if (params.type() != audio::AudioSourceType::Vsti || !audio::hasNativeEditorSupport(params.resourceMeta)) {
        return std::nullopt;
    }

    const playback::IPlaybackController::InstrumentTrackIdMap& trackIds = playbackController()->instrumentTrackIdMap();
    auto it = trackIds.find(*m_targetTrack);
    if (it == trackIds.end()) {
        return std::nullopt;
    }

    return InstrumentEditorTarget { it->second, params.resourceMeta.id };
}

void ArticulationMapEditorModel::updateInstrumentEditorState()
{
    const std::optional<InstrumentEditorTarget> target = instrumentEditorTarget();
    const bool hasEditor = target.has_value();
    bool opened = false;
#ifdef MUSE_MODULE_VST
    opened = target && vstPluginStateProvider() && vstPluginStateProvider()->isInstrumentEditorOpened(target->resourceId, target->trackId);
#endif

    if (hasEditor == m_hasInstrumentEditor && opened == m_instrumentEditorOpened) {
        return;
    }

    m_hasInstrumentEditor = hasEditor;
    m_instrumentEditorOpened = opened;
    emit instrumentEditorChanged();
}

bool ArticulationMapEditorModel::hasInstrumentEditor() const
{
    return m_hasInstrumentEditor;
}

bool ArticulationMapEditorModel::instrumentEditorOpened() const
{
    return m_instrumentEditorOpened;
}

void ArticulationMapEditorModel::sendArticulation(int row)
{
    const Node* node = nodeAt(row);
    if (!node || node->isFolder || (node->messages.empty() && !node->channel) || !instrumentEditorTarget()) {
        return;
    }

    const project::INotationProjectPtr project = globalContext()->currentProject();
    if (!project || !project->masterNotation() || playbackController()->isPlaying()) {
        return;
    }

    mpe::MidiMessagesEvent event;
    event.messages = node->messages;
    event.channel = node->channel.value_or(-1);

    project->masterNotation()->playback()->triggerMidiMessages(*m_targetTrack, event);
}

void ArticulationMapEditorModel::openInstrumentEditor()
{
    const std::optional<InstrumentEditorTarget> target = instrumentEditorTarget();
    if (!target) {
        return;
    }

    actions::ActionQuery query("action://vst/instrument_editor");
    query.addParam("trackId", Val(target->trackId));
    query.addParam("resourceId", Val(target->resourceId));
    actionsDispatcher()->dispatch(query);

    //! NOTE: same as the Mixer's (see MixerChannelItem::openEditor()): a plugin may change its state without
    //! reporting it once its window is open
    if (const project::INotationProjectPtr project = globalContext()->currentProject()) {
        project->audioSettings()->markAsChanged();
    }
}

void ArticulationMapEditorModel::reloadIntoTrack()
{
    if (!m_targetTrack) {
        return;
    }

    // Saved without reloading: the target track is reloaded below, together with the others using the file
    if (m_filePath.empty()) {
        if (!saveMapAs()) {
            return;
        }
    } else if (m_isDirty && !writeFile(m_filePath, false /*reloadTracks*/)) {
        return;
    }

    reloadTracksUsingFile(m_filePath, m_targetTrack);
}

//! NOTE: one undoable action for every track whose map comes from this file, plus alsoTrack (e.g. a track without
//! a map yet, opened from its "New…" menu item, gets it attached)
void ArticulationMapEditorModel::reloadTracksUsingFile(const io::path_t& path, const std::optional<InstrumentTrackId>& alsoTrack)
{
    const IMasterNotationPtr masterNotation = globalContext()->currentMasterNotation();
    const INotationArticulationMapsPtr maps = masterNotation ? masterNotation->articulationMaps() : nullptr;
    if (!maps || !maps->data()) {
        return;
    }

    const String sourcePath = path.toString();

    std::vector<InstrumentTrackId> tracks;
    bool attachesNewMap = false;
    for (const auto& [trackId, map] : maps->data()->maps()) {
        if (map.sourcePath == sourcePath) {
            tracks.push_back(trackId);
        }
    }
    if (alsoTrack && std::find(tracks.cbegin(), tracks.cend(), *alsoTrack) == tracks.cend()) {
        tracks.push_back(*alsoTrack);
        attachesNewMap = maps->data()->map(*alsoTrack) == nullptr;
    }

    if (tracks.empty()) {
        return;
    }

    const RetVal<ByteArray> file = fileSystem()->readFile(path);
    if (!file.ret) {
        interactive()->error(muse::trc("notation", "Cannot read the articulation map"), file.ret.text());
        return;
    }

    ArticulationMapParser::Result result = ArticulationMapParser::parse(String::fromUtf8(file.val));
    if (result.map.name.empty()) {
        result.map.name = io::completeBasename(path).toString();
    }
    result.map.sourcePath = sourcePath;

    // An unchanged map isn't set again: saving without changes adds nothing to undo
    EditArticulationMapChanges changes;
    for (const InstrumentTrackId& trackId : tracks) {
        const ExpressionMap* current = maps->data()->map(trackId);
        if (!current || !(*current == result.map)) {
            changes.maps.emplace(trackId, result.map);
        }
    }

    if (changes.empty()) {
        return;
    }

    maps->edit(changes, attachesNewMap ? muse::TranslatableString("undoableAction", "Load articulation map")
               : muse::TranslatableString("undoableAction", "Reload articulation map"));
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

    notifyAllRowsChanged();
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
    if (node) {
        setSelection({ node }, node);
    } else {
        setSelection({}, nullptr);
    }
}

void ArticulationMapEditorModel::setSelection(const std::vector<const Node*>& nodes, const Node* current)
{
    if (m_selectedNodes == nodes && m_selectedNode == current) {
        return;
    }

    m_selectedNodes = nodes;
    m_selectedNode = current;
    m_selectionAnchor = current;
    notifySelectionChanged();
}

void ArticulationMapEditorModel::notifySelectionChanged()
{
    if (!m_rows.empty()) {
        emit dataChanged(index(0), index(static_cast<int>(m_rows.size()) - 1), { IsSelectedRole });
    }
    emit selectionChanged();
}

bool ArticulationMapEditorModel::isSelected(const Node* node) const
{
    return std::find(m_selectedNodes.cbegin(), m_selectedNodes.cend(), node) != m_selectedNodes.cend();
}

void ArticulationMapEditorModel::selectRow(int row, bool toggle, bool extend)
{
    const Node* node = nodeAt(row);
    if (!node) {
        return;
    }

    const int anchorRow = rowOf(m_selectionAnchor);
    if (extend && anchorRow >= 0) {
        std::vector<const Node*> nodes = toggle ? m_selectedNodes : std::vector<const Node*> {};
        for (int r = std::min(anchorRow, row); r <= std::max(anchorRow, row); ++r) {
            if (std::find(nodes.cbegin(), nodes.cend(), nodeAt(r)) == nodes.cend()) {
                nodes.push_back(nodeAt(r));
            }
        }

        const Node* anchor = m_selectionAnchor;
        setSelection(nodes, node);
        m_selectionAnchor = anchor; // the next Shift+click starts from the same row
        return;
    }

    if (!toggle) {
        selectNode(node);
        return;
    }

    std::vector<const Node*> nodes = m_selectedNodes;
    if (isSelected(node)) {
        std::erase(nodes, node);
        setSelection(nodes, node == m_selectedNode ? (nodes.empty() ? nullptr : nodes.back()) : m_selectedNode);
    } else {
        nodes.push_back(node);
        setSelection(nodes, node);
    }
}

void ArticulationMapEditorModel::selectAll()
{
    std::vector<const Node*> nodes;
    for (const Row& row : m_rows) {
        nodes.push_back(row.node);
    }

    const Node* current = m_selectedNode ? m_selectedNode : (nodes.empty() ? nullptr : nodes.front());
    setSelection(nodes, current);
}

int ArticulationMapEditorModel::selectionCount() const
{
    return static_cast<int>(m_selectedNodes.size());
}

int ArticulationMapEditorModel::selectedArticulationCount() const
{
    return static_cast<int>(selectedEntries().size());
}

ArticulationMapEditorModel::Node* ArticulationMapEditorModel::selectedEntry() const
{
    Node* node = nodeAt(rowOf(m_selectedNode));
    if (node && !node->isFolder) {
        return node;
    }

    const std::vector<Node*> entries = selectedEntries();
    return entries.empty() ? nullptr : entries.front();
}

std::vector<ArticulationMapEditorModel::Node*> ArticulationMapEditorModel::selectedEntries() const
{
    std::vector<Node*> result;
    for (const Row& row : m_rows) {
        if (!row.node->isFolder && isSelected(row.node)) {
            result.push_back(row.node);
        }
    }

    return result;
}

std::vector<ArticulationMapEditorModel::Node*> ArticulationMapEditorModel::selectedTopNodes() const
{
    std::vector<Node*> result;
    for (const Row& row : m_rows) {
        if (!isSelected(row.node)) {
            continue;
        }

        bool inSelectedFolder = false;
        for (const Node* parent = row.node->parent; parent; parent = parent->parent) {
            if (isSelected(parent)) {
                inSelectedFolder = true;
                break;
            }
        }

        if (!inSelectedFolder) {
            result.push_back(row.node);
        }
    }

    return result;
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

bool ArticulationMapEditorModel::selectedIsDisabledMixed() const
{
    const std::vector<Node*> entries = selectedEntries();
    return std::any_of(entries.cbegin(), entries.cend(), [&entries](const Node* entry) {
        return entry->disabled != entries.front()->disabled;
    });
}

bool ArticulationMapEditorModel::selectedHasKeyswitchOffset() const
{
    const Node* entry = selectedEntry();
    return entry && entry->keyswitchOffsetMs.has_value();
}

bool ArticulationMapEditorModel::selectedHasKeyswitchOffsetMixed() const
{
    const std::vector<Node*> entries = selectedEntries();
    return std::any_of(entries.cbegin(), entries.cend(), [&entries](const Node* entry) {
        return entry->keyswitchOffsetMs.has_value() != entries.front()->keyswitchOffsetMs.has_value();
    });
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

QString ArticulationMapEditorModel::selectedScoreArticulationsText() const
{
    const std::vector<Node*> entries = selectedEntries();
    if (entries.empty()) {
        return QString();
    }

    const std::vector<mpe::ArticulationType> types = scoreArticulationsOf(entries.front());
    for (const Node* entry : entries) {
        if (scoreArticulationsOf(entry) != types) {
            //: Shown when the selected articulations are selected by different score markings
            return muse::qtrc("notation", "Various");
        }
    }

    if (types.empty()) {
        //: Shown when no score marking selects an articulation of the map
        return muse::qtrc("notation", "None");
    }

    QStringList names;
    for (const mpe::ArticulationType type : types) {
        names << ArticulationMapParser::scoreArticulationName(type).toQString();
    }

    return names.join(", ");
}

QVariantMap ArticulationMapEditorModel::selectedScoreArticulationStates() const
{
    QVariantMap result;

    const std::vector<Node*> entries = selectedEntries();
    for (const Node* entry : entries) {
        for (const mpe::ArticulationType type : scoreArticulationsOf(entry)) {
            const QString name = ArticulationMapParser::scoreArticulationName(type).toQString();
            result[name] = result.value(name).toInt() + 1;
        }
    }

    for (auto it = result.begin(); it != result.end(); ++it) {
        it.value() = it.value().toInt() == static_cast<int>(entries.size()) ? 1 : 2;
    }

    return result;
}

//! NOTE: the ones a score has (see ARTICULATION_MAP_REFERENCE.md), not every name the maps accept, with the SMuFL
//! glyph of their symbol in the score (0: a playing technique text, or a line without a symbol of its own)
QVariantList ArticulationMapEditorModel::scoreArticulationGroups() const
{
    struct Item {
        const char* name;
        char32_t glyph;
    };

    static const std::vector<std::pair<const char*, std::vector<Item> > > GROUPS {
        { QT_TRANSLATE_NOOP("notation", "Articulations"), {
              { "Staccato", 0xE4A2 }, { "Staccatissimo", 0xE4A6 }, { "Tenuto", 0xE4A4 }, { "Accent", 0xE4A0 },
              { "Marcato", 0xE4AC }, { "SoftAccent", 0xED40 }, { "LaissezVibrer", 0xE4BA }, { "Subito", 0xE539 } } },
        { QT_TRANSLATE_NOOP("notation", "Strings, brass, guitar"), {
              { "Pizzicato", 0xE633 }, { "SnapPizzicato", 0xE631 }, { "ColLegno", 0 }, { "SulPonticello", 0 },
              { "SulTasto", 0 }, { "Detache", 0 }, { "Martele", 0 }, { "Jete", 0xE620 }, { "DownBow", 0xE610 },
              { "UpBow", 0xE612 }, { "Harmonic", 0xE614 }, { "Mute", 0xE5E5 }, { "Open", 0xE5E7 },
              { "Fall", 0xE5DB }, { "QuickFall", 0xE5D8 }, { "Doit", 0xE5D5 }, { "Plop", 0xE5E0 },
              { "Scoop", 0xE5D0 }, { "BrassBend", 0xE5E3 }, { "Multibend", 0 }, { "FadeIn", 0xE843 },
              { "FadeOut", 0xE844 }, { "Slap", 0 }, { "Pop", 0 }, { "LeftHandTapping", 0xE840 },
              { "RightHandTapping", 0xE841 }, { "Distortion", 0 }, { "Overdrive", 0 }, { "JazzTone", 0 } } },
        { QT_TRANSLATE_NOOP("notation", "Lines"), {
              { "Legato", 0 }, { "Pedal", 0xE650 }, { "PalmMute", 0 }, { "Vibrato", 0 }, { "WideVibrato", 0xEAB1 },
              { "DiscreteGlissando", 0 }, { "ContinuousGlissando", 0 } } },
        { QT_TRANSLATE_NOOP("notation", "Ornaments"), {
              { "Trill", 0xE566 }, { "TrillBaroque", 0 }, { "ShortTrill", 0xE56C }, { "UpperMordentBaroque", 0 },
              { "Mordent", 0xE56D }, { "PrallMordent", 0 }, { "UpMordent", 0 }, { "DownMordent", 0 },
              { "PrallUp", 0 }, { "PrallDown", 0 }, { "UpPrall", 0 }, { "LinePrall", 0 }, { "Tremblement", 0xE56E },
              { "Turn", 0xE567 }, { "InvertedTurn", 0xE568 } } },
        { QT_TRANSLATE_NOOP("notation", "Tremolos, arpeggios, grace notes"), {
              { "Tremolo8th", 0xE220 }, { "Tremolo16th", 0xE221 }, { "Tremolo32nd", 0xE222 },
              { "Tremolo64th", 0xE223 }, { "TremoloBuzz", 0xE22A }, { "Arpeggio", 0xE63C }, { "ArpeggioUp", 0xE634 },
              { "ArpeggioDown", 0xE635 }, { "ArpeggioStraightUp", 0 }, { "ArpeggioStraightDown", 0 },
              { "Acciaccatura", 0xE560 }, { "PreAppoggiatura", 0xE562 }, { "PostAppoggiatura", 0 },
              { "Breath", 0xE4CE } } },
        { QT_TRANSLATE_NOOP("notation", "Handbells"), {
              { "ThumbDamp", 0 }, { "BrushDamp", 0 }, { "RingTouch", 0 }, { "Pluck", 0 }, { "PluckLift", 0xE817 },
              { "SingingBell", 0 }, { "SingingVibrate", 0 }, { "MalletBellOnTable", 0xE815 },
              { "MalletBellSuspended", 0xE814 }, { "MalletLift", 0xE816 }, { "Gyro", 0xE81D },
              { "Martellato", 0xE810 }, { "MartellatoLift", 0xE811 }, { "HandMartellato", 0xE812 },
              { "MutedMartellato", 0xE813 }, { "Swing", 0xE81A }, { "Echo", 0xE81B }, { "Ring", 0 } } },
    };

    //! NOTE: the UI's music font (Leland) lacks many symbols, which the system would draw from an unrelated font
    static const QString FALLBACK_FONT_FAMILY("Bravura");
    const QString musicFontFamily = QString::fromStdString(uiConfiguration()->musicalFontFamily());
    const QRawFont musicFont = QRawFont::fromFont(QFont(musicFontFamily));
    const QRawFont fallbackFont = QRawFont::fromFont(QFont(FALLBACK_FONT_FAMILY));

    QVariantList result;
    for (const auto& [title, items] : GROUPS) {
        QVariantList groupItems;
        for (const Item& item : items) {
            // the name as the maps spell it
            const std::optional<mpe::ArticulationType> type = ArticulationMapParser::scoreArticulationType(String::fromAscii(item.name));
            if (type) {
                QVariantMap groupItem;
                groupItem["name"] = ArticulationMapParser::scoreArticulationName(*type).toQString();
                const bool inMusicFont = item.glyph && musicFont.supportsCharacter(item.glyph);
                const bool inFallbackFont = item.glyph && !inMusicFont && fallbackFont.supportsCharacter(item.glyph);
                groupItem["glyph"] = inMusicFont || inFallbackFont ? QString::fromUcs4(&item.glyph, 1) : QString();
                groupItem["glyphFont"] = inFallbackFont ? FALLBACK_FONT_FAMILY : musicFontFamily;
                groupItems << groupItem;
            }
        }

        QVariantMap group;
        group["title"] = muse::qtrc("notation", title);
        group["items"] = groupItems;
        result << group;
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
    const std::vector<Node*> entries = selectedEntries();
    if (entries.empty()) {
        return;
    }

    // per folder, in display order
    std::vector<std::pair<Node*, std::vector<Node*> > > groups;
    for (Node* entry : entries) {
        auto it = std::find_if(groups.begin(), groups.end(), [entry](const auto& group) { return group.first == entry->parent; });
        if (it == groups.end()) {
            groups.push_back({ entry->parent, { entry } });
        } else {
            it->second.push_back(entry);
        }
    }

    std::vector<const Node*> copies;
    for (const auto& [parent, groupEntries] : groups) {
        size_t index = 0;
        for (const Node* entry : groupEntries) {
            index = std::max(index, parent->indexOf(entry) + 1);
        }

        for (const Node* entry : groupEntries) {
            std::unique_ptr<Node> copy = copyOfArticulation(*entry);
            copy->name = uniqueName(parent, entry->name);
            // what selects it, even when it came from the name ("Staccato" copied as "Staccato 2")
            setScoreArticulations(copy.get(), scoreArticulationsOf(entry));
            copies.push_back(parent->addChild(std::move(copy), index++));
        }
    }

    m_selectedNodes = copies;
    m_selectedNode = copies.front();
    m_selectionAnchor = copies.front();
    rebuildRows();
    markDirty();
}

void ArticulationMapEditorModel::removeSelected()
{
    const std::vector<Node*> nodes = selectedTopNodes();
    if (nodes.empty()) {
        return;
    }

    auto doRemove = [this, nodes]() {
        // the first one's parent stays: it isn't selected, nor in a selected folder
        Node* parent = nodes.front()->parent;
        const size_t index = parent->indexOf(nodes.front());

        for (Node* node : nodes) {
            if (m_defaultNode && node->contains(m_defaultNode)) {
                m_defaultNode = nullptr;
            }

            std::unique_ptr<Node> removed = node->parent->takeChild(node);
        }

        // select what takes the place of the first one
        if (index < parent->children.size()) {
            m_selectedNode = parent->children[index].get();
        } else if (index > 0) {
            m_selectedNode = parent->children[index - 1].get();
        } else {
            m_selectedNode = parent == m_root.get() ? nullptr : parent;
        }
        m_selectedNodes.clear();
        m_selectionAnchor = nullptr;

        rebuildRows();
        markDirty();
    };

    const Node* first = nodes.front();
    if (nodes.size() == 1 && (!first->isFolder || first->children.empty())) {
        doRemove();
        return;
    }

    const QString question = nodes.size() == 1
                             ? muse::qtrc("notation", "Remove the folder “%1” and everything in it?").arg(first->name)
                             : muse::qtrc("notation", "Remove the %n selected item(s)?", nullptr, static_cast<int>(nodes.size()));

    interactive()->question("", question.toStdString(), {
        IInteractive::Button::Cancel, IInteractive::Button::Yes
    }, IInteractive::Button::Yes)
    .onResolve(this, [doRemove](const IInteractive::Result& res) {
        if (res.isButton(IInteractive::Button::Yes)) {
            doRemove();
        }
    });
}

std::vector<ArticulationMapEditorModel::Node*> ArticulationMapEditorModel::nodesToMove(int fromRow) const
{
    Node* from = nodeAt(fromRow);
    if (!from) {
        return {};
    }

    return isSelected(from) ? selectedTopNodes() : std::vector<Node*> { from };
}

//! NOTE: toRow -1 = at the end of the map, outside of any folder
bool ArticulationMapEditorModel::canMove(int fromRow, int toRow, DropPosition position) const
{
    const std::vector<Node*> nodes = nodesToMove(fromRow);
    if (toRow < 0) {
        return !nodes.empty() && position == DropPosition::After;
    }

    const Node* to = nodeAt(toRow);
    if (nodes.empty() || !to) {
        return false;
    }

    // not next to itself, and a folder can't go into itself
    for (const Node* node : nodes) {
        if (node->contains(to)) {
            return false;
        }
    }

    return position != DropPosition::Into || to->isFolder;
}

void ArticulationMapEditorModel::move(int fromRow, int toRow, DropPosition position)
{
    if (!canMove(fromRow, toRow, position)) {
        return;
    }

    const std::vector<Node*> nodes = nodesToMove(fromRow);
    Node* to = toRow >= 0 ? nodeAt(toRow) : nullptr;

    std::vector<std::unique_ptr<Node> > taken;
    for (Node* node : nodes) {
        taken.push_back(node->parent->takeChild(node));
    }

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

    for (std::unique_ptr<Node>& node : taken) {
        node->name = uniqueName(parent, node->name);
        parent->addChild(std::move(node), index++);
    }

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
    notifyAllRowsChanged(); // its score markings may come from its name, and the rows below show theirs against them
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

void ArticulationMapEditorModel::setSelectedColor(const QColor& color)
{
    if (!color.isValid()) {
        return;
    }

    bool changed = false;
    for (Node* entry : selectedEntries()) {
        if (entry->color != (color.rgb() & 0xFFFFFF)) {
            entry->color = color.rgb() & 0xFFFFFF;
            changed = true;
        }
    }

    if (changed) {
        notifyAllRowsChanged();
        markDirty();
    }
}

QVariantList ArticulationMapEditorModel::colorGradientPreviews() const
{
    static constexpr int SAMPLE_COUNT = 10;

    QVariantList result;
    for (const std::vector<QColor>& stops : artMapColorGradients()) {
        QVariantList samples;
        for (int i = 0; i < SAMPLE_COUNT; ++i) {
            samples << artMapGradientColor(stops, static_cast<double>(i) / (SAMPLE_COUNT - 1));
        }
        result << QVariant(samples);
    }

    return result;
}

void ArticulationMapEditorModel::applyColorGradient(int gradientIndex)
{
    const std::vector<std::vector<QColor> >& gradients = artMapColorGradients();
    if (gradientIndex < 0 || gradientIndex >= static_cast<int>(gradients.size())) {
        return;
    }

    std::vector<Node*> entries = selectedEntries();
    if (entries.size() < 2) {
        // every articulation, in collapsed folders too
        entries.clear();
        std::function<void(Node*)> visit = [&](Node* parent) {
            for (const auto& child : parent->children) {
                if (child->isFolder) {
                    visit(child.get());
                } else {
                    entries.push_back(child.get());
                }
            }
        };
        visit(m_root.get());
    }

    if (entries.empty()) {
        return;
    }

    for (size_t i = 0; i < entries.size(); ++i) {
        const double t = entries.size() > 1 ? static_cast<double>(i) / (entries.size() - 1) : 0.0;
        entries[i]->color = artMapGradientColor(gradients[gradientIndex], t).rgb() & 0xFFFFFF;
    }

    notifyAllRowsChanged();
    markDirty();
}

void ArticulationMapEditorModel::resetSelectedColor()
{
    bool changed = false;
    for (Node* entry : selectedEntries()) {
        if (entry->color) {
            entry->color.reset();
            changed = true;
        }
    }

    if (changed) {
        notifyAllRowsChanged();
        markDirty();
    }
}

void ArticulationMapEditorModel::setSelectedIsDisabled(bool isDisabled)
{
    bool changed = false;
    for (Node* entry : selectedEntries()) {
        if (entry->disabled == isDisabled) {
            continue;
        }

        entry->disabled = isDisabled;
        changed = true;

        // a disabled articulation is never played, it can't be the one played by default
        if (isDisabled && entry == m_defaultNode) {
            m_defaultNode = nullptr;
        }
    }

    if (changed) {
        notifyAllRowsChanged();
        markDirty();
        emit selectionChanged();
    }
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
    // the value shown (the current articulation's, else the map's) for the ones that get one
    const int ms = selectedKeyswitchOffsetMs();

    bool changed = false;
    for (Node* entry : selectedEntries()) {
        if (has == entry->keyswitchOffsetMs.has_value()) {
            continue;
        }

        if (has) {
            entry->keyswitchOffsetMs = ms;
        } else {
            entry->keyswitchOffsetMs.reset();
        }
        changed = true;
    }

    if (changed) {
        markDirty();
        emit selectionChanged();
    }
}

void ArticulationMapEditorModel::setSelectedKeyswitchOffsetMs(int ms)
{
    bool changed = false;
    for (Node* entry : selectedEntries()) {
        if (entry->keyswitchOffsetMs != ms) {
            entry->keyswitchOffsetMs = ms;
            changed = true;
        }
    }

    if (changed) {
        markDirty();
        emit selectionChanged();
    }
}

void ArticulationMapEditorModel::setSelectedChannel(int channel)
{
    const std::optional<int> newChannel = channel >= 1 && channel <= ArticulationMapParser::MIDI_CHANNEL_COUNT
                                          ? std::optional<int>(channel - 1) : std::nullopt;

    bool changed = false;
    for (Node* entry : selectedEntries()) {
        if (entry->channel != newChannel) {
            entry->channel = newChannel;
            changed = true;
        }
    }

    if (changed) {
        notifyAllRowsChanged();
        markDirty();
        emit selectionChanged();
    }
}

void ArticulationMapEditorModel::setSelectedScoreArticulation(const QString& name, bool selects)
{
    const std::optional<mpe::ArticulationType> type = ArticulationMapParser::scoreArticulationType(String::fromQString(name));
    if (!type) {
        return;
    }

    bool changed = false;
    for (Node* entry : selectedEntries()) {
        std::vector<mpe::ArticulationType> types = scoreArticulationsOf(entry);
        const bool has = std::find(types.cbegin(), types.cend(), *type) != types.cend();
        if (has == selects) {
            continue;
        }

        if (selects) {
            types.push_back(*type);
        } else {
            std::erase(types, *type);
        }

        setScoreArticulations(entry, types);
        changed = true;
    }

    if (changed) {
        notifyAllRowsChanged();
        markDirty();
        emit selectionChanged();
    }
}

void ArticulationMapEditorModel::setSelectedNotesOffsetMs(int ms)
{
    bool changed = false;
    for (Node* entry : selectedEntries()) {
        if (entry->notesOffsetMs != ms) {
            entry->notesOffsetMs = ms;
            changed = true;
        }
    }

    if (changed) {
        markDirty();
        emit selectionChanged();
    }
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
