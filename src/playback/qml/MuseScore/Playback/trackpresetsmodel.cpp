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

#include "trackpresetsmodel.h"

#include <map>

#include "async/notifylist.h"
#include "engraving/dom/instrument.h"
#include "engraving/dom/part.h"
#include "notation/imasternotation.h"
#include "notation/inotationparts.h"
#include "project/inotationproject.h"

#include "audio/common/soundfonttypes.h"

#include "msbasicpresetscategories.h"
#include "savetrackpresetmodel.h"

#include "settings.h"
#include "translation.h"

using namespace mu::playback;
using namespace mu::engraving;
using namespace muse;
using namespace muse::audio;

static const QString SOUND_KEY_PREFIX("sound:");
static const QString SOUNDFONT_KEY_PREFIX("soundfont:");

//! NOTE: kept from one opening of the window to the next: the presets' columns order, the tab, and per tab the selected
//! names in the columns and the selected item
static const Settings::Key GROUP_BY_KEY("playback", "playback/trackPresets/groupBy");
static const Settings::Key SOURCE_KEY("playback", "playback/trackPresets/source");

static Settings::Key navigationKey(int source, const char* name)
{
    return Settings::Key("playback", "playback/trackPresets/" + std::to_string(source) + "/" + name);
}

namespace {
struct CaseInsensitiveLess {
    bool operator()(const QString& a, const QString& b) const
    {
        return a.compare(b, Qt::CaseInsensitive) < 0;
    }
};

QString orOther(const QString& key)
{
    return key.isEmpty() ? muse::qtrc("playback", "Other") : key;
}
}

TrackPresetsModel::TrackPresetsModel(QObject* parent)
    : QObject(parent), muse::Contextable(muse::iocCtxForQmlObject(this))
{
    settings()->setDefaultValue(GROUP_BY_KEY, Val(static_cast<int>(FamilyInstrument)));
    const int groupBy = settings()->value(GROUP_BY_KEY).toInt();
    if (groupBy >= FamilyInstrument && groupBy <= InstrumentPlugin) {
        m_groupBy = groupBy;
    }

    settings()->setDefaultValue(SOURCE_KEY, Val(static_cast<int>(Presets)));
    const int source = settings()->value(SOURCE_KEY).toInt();
    if (source >= Presets && source <= SoundFonts) {
        m_source = source;
    }
    restoreNavigation();
}

void TrackPresetsModel::restoreNavigation()
{
    m_selectedFirst = settings()->value(navigationKey(m_source, "first")).toQString();
    m_selectedSecond = settings()->value(navigationKey(m_source, "second")).toQString();
    m_selectedKey = settings()->value(navigationKey(m_source, "item")).toQString();
}

void TrackPresetsModel::saveNavigation() const
{
    settings()->setSharedValue(SOURCE_KEY, Val(m_source));
    settings()->setSharedValue(navigationKey(m_source, "first"), Val(m_selectedFirst));
    settings()->setSharedValue(navigationKey(m_source, "second"), Val(m_selectedSecond));
    settings()->setSharedValue(navigationKey(m_source, "item"), Val(m_selectedKey));
}

// ---- loading

void TrackPresetsModel::load(const QString& partId, const QString& instrumentId)
{
    m_presets = std::make_unique<TrackPresets>(iocContext());

    reloadInstruments();

    bool ok = false;
    const uint64_t part = partId.toULongLong(&ok);
    if (ok) {
        const InstrumentTrackId trackId { muse::ID(part), String::fromQString(instrumentId) };
        for (size_t i = 0; i < m_instruments.size(); ++i) {
            if (m_instruments[i].trackId == trackId) {
                m_instruments[i].selected = true;
                m_instrumentAnchor = static_cast<int>(i);
            }
        }
    }

    reloadPresets();
    reloadSounds();

    TrackPresets::presetsChanged().onNotify(this, [this]() {
        reloadPresets();
    });

    //! NOTE: the instruments' sounds and colors, changed here or elsewhere
    if (const project::INotationProjectPtr project = globalContext()->currentProject()) {
        project->audioSettings()->settingsChanged().onNotify(this, [this]() {
            updateInstrumentSounds();
        });
        project->audioSettings()->trackInputParamsChanged().onReceive(this, [this](const InstrumentTrackId&) {
            updateInstrumentSounds();
        });
    }

    updateInstrumentSounds();
    emit instrumentCountChanged();
}

