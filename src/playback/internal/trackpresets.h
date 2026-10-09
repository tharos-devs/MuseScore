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

#include <optional>
#include <vector>

#include <QJsonObject>
#include <QString>
#include <QStringList>

#include "async/asyncable.h"
#include "async/notification.h"
#include "modularity/ioc.h"
#include "global/iglobalconfiguration.h"
#include "global/io/ifilesystem.h"
#include "context/iglobalcontext.h"
#include "audio/main/iplayback.h"
#include "audioplugins/iknownaudiopluginsregister.h"
#include "project/iprojectaudiosettings.h"

#include "muse_framework_config.h"
#ifdef MUSE_MODULE_VST
#include "vst/ivstpluginstateprovider.h"
#endif

#include "../iplaybackcontroller.h"

namespace mu::playback {
//! NOTE: a snapshot of an instrument track's sound, to apply to another one (in this project or another): its
//! instrument (VST3 and its loaded patch), Mixer settings (gain, volume, pan, effects, sends to the aux/group buses,
//! by bus name), color, MIDI routing and articulation map. Never the score's instrument (name, clef, transposition):
//! only what the track sounds like. One file per preset in the user's TrackPresets folder.
//! TrackPresetInfo is what lists show (a few hundred bytes); the snapshot, with the plugins' states (up to megabytes for
//! a sampler), is only read to apply or change a preset
struct TrackPresetInfo {
    QString filePath;
    QString name;
    QStringList tags; // the user's own, free

    // what it was saved from, for the automatic tags and to offer it for the same instrument
    QString instrumentId; // e.g. "flute"
    QString instrumentName;
    QString familyName; // the group of the New score dialog (Woodwinds, Brass...)
    QString pluginName; // the sound's (e.g. "Kontakt 7"), "MuseSounds"...

    // its instrument plugin, to know whether it's on this machine
    muse::audio::AudioResourceId pluginResourceId;
    bool isVstPlugin = false;

    qint64 modified = 0; // the file's, in ms since epoch
};

struct TrackPreset : TrackPresetInfo {
    struct AuxSend {
        QString busName;
        bool isGroupBus = false;
        float signalAmount = 0.f;
        bool active = false;
    };

    project::AudioInputParams input;
    project::AudioOutputParams output; // without aux sends, see auxSends
    std::vector<AuxSend> auxSends;

    bool hasArticulationMap = false;
    QString articulationMapPath;
    QString articulationMapText;
};

class TrackPresets : public muse::Contextable, public muse::async::Asyncable
{
    muse::GlobalInject<muse::IGlobalConfiguration> globalConfiguration;
    muse::GlobalInject<muse::io::IFileSystem> fileSystem;
    muse::GlobalInject<muse::audioplugins::IKnownAudioPluginsRegister> knownAudioPlugins;
#ifdef MUSE_MODULE_VST
    muse::GlobalInject<muse::vst::IVstPluginStateProvider> vstPluginStateProvider;
#endif
    muse::ContextInject<context::IGlobalContext> globalContext = { this };
    muse::ContextInject<IPlaybackController> playbackController = { this };
    muse::ContextInject<muse::audio::IPlayback> playback = { this };

public:
    explicit TrackPresets(const muse::modularity::ContextPtr& iocCtx);

    muse::io::path_t presetsDir() const;

    //! NOTE: the presets' infos, from the folder's index (see readIndex()): only the presets added or changed since are
    //! read, and only their infos
    std::vector<TrackPresetInfo> loadAll() const;
    //! NOTE: the whole preset, snapshot included
    std::optional<TrackPreset> readPreset(const QString& filePath) const;

    //! NOTE: the track as it is now (its plugins' live states included), named after its instrument
    std::optional<TrackPreset> capture(const engraving::InstrumentTrackId& trackId) const;
    muse::Ret save(TrackPreset& preset) const; // sets its filePath, overwriting a preset of the same name
    muse::Ret remove(const TrackPresetInfo& preset) const;
    //! NOTE: name and tags only, without touching the snapshot (the file follows the name)
    muse::Ret update(const TrackPresetInfo& preset) const;

    //! NOTE: whether its instrument plugin is on this machine (MuseScore's own sounds always are)
    bool isAvailable(const TrackPresetInfo& preset) const;

    void apply(const TrackPreset& preset, const engraving::InstrumentTrackId& trackId);
    //! NOTE: only the instrument sound (e.g. a MuseSounds one), like the Mixer's Sound menu: the rest of the track stays
    void applySound(const muse::audio::AudioResourceMeta& sound, const engraving::InstrumentTrackId& trackId);

    //! NOTE: the free tags of the last preset saved with that plugin, to offer them for the next one
    QStringList lastTagsForPlugin(const QString& pluginName) const;

    //! NOTE: a preset was saved, changed or removed (by any window)
    static muse::async::Notification presetsChanged();

    static QString pluginNameOf(const project::AudioInputParams& input);
    //! NOTE: what the track plays, e.g. "Flute · VSL" for a MuseSounds sound, the plugin's name for a VST3
    static QString soundNameOf(const project::AudioInputParams& input);
    //! NOTE: a name is unique per plugin (a "Flute" of each library): the file is named after both
    QString filePathFor(const QString& name, const QString& pluginName) const;

private:
    project::IProjectAudioSettingsPtr audioSettings() const;
    QString auxBusName(muse::audio::aux_channel_idx_t index) const;

    static TrackPresetInfo infoFromJson(const QJsonObject& root);
    muse::Ret write(const TrackPreset& preset) const;

    //! NOTE: <TrackPresets>/index.json: per preset file name, its size, date and infos
    muse::io::path_t indexPath() const;
};
}
