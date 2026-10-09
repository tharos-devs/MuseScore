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

#include "trackpresets.h"

#include <algorithm>

#include <QDateTime>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

#include "engraving/articulationmap/articulationmapparser.h"
#include "engraving/dom/instrtemplate.h"
#include "engraving/dom/instrument.h"
#include "engraving/dom/part.h"
#include "notation/imasternotation.h"
#include "notation/inotationarticulationmaps.h"
#include "notation/inotationparts.h"
#include "project/inotationproject.h"
#include "global/io/fileinfo.h"
#include "audio/common/audioutils.h"

#include "log.h"
#include "translation.h"

using namespace mu::playback;
using namespace mu::engraving;
using namespace muse;
using namespace muse::audio;

static const QString PRESETS_DIR_NAME("/TrackPresets");
static const QString PRESET_SUFFIX("trackpreset");
static constexpr int PRESET_FORMAT_VERSION = 1;

//! NOTE: an unused slot of a track's aux sends (see MixerChannelItem::blankAuxSendParams())
static const AuxSendParams BLANK_AUX_SEND { -1.f, false };

TrackPresets::TrackPresets(const modularity::ContextPtr& iocCtx)
    : Contextable(iocCtx)
{
}

mu::project::IProjectAudioSettingsPtr TrackPresets::audioSettings() const
{
    const mu::project::INotationProjectPtr project = globalContext()->currentProject();
    return project ? project->audioSettings() : nullptr;
}

io::path_t TrackPresets::presetsDir() const
{
    const io::path_t dir = globalConfiguration()->userDataPath() + PRESETS_DIR_NAME;
    if (!fileSystem()->exists(dir)) {
        fileSystem()->makePath(dir);
    }

    return dir;
}

QString TrackPresets::filePathFor(const QString& name, const QString& pluginName) const
{
    QString fileName = pluginName.isEmpty() ? name : name + " (" + pluginName + ")";
    static const QString FORBIDDEN("/\\:*?\"<>|");
    for (const QChar c : FORBIDDEN) {
        fileName.replace(c, '_');
    }

    return presetsDir().toQString() + "/" + fileName.trimmed() + "." + PRESET_SUFFIX;
}

QString TrackPresets::pluginNameOf(const mu::project::AudioInputParams& input)
{
    switch (input.type()) {
    case AudioSourceType::MuseSampler: return QStringLiteral("MuseSounds");
    case AudioSourceType::Vsti:
    case AudioSourceType::Fluid:
        return QString::fromStdString(input.resourceMeta.id);
    default:
        break;
    }

    return QString();
}

QString TrackPresets::soundNameOf(const mu::project::AudioInputParams& input)
{
    if (input.type() == AudioSourceType::MuseSampler) {
        QStringList parts { input.resourceMeta.attributeVal(u"museName").toQString(),
                            input.resourceMeta.attributeVal(u"museVendorName").toQString() };
        parts.removeAll(QString());
        return parts.join(" · ");
    }

    return audioSourceName(input).toQString();
}

//! NOTE: the name the Mixer shows for the bus, to find it again by name in another project
QString TrackPresets::auxBusName(aux_channel_idx_t index) const
{
    const mu::project::IProjectAudioSettingsPtr settings = audioSettings();
    const String customName = settings ? settings->auxName(index) : String();
    if (!customName.empty()) {
        return customName.toQString();
    }

    return QString::fromStdString(playbackController()->auxChannelName(index));
}

// ---- files

static constexpr int INDEX_FORMAT_VERSION = 1;

io::path_t TrackPresets::indexPath() const
{
    return presetsDir() + "/index.json";
}

//! NOTE: the infos, as written at the top of a preset file and in the index
static QJsonObject infoToJson(const TrackPresetInfo& info)
{
    QJsonObject result;
    result.insert("name", info.name);
    result.insert("tags", QJsonArray::fromStringList(info.tags));

    QJsonObject instrument;
    instrument.insert("id", info.instrumentId);
    instrument.insert("name", info.instrumentName);
    instrument.insert("family", info.familyName);
    result.insert("instrument", instrument);

    result.insert("plugin", info.pluginName);
    result.insert("pluginId", QString::fromStdString(info.pluginResourceId));
    result.insert("isVstPlugin", info.isVstPlugin);

    return result;
}