void TrackPresetsModel::reloadInstruments()
{
    m_instruments.clear();

    const notation::IMasterNotationPtr masterNotation = globalContext()->currentMasterNotation();
    if (!masterNotation) {
        return;
    }

    //! NOTE: one per part, like the Track list: its first instrument's track (the one the Mixer shows first)
    for (const Part* part : masterNotation->parts()->partList()) {
        if (part->instrument()) {
            ScoreInstrument instrument;
            instrument.trackId = InstrumentTrackId { part->id(), part->instrument()->id() };
            instrument.name = part->partName().toQString();
            m_instruments.push_back(std::move(instrument));
        }
    }
}

void TrackPresetsModel::reloadPresets()
{
    m_presetList = m_presets->loadAll();
    if (m_source == Presets) {
        rebuildColumns();
    }
}

//! NOTE: the ones the Mixer's Sound menu offers (see InputResourceItem::requestAvailableResources())
void TrackPresetsModel::reloadSounds()
{
    playback()->availableInputResources().onResolve(this, [this](const AudioResourceMetaList& resources) {
        m_sounds.clear();
        for (const AudioResourceMeta& meta : resources) {
            if (isResourceType(meta, AudioResourceType::MuseSamplerSoundPack)) {
                m_sounds.push_back(meta);
            }
        }

        std::sort(m_sounds.begin(), m_sounds.end(), [](const AudioResourceMeta& a, const AudioResourceMeta& b) {
            return a.attributeVal(u"museName").toQString().compare(b.attributeVal(u"museName").toQString(), Qt::CaseInsensitive) < 0;
        });

        loadSoundFontPresets(resources);

        if (m_source != Presets) {
            rebuildColumns();
        }
    });
}

//! NOTE: as the Mixer's Sound menu lists them (see InputResourceItem::buildSoundFontsMenuItem()): MS Basic's presets by
//! its categories, the other SoundFonts' by bank, in program order; "Choose automatically" (a preset for each
//! instrument) too
void TrackPresetsModel::loadSoundFontPresets(const AudioResourceMetaList& resources)
{
    using namespace muse::audio::synth;

    m_soundFontPresets.clear();

    const QString automaticCategory = muse::qtrc("playback", "Automatic");
    const QString automaticName = muse::qtrc("playback", "Choose automatically");

    std::map<QString, std::map<muse::midi::Program, AudioResourceMeta>, CaseInsensitiveLess> bySoundFont;
    for (const AudioResourceMeta& meta : resources) {
        if (!isResourceType(meta, AudioResourceType::FluidSoundfont)) {
            continue;
        }

        const QString soundFont = meta.attributeVal(SOUNDFONT_NAME_ATTRIBUTE).toQString();
        bool bankOk = false;
        bool programOk = false;
        const int bank = meta.attributeVal(PRESET_BANK_ATTRIBUTE).toInt(&bankOk);
        const int program = meta.attributeVal(PRESET_PROGRAM_ATTRIBUTE).toInt(&programOk);
        if (bankOk && programOk) {
            bySoundFont[soundFont][muse::midi::Program(bank, program)] = meta;
        } else {
            m_soundFontPresets.push_back({ meta, soundFont, automaticCategory, automaticName });
        }
    }

    auto presetName = [](const muse::midi::Program& program, const AudioResourceMeta& meta) {
        const QString name = meta.attributeVal(PRESET_NAME_ATTRIBUTE).toQString();
        return name.isEmpty() ? muse::qtrc("playback", "Bank %1, preset %2").arg(program.bank).arg(program.program) : name;
    };

    for (const auto& [soundFont, presets] : bySoundFont) {
        if (soundFont != MS_BASIC_SOUNDFONT_NAME.toQString()) {
            for (const auto& [program, meta] : presets) {
                m_soundFontPresets.push_back({ meta, soundFont, muse::qtrc("playback", "Bank %1").arg(program.bank),
                                               presetName(program, meta) });
            }
            continue;
        }

        std::function<void(const MsBasicItem&, const QString&)> addCategory = [&](const MsBasicItem& item, const QString& category) {
            for (const MsBasicItem& subItem : item.subItems) {
                if (!subItem.subItems.empty()) {
                    addCategory(subItem, category + " › " + subItem.title.toQString());
                    continue;
                }

                auto it = presets.find(subItem.preset);
                if (it == presets.end()) {
                    continue;
                }

                // as in the Sound menu, see https://github.com/musescore/MuseScore/issues/20142
                const QString name = presetName(it->first, it->second);
                if (!name.contains(u"Expr.")) {
                    m_soundFontPresets.push_back({ it->second, soundFont, category, name });
                }
            }
        };

        for (const MsBasicItem& category : MS_BASIC_PRESET_CATEGORIES) {
            addCategory(category, category.title.toQString());
        }
    }
}

