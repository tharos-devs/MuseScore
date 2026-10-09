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

#include "savetrackpresetmodel.h"

#include <QFile>

#include "translation.h"

using namespace mu::playback;
using namespace muse;

SaveTrackPresetModel::SaveTrackPresetModel(QObject* parent)
    : QObject(parent), muse::Contextable(muse::iocCtxForQmlObject(this))
{
}

void SaveTrackPresetModel::load(const QString& partId, const QString& instrumentId)
{
    m_presets = std::make_unique<TrackPresets>(iocContext());

    bool ok = false;
    const uint64_t part = partId.toULongLong(&ok);
    if (!ok) {
        return;
    }

    m_preset = m_presets->capture(engraving::InstrumentTrackId { muse::ID(part), String::fromQString(instrumentId) });
    if (!m_preset) {
        return;
    }

    //! NOTE: the tags of the last preset of that plugin (e.g. its library); the name stays the instrument's
    m_tagsText = m_presets->lastTagsForPlugin(m_preset->pluginName).join(", ");

    emit changed();
}

QString SaveTrackPresetModel::pluginName() const
{
    return m_preset ? m_preset->pluginName : QString();
}

QString SaveTrackPresetModel::name() const
{
    return m_preset ? m_preset->name : QString();
}

void SaveTrackPresetModel::setName(const QString& name)
{
    if (!m_preset || m_preset->name == name) {
        return;
    }

    m_preset->name = name;
    emit changed();
}

QString SaveTrackPresetModel::tagsText() const
{
    return m_tagsText;
}

void SaveTrackPresetModel::setTagsText(const QString& text)
{
    if (m_tagsText == text) {
        return;
    }

    m_tagsText = text;
    emit changed();
}

QStringList SaveTrackPresetModel::tagsFromText(const QString& text)
{
    QStringList tags;
    for (const QString& part : text.split(',')) {
        const QString tag = part.simplified();
        if (!tag.isEmpty() && !tags.contains(tag, Qt::CaseInsensitive)) {
            tags << tag;
        }
    }

    return tags;
}

QString SaveTrackPresetModel::automaticTagsText() const
{
    if (!m_preset) {
        return QString();
    }

    // the plugin has a field of its own
    QStringList tags { m_preset->instrumentName, m_preset->familyName };
    tags.removeAll(QString());
    return tags.join(" · ");
}

bool SaveTrackPresetModel::canSave() const
{
    return m_preset && !m_preset->name.trimmed().isEmpty();
}

void SaveTrackPresetModel::save()
{
    if (!canSave()) {
        return;
    }

    m_preset->name = m_preset->name.simplified();
    m_preset->tags = tagsFromText(m_tagsText);

    if (!QFile::exists(m_presets->filePathFor(m_preset->name, m_preset->pluginName))) {
        write();
        return;
    }

    interactive()->question(muse::trc("playback", "A track preset with this name already exists for this plugin"),
                            muse::qtrc("playback", "Do you want to replace “%1”?").arg(m_preset->name).toStdString(),
                            { IInteractive::Button::Cancel, IInteractive::Button::Yes }, IInteractive::Button::Yes)
    .onResolve(this, [this](const IInteractive::Result& res) {
        if (res.isButton(IInteractive::Button::Yes)) {
            write();
        }
    });
}

void SaveTrackPresetModel::write()
{
    const Ret ret = m_presets->save(*m_preset);
    if (!ret) {
        interactive()->error(muse::trc("playback", "Cannot save the track preset"), ret.text());
        return;
    }

    emit saved();
}