TrackPresetInfo TrackPresets::infoFromJson(const QJsonObject& root)
{
    TrackPresetInfo info;
    info.name = root.value("name").toString();
    for (const QJsonValue tag : root.value("tags").toArray()) {
        info.tags << tag.toString();
    }

    const QJsonObject instrument = root.value("instrument").toObject();
    info.instrumentId = instrument.value("id").toString();
    info.instrumentName = instrument.value("name").toString();
    info.familyName = instrument.value("family").toString();

    info.pluginName = root.value("plugin").toString();
    info.pluginResourceId = root.value("pluginId").toString().toStdString();
    info.isVstPlugin = root.value("isVstPlugin").toBool();

    return info;
}

std::vector<TrackPresetInfo> TrackPresets::loadAll() const
{
    std::vector<TrackPresetInfo> result;

    const RetVal<io::paths_t> paths = fileSystem()->scanFiles(presetsDir(), { "*." + PRESET_SUFFIX.toStdString() },
                                                              io::ScanMode::FilesInCurrentDir);
    if (!paths.ret) {
        return result;
    }

    QJsonObject index;
    if (const RetVal<ByteArray> data = fileSystem()->readFile(indexPath()); data.ret) {
        const QJsonObject root = QJsonDocument::fromJson(data.val.toQByteArrayNoCopy()).object();
        if (root.value("version").toInt() == INDEX_FORMAT_VERSION) {
            index = root.value("presets").toObject();
        }
    }

    QJsonObject newIndex;
    bool indexChanged = false;

    for (const io::path_t& path : paths.val) {
        const QFileInfo file(path.toQString());
        const qint64 size = file.size();
        const qint64 modified = file.lastModified().toMSecsSinceEpoch();

        //! NOTE: an unchanged file's infos are the index's; only a new or changed one is read
        TrackPresetInfo info;
        QJsonObject entry = index.value(file.fileName()).toObject();
        if (!entry.isEmpty() && entry.value("size").toInteger() == size && entry.value("modified").toInteger() == modified) {
            info = infoFromJson(entry.value("info").toObject());
        } else {
            const std::optional<TrackPreset> preset = readPreset(path.toQString());
            if (!preset) {
                continue;
            }
            info = *preset;

            entry = QJsonObject();
            entry.insert("size", size);
            entry.insert("modified", modified);
            entry.insert("info", infoToJson(info));
            indexChanged = true;
        }

        info.filePath = path.toQString();
        info.modified = modified;
        newIndex.insert(file.fileName(), entry);
        result.push_back(std::move(info));
    }

    // a preset removed since
    if (indexChanged || newIndex.size() != index.size()) {
        QJsonObject root;
        root.insert("version", INDEX_FORMAT_VERSION);
        root.insert("presets", newIndex);
        fileSystem()->writeFile(indexPath(), ByteArray::fromQByteArrayNoCopy(QJsonDocument(root).toJson(QJsonDocument::Compact)));
    }

    std::sort(result.begin(), result.end(), [](const TrackPresetInfo& a, const TrackPresetInfo& b) {
        return a.name.compare(b.name, Qt::CaseInsensitive) < 0;
    });

    return result;
}

std::optional<TrackPreset> TrackPresets::readPreset(const QString& filePath) const
{
    const mu::project::IProjectAudioSettingsPtr settings = audioSettings();
    const RetVal<ByteArray> data = fileSystem()->readFile(io::path_t(filePath));
    if (!settings || !data.ret) {
        return std::nullopt;
    }

    const QJsonDocument document = QJsonDocument::fromJson(data.val.toQByteArrayNoCopy());
    if (!document.isObject()) {
        LOGW() << "Not a track preset: " << filePath;
        return std::nullopt;
    }

    const QJsonObject root = document.object();

    TrackPreset preset;
    static_cast<TrackPresetInfo&>(preset) = infoFromJson(root);
    preset.filePath = filePath;
    preset.modified = QFileInfo(filePath).lastModified().toMSecsSinceEpoch();

    preset.input = settings->inputParamsFromJson(root.value("in").toObject());
    preset.output = settings->outputParamsFromJson(root.value("out").toObject());

    // a preset written before these were
    if (preset.pluginResourceId.empty()) {
        preset.pluginResourceId = preset.input.resourceMeta.id;
        preset.isVstPlugin = isResourceType(preset.input.resourceMeta, AudioResourceType::VstPlugin);
    }

    for (const QJsonValue value : root.value("auxSends").toArray()) {
        const QJsonObject send = value.toObject();
        preset.auxSends.push_back({ send.value("bus").toString(), send.value("isGroupBus").toBool(),
                                    static_cast<float>(send.value("signalAmount").toDouble()), send.value("active").toBool() });
    }

    if (root.contains("articulationMap")) {
        const QJsonObject map = root.value("articulationMap").toObject();
        preset.hasArticulationMap = true;
        preset.articulationMapPath = map.value("path").toString();
        preset.articulationMapText = map.value("text").toString();
    }

    if (preset.name.isEmpty()) {
        preset.name = io::completeBasename(io::path_t(filePath)).toQString();
    }

    return preset;
}