// ---- instruments

void TrackPresetsModel::updateInstrumentSounds()
{
    const project::INotationProjectPtr project = globalContext()->currentProject();
    const project::IProjectAudioSettingsPtr settings = project ? project->audioSettings() : nullptr;
    if (!settings) {
        return;
    }

    bool changed = false;
    for (ScoreInstrument& instrument : m_instruments) {
        const QString sound = TrackPresets::soundNameOf(settings->trackInputParams(instrument.trackId));
        const QColor color = settings->trackHasExistingOutputParams(instrument.trackId)
                             ? settings->trackOutputParams(instrument.trackId).color : QColor();
        if (sound != instrument.sound || color != instrument.color) {
            instrument.sound = sound;
            instrument.color = color;
            changed = true;
        }
    }

    if (changed) {
        ++m_instrumentsRevision;
        emit instrumentsChanged();
    }
}

int TrackPresetsModel::instrumentCount() const
{
    return static_cast<int>(m_instruments.size());
}

int TrackPresetsModel::instrumentsRevision() const
{
    return m_instrumentsRevision;
}

QVariantMap TrackPresetsModel::instrumentAt(int index) const
{
    if (index < 0 || index >= static_cast<int>(m_instruments.size())) {
        return QVariantMap();
    }

    const ScoreInstrument& instrument = m_instruments.at(index);
    return {
        { "name", instrument.name },
        { "sound", instrument.sound },
        { "color", instrument.color },
        { "hasColor", instrument.color.isValid() },
        { "selected", instrument.selected },
    };
}

void TrackPresetsModel::selectInstrument(int index, bool toggle, bool range)
{
    if (index < 0 || index >= static_cast<int>(m_instruments.size())) {
        return;
    }

    if (range && m_instrumentAnchor >= 0) {
        for (int i = 0; i < static_cast<int>(m_instruments.size()); ++i) {
            const bool inRange = i >= std::min(m_instrumentAnchor, index) && i <= std::max(m_instrumentAnchor, index);
            m_instruments[i].selected = inRange || (toggle && m_instruments[i].selected);
        }
    } else if (toggle) {
        m_instruments[index].selected = !m_instruments[index].selected;
        m_instrumentAnchor = index;
    } else {
        for (ScoreInstrument& instrument : m_instruments) {
            instrument.selected = false;
        }
        m_instruments[index].selected = true;
        m_instrumentAnchor = index;
    }

    ++m_instrumentsRevision;
    emit instrumentsChanged();
    emit selectionChanged(); // whether the selected preset or sound can be applied
}

std::vector<InstrumentTrackId> TrackPresetsModel::selectedTrackIds() const
{
    std::vector<InstrumentTrackId> trackIds;
    for (const ScoreInstrument& instrument : m_instruments) {
        if (instrument.selected) {
            trackIds.push_back(instrument.trackId);
        }
    }

    return trackIds;
}

// ---- the current source's items

int TrackPresetsModel::itemCount() const
{
    switch (m_source) {
    case Presets: return static_cast<int>(m_presetList.size());
    case MuseSounds: return static_cast<int>(m_sounds.size());
    default: return static_cast<int>(m_soundFontPresets.size());
    }
}

