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
#include <vector>

#include <QObject>
#include <qqmlintegration.h>

#include "async/asyncable.h"
#include "modularity/ioc.h"
#include "context/iglobalcontext.h"
#include "interactive/iinteractive.h"
#include "audio/main/iplayback.h"

#include "playback/internal/trackpresets.h"

namespace mu::playback {
//! NOTE: the track presets window: the score's instruments on one side, on the other the track presets, the installed
//! MuseSounds sounds or the SoundFonts' presets (no track preset needed: a sound is all there is to it), in three
//! columns narrowing down to them: two groupings (a preset's family, instrument or plugin in the chosen order; a
//! sound's vendor and category; a SoundFont preset's SoundFont and category, or bank), then the presets or sounds. Searchable. A double click applies a preset or sound to the selected instruments. Only the sound
//! changes: the score's instruments stay as they are
class TrackPresetsModel : public QObject, public muse::Contextable, public muse::async::Asyncable
{
    Q_OBJECT
    QML_ELEMENT;

    //! NOTE: the lists are given as a count and an accessor, with a revision to re-read them, never as a new array: a
    //! QML list given a new array starts again from the top (a click low in a long list would scroll it back up).
    //! The instruments: instrumentAt() = { name, sound, color, hasColor, selected }
    Q_PROPERTY(int instrumentCount READ instrumentCount NOTIFY instrumentCountChanged)
    Q_PROPERTY(int instrumentsRevision READ instrumentsRevision NOTIFY instrumentsChanged)

    //! NOTE: the track presets, or the MuseSounds sounds (applying one changes only the sound)
    Q_PROPERTY(int source READ source WRITE setSource NOTIFY sourceChanged)

    //! NOTE: the presets' two grouping columns, in that order
    Q_PROPERTY(QVariantList groupByOptions READ groupByOptions CONSTANT)
    Q_PROPERTY(int groupBy READ groupBy WRITE setGroupBy NOTIFY groupByChanged)
    Q_PROPERTY(QString searchText READ searchText WRITE setSearchText NOTIFY searchTextChanged)

    //! NOTE: the columns, of what's offered (search): the first one's names, the second one's for the selected first
    //! one, then the items ([{ index, name, details, isAvailable }]) for the selected second one. Each list is only
    //! notified when it changes, a selection on its own (same reason as above)
    Q_PROPERTY(QString firstColumnTitle READ firstColumnTitle NOTIFY firstColumnChanged)
    Q_PROPERTY(QStringList firstColumnNames READ firstColumnNames NOTIFY firstColumnChanged)
    Q_PROPERTY(QString selectedFirst READ selectedFirst WRITE setSelectedFirst NOTIFY columnSelectionChanged)
    Q_PROPERTY(QString secondColumnTitle READ secondColumnTitle NOTIFY secondColumnChanged)
    Q_PROPERTY(QStringList secondColumnNames READ secondColumnNames NOTIFY secondColumnChanged)
    Q_PROPERTY(QString selectedSecond READ selectedSecond WRITE setSelectedSecond NOTIFY columnSelectionChanged)
    Q_PROPERTY(QString itemsColumnTitle READ itemsColumnTitle NOTIFY itemsChanged)
    Q_PROPERTY(QVariantList columnItems READ columnItems NOTIFY itemsChanged)
    Q_PROPERTY(int selectedItemIndex READ selectedItemIndex NOTIFY columnSelectionChanged)

    Q_PROPERTY(bool hasSelectedPreset READ hasSelectedPreset NOTIFY selectionChanged)
    Q_PROPERTY(bool hasSelectedItem READ hasSelectedItem NOTIFY selectionChanged)
    Q_PROPERTY(QString selectedPresetPlugin READ selectedPresetPlugin NOTIFY selectionChanged)
    Q_PROPERTY(QString selectedPresetName READ selectedPresetName NOTIFY selectionChanged)
    Q_PROPERTY(QString selectedPresetTags READ selectedPresetTags NOTIFY selectionChanged)
    Q_PROPERTY(QString selectedItemDetails READ selectedItemDetails NOTIFY selectionChanged)
    Q_PROPERTY(bool canApplySelectedItem READ canApplySelectedItem NOTIFY selectionChanged)

    muse::ContextInject<context::IGlobalContext> globalContext = { this };
    muse::ContextInject<muse::IInteractive> interactive = { this };
    muse::ContextInject<muse::audio::IPlayback> playback = { this };

public:
    enum Source {
        Presets = 0,
        MuseSounds,
        SoundFonts,
    };
    Q_ENUM(Source)

    enum GroupBy {
        FamilyInstrument = 0,
        FamilyPlugin,
        PluginFamily,
        PluginInstrument,
        InstrumentPlugin,
    };
    Q_ENUM(GroupBy)