Ret TrackPresets::write(const TrackPreset& preset) const
{
    const mu::project::IProjectAudioSettingsPtr settings = audioSettings();
    if (!settings) {
        return make_ret(Ret::Code::InternalError);
    }

    QJsonObject root = infoToJson(preset);
    root.insert("version", PRESET_FORMAT_VERSION);

    root.insert("in", settings->inputParamsToJson(preset.input));
    root.insert("out", settings->outputParamsToJson(preset.output));

    QJsonArray auxSends;
    for (const TrackPreset::AuxSend& send : preset.auxSends) {
        QJsonObject object;
        object.insert("bus", send.busName);
        object.insert("isGroupBus", send.isGroupBus);
        object.insert("signalAmount", send.signalAmount);
        object.insert("active", send.active);
        auxSends.append(object);
    }
    root.insert("auxSends", auxSends);

    if (preset.hasArticulationMap) {
        QJsonObject map;
        map.insert("path", preset.articulationMapPath);
        map.insert("text", preset.articulationMapText);
        root.insert("articulationMap", map);
    }

    const QByteArray json = QJsonDocument(root).toJson(QJsonDocument::Indented);
    return fileSystem()->writeFile(io::path_t(preset.filePath), ByteArray::fromQByteArrayNoCopy(json));
}

muse::async::Notification TrackPresets::presetsChanged()
{
    static muse::async::Notification notification;
    return notification;
}

Ret TrackPresets::save(TrackPreset& preset) const
{
    preset.filePath = filePathFor(preset.name, preset.pluginName);
    const Ret ret = write(preset);
    if (ret) {
        presetsChanged().notify();
    }
    return ret;
}

Ret TrackPresets::update(const TrackPresetInfo& info) const
{
    //! NOTE: the whole file is rewritten: its snapshot read back first
    std::optional<TrackPreset> preset = readPreset(info.filePath);
    if (!preset) {
        return make_ret(Ret::Code::InternalError);
    }
    preset->name = info.name;
    preset->tags = info.tags;

    const QString newPath = filePathFor(preset->name, preset->pluginName);

    //! NOTE: never onto another preset (same name for the same plugin)
    if (newPath.compare(preset->filePath, Qt::CaseInsensitive) != 0 && QFile::exists(newPath)) {
        return make_ret(Ret::Code::UnknownError,
                        muse::trc("playback", "A track preset with this name already exists for this plugin"));
    }

    Ret ret;
    //! NOTE: case only (e.g. "flute" to "Flute"): the same file on a case-insensitive file system (macOS, Windows),
    //! which writing the new one then removing the old one would delete - renamed instead
    if (newPath.compare(preset->filePath, Qt::CaseInsensitive) == 0) {
        ret = write(*preset);
        if (ret && newPath != preset->filePath && !QFile::rename(preset->filePath, newPath)) {
            ret = make_ret(Ret::Code::UnknownError, muse::trc("playback", "Cannot rename the track preset"));
        }
    } else {
        const QString oldPath = preset->filePath;
        preset->filePath = newPath;
        ret = write(*preset);
        if (ret) {
            fileSystem()->remove(io::path_t(oldPath));
        }
    }

    if (ret) {
        presetsChanged().notify();
    }
    return ret;
}

Ret TrackPresets::remove(const TrackPresetInfo& preset) const
{
    const Ret ret = fileSystem()->remove(io::path_t(preset.filePath));
    if (ret) {
        presetsChanged().notify();
    }
    return ret;
}