QString TrackPresetsModel::itemKey(int index) const
{
    switch (m_source) {
    case Presets: return m_presetList.at(index).filePath;
    case MuseSounds: return SOUND_KEY_PREFIX + QString::fromStdString(m_sounds.at(index).id);
    default: return SOUNDFONT_KEY_PREFIX + QString::fromStdString(m_soundFontPresets.at(index).meta.id);
    }
}

QString TrackPresetsModel::itemTitle(int index) const
{
    switch (m_source) {
    case Presets: return m_presetList.at(index).name;
    case MuseSounds: return m_sounds.at(index).attributeVal(u"museName").toQString();
    default: return m_soundFontPresets.at(index).name;
    }
}

QString TrackPresetsModel::itemDetails(int index) const
{
    //! NOTE: a preset's plugin, to tell two presets of the same name apart (a "Flute" of each library), unless it's a
    //! column already; its tags would be cut, they're shown for the selected one
    if (m_source == Presets) {
        const auto [firstField, secondField] = columnFields();
        const bool pluginIsColumn = firstField == Field::Plugin || secondField == Field::Plugin;
        return pluginIsColumn ? QString() : m_presetList.at(index).pluginName;
    }

    if (m_source == SoundFonts) {
        return QString();
    }

    return m_sounds.at(index).attributeVal(u"isOnline") == u"1" ? muse::qtrc("playback", "Online") : QString();
}

bool TrackPresetsModel::itemAvailable(int index) const
{
    return m_source != Presets || m_presets->isAvailable(m_presetList.at(index));
}

QString TrackPresetsModel::itemField(int index, Field field) const
{
    if (m_source == Presets) {
        const TrackPresetInfo& preset = m_presetList.at(index);
        switch (field) {
        case Field::Family: return orOther(preset.familyName);
        case Field::Instrument: return orOther(preset.instrumentName);
        case Field::Plugin: return orOther(preset.pluginName);
        default: return QString();
        }
    }

    if (m_source == SoundFonts) {
        const SoundFontPreset& preset = m_soundFontPresets.at(index);
        switch (field) {
        case Field::SoundFont: return orOther(preset.soundFont);
        case Field::Category: return orOther(preset.category);
        default: return QString();
        }
    }

    const AudioResourceMeta& sound = m_sounds.at(index);
    switch (field) {
    case Field::Vendor: return orOther(sound.attributeVal(u"museVendorName").toQString());
    case Field::Category: return orOther(sound.attributeVal(u"museCategory").toQString());
    default: return QString();
    }
}

//! NOTE: everything is offered (any sound on any instrument), the search narrows it
bool TrackPresetsModel::itemMatchesSearch(int index) const
{
    if (m_searchText.isEmpty()) {
        return true;
    }

    QStringList fields;
    if (m_source == Presets) {
        const TrackPresetInfo& preset = m_presetList.at(index);
        fields << preset.name << preset.instrumentName << preset.familyName << preset.pluginName << preset.tags;
    } else if (m_source == MuseSounds) {
        const AudioResourceMeta& sound = m_sounds.at(index);
        for (const char16_t* key : { u"museName", u"museVendorName", u"musePack", u"museCategory" }) {
            fields << sound.attributeVal(key).toQString();
        }
    } else {
        const SoundFontPreset& preset = m_soundFontPresets.at(index);
        fields << preset.name << preset.soundFont << preset.category;
    }

    for (const QString& field : fields) {
        if (field.contains(m_searchText, Qt::CaseInsensitive)) {
            return true;
        }
    }

    return false;
}

// ---- columns

std::pair<TrackPresetsModel::Field, TrackPresetsModel::Field> TrackPresetsModel::columnFields() const
{
    if (m_source == MuseSounds) {
        return { Field::Vendor, Field::Category };
    }

    if (m_source == SoundFonts) {
        return { Field::SoundFont, Field::Category };
    }

    switch (m_groupBy) {
    case FamilyPlugin: return { Field::Family, Field::Plugin };
    case PluginFamily: return { Field::Plugin, Field::Family };
    case PluginInstrument: return { Field::Plugin, Field::Instrument };
    case InstrumentPlugin: return { Field::Instrument, Field::Plugin };
    default: break;
    }

    return { Field::Family, Field::Instrument };
}

