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

#include <memory>
#include <optional>

#include <QObject>
#include <qqmlintegration.h>

#include "async/asyncable.h"
#include "modularity/ioc.h"
#include "interactive/iinteractive.h"

#include "playback/internal/trackpresets.h"

namespace mu::playback {
//! NOTE: "Save as track preset…": names the snapshot of a track (see TrackPresets) and gives it tags. The instrument,
//! its family and the plugin are tags of their own, shown here but not edited
class SaveTrackPresetModel : public QObject, public muse::Contextable, public muse::async::Asyncable
{
    Q_OBJECT
    QML_ELEMENT;

    Q_PROPERTY(QString pluginName READ pluginName NOTIFY changed)
    Q_PROPERTY(QString name READ name WRITE setName NOTIFY changed)
    Q_PROPERTY(QString tagsText READ tagsText WRITE setTagsText NOTIFY changed)
    Q_PROPERTY(QString automaticTagsText READ automaticTagsText NOTIFY changed)
    Q_PROPERTY(bool canSave READ canSave NOTIFY changed)

    muse::ContextInject<muse::IInteractive> interactive = { this };

public:
    explicit SaveTrackPresetModel(QObject* parent = nullptr);

    Q_INVOKABLE void load(const QString& partId, const QString& instrumentId);
    //! NOTE: asks before replacing a preset of the same name; emits saved once written
    Q_INVOKABLE void save();

    QString pluginName() const;
    QString name() const;
    void setName(const QString& name);
    QString tagsText() const;
    void setTagsText(const QString& text);
    QString automaticTagsText() const;
    bool canSave() const;

    static QStringList tagsFromText(const QString& text);

signals:
    void changed();
    void saved();

private:
    void write();

    std::unique_ptr<TrackPresets> m_presets;
    std::optional<TrackPreset> m_preset;
    QString m_tagsText;
};
}