    explicit TrackPresetsModel(QObject* parent = nullptr);

    //! NOTE: selects that instrument (the one the window was opened for)
    Q_INVOKABLE void load(const QString& partId, const QString& instrumentId);

    Q_INVOKABLE void selectInstrument(int index, bool toggle, bool range);
    Q_INVOKABLE QVariantMap instrumentAt(int index) const;

    //! NOTE: an item by its columnItems "index"
    Q_INVOKABLE void selectItem(int itemIndex);
    //! NOTE: to the selected instruments (a double click, or Load for the selected one)
    Q_INVOKABLE void applyItem(int itemIndex);
    Q_INVOKABLE void applySelectedItem();

    //! NOTE: the selected preset's name and tags (the file follows the name)
    Q_INVOKABLE void updateSelectedPreset(const QString& name, const QString& tagsText);
    Q_INVOKABLE void removeSelectedPreset();

    int instrumentCount() const;
    int instrumentsRevision() const;
    int source() const;
    void setSource(int source);
    QVariantList groupByOptions() const;
    int groupBy() const;
    void setGroupBy(int groupBy);
    QString searchText() const;
    void setSearchText(const QString& text);

    QString firstColumnTitle() const;
    QStringList firstColumnNames() const;
    QString selectedFirst() const;
    void setSelectedFirst(const QString& name);
    QString secondColumnTitle() const;
    QStringList secondColumnNames() const;
    QString selectedSecond() const;
    void setSelectedSecond(const QString& name);
    QString itemsColumnTitle() const;
    QVariantList columnItems() const;
    int selectedItemIndex() const;

    bool hasSelectedPreset() const;
    bool hasSelectedItem() const;
    QString selectedPresetPlugin() const;
    QString selectedPresetName() const;
    QString selectedPresetTags() const;
    QString selectedItemDetails() const;
    bool canApplySelectedItem() const;

signals:
    void instrumentCountChanged();
    void instrumentsChanged();
    void sourceChanged();
    void groupByChanged();
    void searchTextChanged();
    void firstColumnChanged();
    void secondColumnChanged();
    void itemsChanged();
    void columnSelectionChanged();
    void selectionChanged();

private:
    //! NOTE: a SoundFont's preset, with its place in the Mixer's Sound menu: MS Basic's categories, the other
    //! SoundFonts' banks
    struct SoundFontPreset {
        muse::audio::AudioResourceMeta meta;
        QString soundFont;
        QString category;
        QString name;
    };

    struct ScoreInstrument {
        engraving::InstrumentTrackId trackId;
        QString name;
        QString sound;
        QColor color;
        bool selected = false;
    };

    enum class Field {
        Family,
        Instrument,
        Plugin,
        Vendor,
        Category,
        SoundFont,
    };

    void reloadPresets();
    void reloadSounds();
    void loadSoundFontPresets(const muse::audio::AudioResourceMetaList& resources);
    void reloadInstruments();
    //! NOTE: re-reads the instruments' sounds and colors, notified only if one changed (the audio settings change often,
    //! e.g. while a Mixer fader moves)
    void updateInstrumentSounds();
    void rebuildColumns();

    //! NOTE: where the user was (tab, columns, item), kept from one opening of the window to the next, per tab
    void restoreNavigation();
    void saveNavigation() const;

    std::pair<Field, Field> columnFields() const;
    static QString fieldTitle(Field field);

    // the current source's items
    int itemCount() const;
    QString itemKey(int index) const; // stays the same when the lists are reloaded
    QString itemTitle(int index) const;
    QString itemDetails(int index) const;
    bool itemAvailable(int index) const;
    bool itemMatchesSearch(int index) const;
    QString itemField(int index, Field field) const;

    const TrackPresetInfo* selectedPreset() const;
    std::vector<engraving::InstrumentTrackId> selectedTrackIds() const;
    void applyItemTo(int itemIndex, const std::vector<engraving::InstrumentTrackId>& trackIds);

    std::unique_ptr<TrackPresets> m_presets;
    std::vector<TrackPresetInfo> m_presetList;
    muse::audio::AudioResourceMetaList m_sounds;
    std::vector<SoundFontPreset> m_soundFontPresets;
    std::vector<ScoreInstrument> m_instruments;
    int m_instrumentAnchor = -1;
    int m_instrumentsRevision = 0;

    int m_source = Presets;
    int m_groupBy = FamilyInstrument;
    QString m_searchText;

    QString m_firstColumnTitle;
    QStringList m_firstColumnNames;
    QString m_selectedFirst;
    QString m_secondColumnTitle;
    QStringList m_secondColumnNames;
    QString m_selectedSecond;
    std::vector<int> m_itemIndices; // of the selected second column name, in the source's list
    QString m_itemsColumnTitle;
    QVariantList m_columnItems;
    QString m_selectedKey;
};
}