QString TrackPresetsModel::fieldTitle(Field field)
{
    switch (field) {
    case Field::Family: return muse::qtrc("playback", "Family");
    case Field::Instrument: return muse::qtrc("playback", "Instrument");
    case Field::Plugin: return muse::qtrc("playback", "Plugin");
    case Field::Vendor: return muse::qtrc("playback", "Vendor");
    case Field::Category: return muse::qtrc("playback", "Category");
    case Field::SoundFont: return muse::qtrc("playback", "SoundFont");
    }

    return QString();
}

//! NOTE: the names of the items offered; a name no longer there gives way to the first one
void TrackPresetsModel::rebuildColumns()
{
    const auto [firstField, secondField] = columnFields();

    std::map<QString, std::map<QString, std::vector<int>, CaseInsensitiveLess>, CaseInsensitiveLess> groups;
    const int count = itemCount();
    for (int i = 0; i < count; ++i) {
        if (itemMatchesSearch(i)) {
            groups[itemField(i, firstField)][itemField(i, secondField)].push_back(i);
        }
    }

    QStringList firstNames;
    for (const auto& [name, subGroups] : groups) {
        firstNames << name;
    }
    if (!firstNames.contains(m_selectedFirst)) {
        m_selectedFirst = firstNames.isEmpty() ? QString() : firstNames.first();
    }

    QStringList secondNames;
    m_itemIndices.clear();
    auto first = groups.find(m_selectedFirst);
    if (first != groups.end()) {
        for (const auto& [name, items] : first->second) {
            secondNames << name;
        }
        if (!secondNames.contains(m_selectedSecond)) {
            m_selectedSecond = secondNames.first();
        }
        m_itemIndices = first->second.at(m_selectedSecond);

        // by name, but a SoundFont's presets in its own order (by program)
        if (m_source != SoundFonts) {
            std::sort(m_itemIndices.begin(), m_itemIndices.end(), [this](int a, int b) {
                return itemTitle(a).compare(itemTitle(b), Qt::CaseInsensitive) < 0;
            });
        }
    } else {
        m_selectedSecond.clear();
    }

    saveNavigation();

    // each list notified only when it changed (see the NOTE in the header)
    const QString firstTitle = fieldTitle(firstField);
    if (firstNames != m_firstColumnNames || firstTitle != m_firstColumnTitle) {
        m_firstColumnNames = firstNames;
        m_firstColumnTitle = firstTitle;
        emit firstColumnChanged();
    }

    const QString secondTitle = fieldTitle(secondField);
    if (secondNames != m_secondColumnNames || secondTitle != m_secondColumnTitle) {
        m_secondColumnNames = secondNames;
        m_secondColumnTitle = secondTitle;
        emit secondColumnChanged();
    }

    QVariantList items;
    for (int index : m_itemIndices) {
        QVariantMap item;
        item["index"] = index;
        item["name"] = itemTitle(index);
        item["details"] = itemDetails(index);
        item["isAvailable"] = itemAvailable(index);
        items << item;
    }
    const QString itemsTitle = m_source == Presets ? muse::qtrc("playback", "Preset") : muse::qtrc("playback", "Sound");
    if (items != m_columnItems || itemsTitle != m_itemsColumnTitle) {
        m_columnItems = items;
        m_itemsColumnTitle = itemsTitle;
        emit itemsChanged();
    }

    emit columnSelectionChanged();
    emit selectionChanged();
}

QString TrackPresetsModel::firstColumnTitle() const
{
    return m_firstColumnTitle;
}

QStringList TrackPresetsModel::firstColumnNames() const
{
    return m_firstColumnNames;
}

QString TrackPresetsModel::selectedFirst() const
{
    return m_selectedFirst;
}

void TrackPresetsModel::setSelectedFirst(const QString& name)
{
    if (m_selectedFirst != name) {
        m_selectedFirst = name;
        rebuildColumns();
    }
}

QString TrackPresetsModel::secondColumnTitle() const
{
    return m_secondColumnTitle;
}

QStringList TrackPresetsModel::secondColumnNames() const
{
    return m_secondColumnNames;
}