QStringList TrackPresets::lastTagsForPlugin(const QString& pluginName) const
{
    QStringList tags;
    qint64 lastModified = -1;

    for (const TrackPresetInfo& preset : loadAll()) {
        if (preset.pluginName == pluginName && !preset.tags.isEmpty() && preset.modified > lastModified) {
            lastModified = preset.modified;
            tags = preset.tags;
        }
    }

    return tags;
}

bool TrackPresets::isAvailable(const TrackPresetInfo& preset) const
{
    return !preset.isVstPlugin || knownAudioPlugins()->exists(preset.pluginResourceId);
}

// ---- snapshot

std::optional<TrackPreset> TrackPresets::capture(const InstrumentTrackId& trackId) const
{
    const mu::project::IProjectAudioSettingsPtr settings = audioSettings();
    const mu::notation::IMasterNotationPtr masterNotation = globalContext()->currentMasterNotation();
    if (!settings || !masterNotation) {
        return std::nullopt;
    }

    const Part* part = masterNotation->parts()->part(trackId.partId);
    const Instrument* instrument = part ? part->instrumentById(trackId.instrumentId) : nullptr;
    if (!instrument) {
        return std::nullopt;
    }

    TrackPreset preset;
    preset.instrumentId = instrument->id().toQString();
    preset.instrumentName = instrument->trackName().toQString();
    for (const InstrumentGroup* group : instrumentGroups) {
        if (group->id == instrument->group()) {
            preset.familyName = group->name.toQString();
            break;
        }
    }
    preset.name = preset.instrumentName;

    preset.input = settings->trackInputParams(trackId);
    preset.input.midiPortNames.clear(); // the plugin's, filled in once loaded
    preset.pluginName = pluginNameOf(preset.input);
    preset.pluginResourceId = preset.input.resourceMeta.id;
    preset.isVstPlugin = isResourceType(preset.input.resourceMeta, AudioResourceType::VstPlugin);

    preset.output = settings->trackOutputParams(trackId);
    preset.output.solo = false;
    preset.output.muted = false;
    preset.output.forceMute = false;

    //! NOTE: the plugins' states as they are now, not as last saved: a plugin may change without reporting it
    //! (e.g. a patch loaded in Kontakt), see PlaybackController::refreshAudioPluginStates()
    const IPlaybackController::InstrumentTrackIdMap& trackIds = playbackController()->instrumentTrackIdMap();
    auto it = trackIds.find(trackId);
#ifdef MUSE_MODULE_VST
    if (it != trackIds.end() && vstPluginStateProvider()) {
        if (preset.input.type() == AudioSourceType::Vsti) {
            if (const std::optional<AudioUnitConfig> state
                    = vstPluginStateProvider()->instrumentPluginState(preset.input.resourceMeta.id, it->second)) {
                preset.input.configuration = *state;
            }
        }

        for (auto& [chainOrder, fxParams] : preset.output.fxChain) {
            if (!isResourceType(fxParams.resourceMeta, AudioResourceType::VstPlugin)) {
                continue;
            }
            if (const std::optional<AudioUnitConfig> state
                    = vstPluginStateProvider()->fxPluginState(fxParams.resourceMeta.id, it->second, chainOrder)) {
                fxParams.configuration = *state;
            }
        }
    }
#else
    UNUSED(it);
#endif

    //! NOTE: by bus name: the bus indices of another project are unrelated
    const IPlaybackController::AuxTrackIdMap& auxTrackIds = playbackController()->auxTrackIdMap();
    for (size_t index = 0; index < preset.output.auxSends.size(); ++index) {
        const AuxSendParams& send = preset.output.auxSends.at(index);
        const aux_channel_idx_t busIndex = static_cast<aux_channel_idx_t>(index);
        if (send.signalAmount < 0.f || auxTrackIds.find(busIndex) == auxTrackIds.end()) {
            continue;
        }

        preset.auxSends.push_back({ auxBusName(busIndex), settings->isAuxBusGroup(busIndex), send.signalAmount, send.active });
    }
    preset.output.auxSends.clear();

    if (const mu::notation::INotationArticulationMapsPtr maps = masterNotation->articulationMaps()) {
        if (const ExpressionMap* map = maps->data() ? maps->data()->map(trackId) : nullptr) {
            preset.hasArticulationMap = true;
            preset.articulationMapPath = map->sourcePath.toQString();
            preset.articulationMapText = map->sourceText.toQString();
        }
    }

    return preset;
}