QString TrackPresetsModel::selectedSecond() const
{
    return m_selectedSecond;
}

void TrackPresetsModel::setSelectedSecond(const QString& name)
{
    if (m_selectedSecond != name) {
        m_selectedSecond = name;
        rebuildColumns();
    }
}

QString TrackPresetsModel::itemsColumnTitle() const
{
    return m_itemsColumnTitle;
}

QVariantList TrackPresetsModel::columnItems() const
{
    return m_columnItems;
}

int TrackPresetsModel::source() const
{
    return m_source;
}

void TrackPresetsModel::setSource(int source)
{
    if (m_source == source || source < Presets || source > SoundFonts) {
        return;
    }

    m_source = source;
    restoreNavigation();
    emit sourceChanged();
    rebuildColumns();
}

QVariantList TrackPresetsModel::groupByOptions() const
{
    return {
        QVariantMap { { "text", muse::qtrc("playback", "Family › Instrument") }, { "value", FamilyInstrument } },
        QVariantMap { { "text", muse::qtrc("playback", "Family › Plugin") }, { "value", FamilyPlugin } },
        QVariantMap { { "text", muse::qtrc("playback", "Plugin › Family") }, { "value", PluginFamily } },
        QVariantMap { { "text", muse::qtrc("playback", "Plugin › Instrument") }, { "value", PluginInstrument } },
        QVariantMap { { "text", muse::qtrc("playback", "Instrument › Plugin") }, { "value", InstrumentPlugin } },
    };
}

int TrackPresetsModel::groupBy() const
{
    return m_groupBy;
}

void TrackPresetsModel::setGroupBy(int groupBy)
{
    if (m_groupBy == groupBy || groupBy < FamilyInstrument || groupBy > InstrumentPlugin) {
        return;
    }

    m_groupBy = groupBy;
    settings()->setSharedValue(GROUP_BY_KEY, Val(groupBy));
    emit groupByChanged();
    m_selectedFirst.clear();
    m_selectedSecond.clear();
    rebuildColumns();
}

QString TrackPresetsModel::searchText() const
{
    return m_searchText;
}

void TrackPresetsModel::setSearchText(const QString& text)
{
    const QString trimmed = text.trimmed();
    if (m_searchText == trimmed) {
        return;
    }

    m_searchText = trimmed;
    emit searchTextChanged();
    rebuildColumns();
}

// ---- selection

//! NOTE: the selected preset's or sound's index in its list, -1 if it's not in the items column
int TrackPresetsModel::selectedItemIndex() const
{
    for (int index : m_itemIndices) {
        if (itemKey(index) == m_selectedKey) {
            return index;
        }
    }

    return -1;
}

const TrackPresetInfo* TrackPresetsModel::selectedPreset() const
{
    const int index = selectedItemIndex();
    return m_source == Presets && index >= 0 ? &m_presetList.at(index) : nullptr;
}

void TrackPresetsModel::selectItem(int itemIndex)
{
    if (itemIndex < 0 || itemIndex >= itemCount()) {
        return;
    }

    const QString key = itemKey(itemIndex);
    if (m_selectedKey == key) {
        return;
    }

    m_selectedKey = key;
    saveNavigation();
    emit columnSelectionChanged();
    emit selectionChanged();
}

bool TrackPresetsModel::hasSelectedItem() const
{
    return selectedItemIndex() >= 0;
}

bool TrackPresetsModel::hasSelectedPreset() const
{
    return selectedPreset() != nullptr;
}

QString TrackPresetsModel::selectedPresetPlugin() const
{
    const TrackPresetInfo* preset = selectedPreset();
    return preset ? preset->pluginName : QString();
}

QString TrackPresetsModel::selectedPresetName() const
{
    const TrackPresetInfo* preset = selectedPreset();
    return preset ? preset->name : QString();
}

QString TrackPresetsModel::selectedPresetTags() const
{
    const TrackPresetInfo* preset = selectedPreset();
    return preset ? preset->tags.join(", ") : QString();
}

QString TrackPresetsModel::selectedItemDetails() const
{
    const int itemIndex = selectedItemIndex();
    if (itemIndex < 0) {
        return QString();
    }

    if (m_source == SoundFonts) {
        const SoundFontPreset& preset = m_soundFontPresets.at(itemIndex);
        return preset.soundFont + " · " + preset.category + "\n"
               + muse::qtrc("playback", "Changes only the sound: the rest of the track stays as it is");
    }

    if (m_source == MuseSounds) {
        const AudioResourceMeta& sound = m_sounds.at(itemIndex);
        QStringList details;
        for (const char16_t* key : { u"museName", u"museVendorName", u"musePack", u"museCategory" }) {
            details << sound.attributeVal(key).toQString();
        }
        details.removeAll(QString());
        details.removeDuplicates();
        return details.join(" · ") + "\n" + muse::qtrc("playback", "Changes only the sound: the rest of the track stays as it is");
    }

    // a preset's plugin, name and tags have fields of their own
    const TrackPresetInfo& preset = m_presetList.at(itemIndex);
    return m_presets->isAvailable(preset) ? QString()
           : muse::qtrc("playback", "%1 is not installed on this computer").arg(preset.pluginName);
}

void TrackPresetsModel::updateSelectedPreset(const QString& name, const QString& tagsText)
{
    const TrackPresetInfo* preset = selectedPreset();
    const QString newName = name.simplified();
    const QStringList tags = SaveTrackPresetModel::tagsFromText(tagsText);
    if (!preset || newName.isEmpty() || (newName == preset->name && tags == preset->tags)) {
        return;
    }

    TrackPresetInfo updated = *preset;
    updated.name = newName;
    updated.tags = tags;

    //! NOTE: the file follows the name (update() refuses another preset's), the selection with it
    const QString previousKey = m_selectedKey;
    m_selectedKey = m_presets->filePathFor(newName, preset->pluginName);

    const Ret ret = m_presets->update(updated); // reloads the list (see presetsChanged)
    if (!ret) {
        m_selectedKey = previousKey;
        interactive()->error(ret.text(), "");
        emit selectionChanged(); // the fields back to the preset's
    }
}

void TrackPresetsModel::removeSelectedPreset()
{
    const TrackPresetInfo* preset = selectedPreset();
    if (!preset) {
        return;
    }

    const TrackPresetInfo removed = *preset;
    interactive()->question("", muse::qtrc("playback", "Delete the track preset “%1”?").arg(removed.name).toStdString(), {
        IInteractive::Button::Cancel, IInteractive::Button::Yes
    }, IInteractive::Button::Yes)
    .onResolve(this, [this, removed](const IInteractive::Result& res) {
        if (res.isButton(IInteractive::Button::Yes)) {
            m_presets->remove(removed);
        }
    });
}

// ---- applying

void TrackPresetsModel::applyItemTo(int itemIndex, const std::vector<InstrumentTrackId>& trackIds)
{
    if (itemIndex < 0 || itemIndex >= itemCount() || !itemAvailable(itemIndex)) {
        return;
    }

    //! NOTE: a preset's snapshot read once, only now
    std::optional<TrackPreset> preset;
    if (m_source == Presets) {
        preset = m_presets->readPreset(m_presetList.at(itemIndex).filePath);
        if (!preset) {
            interactive()->error(muse::trc("playback", "Cannot read the track preset"), "");
            return;
        }
    }

    for (const InstrumentTrackId& trackId : trackIds) {
        switch (m_source) {
        case Presets:
            m_presets->apply(*preset, trackId);
            break;
        case MuseSounds:
            m_presets->applySound(m_sounds.at(itemIndex), trackId);
            break;
        default:
            m_presets->applySound(m_soundFontPresets.at(itemIndex).meta, trackId);
            break;
        }
    }
}

void TrackPresetsModel::applyItem(int itemIndex)
{
    applyItemTo(itemIndex, selectedTrackIds());
}

void TrackPresetsModel::applySelectedItem()
{
    applyItem(selectedItemIndex());
}

bool TrackPresetsModel::canApplySelectedItem() const
{
    const int itemIndex = selectedItemIndex();
    return itemIndex >= 0 && itemAvailable(itemIndex) && !selectedTrackIds().empty();
}