void TrackPresets::applySound(const AudioResourceMeta& sound, const InstrumentTrackId& trackId)
{
    const mu::project::IProjectAudioSettingsPtr settings = audioSettings();
    const IPlaybackController::InstrumentTrackIdMap& trackIds = playbackController()->instrumentTrackIdMap();
    auto it = trackIds.find(trackId);
    if (!settings || it == trackIds.end()) {
        return;
    }

    //! NOTE: as the Mixer's Sound menu does (see InputResourceItem::setParamsRecourceMeta())
    AudioInputParams params = settings->trackInputParams(trackId);
    params.resourceMeta = sound;
    params.configuration.clear();
    params.midiPort = 0;
    params.midiChannel = 0;
    params.midiPortNames.clear();

    playback()->setSourceParams(it->second, params);
}

void TrackPresets::apply(const TrackPreset& preset, const InstrumentTrackId& trackId)
{
    const mu::project::IProjectAudioSettingsPtr settings = audioSettings();
    const mu::notation::IMasterNotationPtr masterNotation = globalContext()->currentMasterNotation();
    if (!settings || !masterNotation || !isAvailable(preset)) {
        return;
    }

    const IPlaybackController::InstrumentTrackIdMap& trackIds = playbackController()->instrumentTrackIdMap();
    auto it = trackIds.find(trackId);
    if (it == trackIds.end()) {
        return;
    }
    const TrackId engineTrackId = it->second;

    // the instrument and its patch (the plugin is reloaded with that state), its effects
    playback()->setSourceParams(engineTrackId, preset.input);
    playback()->setFxChainParams(engineTrackId, preset.output.fxChain);

    // the sends to the buses of this project with the same names (and kind), the others are left out
    AuxSendsParams auxSends;
    for (const auto& [busIndex, busTrackId] : playbackController()->auxTrackIdMap()) {
        UNUSED(busTrackId);
        const QString busName = auxBusName(busIndex);
        auto send = std::find_if(preset.auxSends.cbegin(), preset.auxSends.cend(), [&](const TrackPreset::AuxSend& s) {
            return s.busName == busName && s.isGroupBus == settings->isAuxBusGroup(busIndex);
        });
        if (send == preset.auxSends.cend()) {
            continue;
        }

        if (auxSends.size() <= busIndex) {
            auxSends.resize(busIndex + 1, BLANK_AUX_SEND);
        }
        auxSends[busIndex] = { send->signalAmount, send->active };
    }
    playback()->setAuxSendsParams(engineTrackId, auxSends);

    // gain, volume and pan, keeping the track's mute (owned by the solo/mute state)
    playback()->params(engineTrackId).onResolve(this, [this, engineTrackId, preset](const TrackParams& params) {
        ControlParams control = params.control;
        control.volume = preset.output.volume;
        control.balance = preset.output.balance;
        control.gain = preset.output.gain;
        playback()->setControlParams(engineTrackId, control);
    });

    mu::project::AudioOutputParams outParams = settings->trackOutputParams(trackId);
    outParams.volume = preset.output.volume;
    outParams.balance = preset.output.balance;
    outParams.gain = preset.output.gain;
    outParams.color = preset.output.color;
    settings->setTrackOutputParams(trackId, outParams);

    // the articulation map, or none
    const mu::notation::INotationArticulationMapsPtr maps = masterNotation->articulationMaps();
    if (!maps || !maps->data()) {
        return;
    }

    EditArticulationMapChanges changes;
    if (preset.hasArticulationMap) {
        ArticulationMapParser::Result result = ArticulationMapParser::parse(String::fromQString(preset.articulationMapText));
        if (result.map.name.empty()) {
            result.map.name = preset.articulationMapPath.isEmpty()
                              ? String::fromQString(preset.name)
                              : io::completeBasename(io::path_t(preset.articulationMapPath)).toString();
        }
        result.map.sourcePath = String::fromQString(preset.articulationMapPath);

        const ExpressionMap* current = maps->data()->map(trackId);
        if (!current || !(*current == result.map)) {
            changes.maps.emplace(trackId, result.map);
        }
    } else if (maps->data()->map(trackId)) {
        changes.maps.emplace(trackId, std::nullopt);
    }

    if (!changes.empty()) {
        maps->edit(changes, TranslatableString("undoableAction", "Load track preset"));
    }
}
