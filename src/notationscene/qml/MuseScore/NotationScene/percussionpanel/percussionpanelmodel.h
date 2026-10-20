/*
 * SPDX-License-Identifier: GPL-3.0-only
 * MuseScore-Studio-CLA-applies
 *
 * MuseScore Studio
 * Music Composition & Notation
 *
 * Copyright (C) 2024 MuseScore Limited and others
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

#include <QObject>
#include <qqmlintegration.h>

#include "modularity/ioc.h"
#include "async/asyncable.h"

#include "rcommand/icommanddispatcher.h"
#include "rcommand/commandable.h"
#include "context/iglobalcontext.h"
#include "interactive/iinteractive.h"
#include "musesampler/imusesamplerinfo.h"
#include "notation/iinstrumentsrepository.h"
#include "notation/inotationconfiguration.h"
#include "playback/iplaybackcontroller.h"

#include "inotationsceneconfiguration.h"

#include "percussionpanelpadlistmodel.h"

namespace mu::notation {
namespace PanelMode {
Q_NAMESPACE;
QML_ELEMENT;

enum Mode
{
    WRITE,
    SOUND_PREVIEW, //! NOTE: Not to be confused with "notation preview"
    EDIT_LAYOUT
};
Q_ENUM_NS(Mode)
}

class PercussionPanelModel : public QObject, public muse::Contextable, public muse::async::Asyncable, public muse::rcommand::Commandable
{
    Q_OBJECT

    //! NOTE: the pad outlined: the one edited in the "Edit pad" dialog, else the one selected (clicked) (-1: none)
    Q_PROPERTY(int editedPadIndex READ editedPadIndex NOTIFY editedPadIndexChanged)

    Q_PROPERTY(bool enabled READ enabled NOTIFY enabledChanged)

    Q_PROPERTY(QString soundTitle READ soundTitle NOTIFY soundTitleChanged)

    Q_PROPERTY(mu::notation::PanelMode::Mode currentPanelMode READ currentPanelMode WRITE setCurrentPanelMode NOTIFY currentPanelModeChanged)
    Q_PROPERTY(bool useNotationPreview READ useNotationPreview WRITE setUseNotationPreview NOTIFY useNotationPreviewChanged)
    Q_PROPERTY(int notationPreviewNumStaffLines READ notationPreviewNumStaffLines NOTIFY notationPreviewNumStaffLinesChanged)
    Q_PROPERTY(QColor notationPreviewBackgroundColor READ notationPreviewBackgroundColor CONSTANT)

    Q_PROPERTY(mu::notation::PercussionPanelPadListModel * padListModel READ padListModel NOTIFY padListModelChanged)

    Q_PROPERTY(QList<QVariantMap> layoutMenuItems READ layoutMenuItems CONSTANT)

    QML_ELEMENT

    muse::GlobalInject<INotationSceneConfiguration> configuration;
    muse::GlobalInject<engraving::IEngravingConfiguration> engravingConfiguration;
    muse::GlobalInject<muse::musesampler::IMuseSamplerInfo> museSampler;
    muse::GlobalInject<IInstrumentsRepository> instrumentsRepository;
    muse::ContextInject<mu::context::IGlobalContext> globalContext = { this };
    muse::ContextInject<muse::IInteractive> interactive = { this };
    muse::ContextInject<muse::rcommand::ICommandDispatcher> commandDispatcher = { this };
    muse::ContextInject<mu::playback::IPlaybackController> playbackController = { this };

public:
    explicit PercussionPanelModel(QObject* parent = nullptr);

    bool enabled() const;
    QString soundTitle() const;

    PanelMode::Mode currentPanelMode() const;
    void setCurrentPanelMode(const PanelMode::Mode& panelMode);

    bool useNotationPreview() const;
    void setUseNotationPreview(bool useNotationPreview);

    int notationPreviewNumStaffLines() const;
    QColor notationPreviewBackgroundColor() const;

    PercussionPanelPadListModel* padListModel() const;

    Q_INVOKABLE void init();

    QList<QVariantMap> layoutMenuItems() const;
    Q_INVOKABLE void handleMenuItem(const QString& itemId);
    Q_INVOKABLE void editEmptyPad(int padIndex);
    Q_INVOKABLE void onEmptyPadClicked(int padIndex);

    int editedPadIndex() const;

    //! NOTE: There are a handful of circumstances where we should discard changes (e.g. undoing/redoing a layout change mid
    //! edit, resetting the layout mid edit, or selecting a different drumset mid edit)
    Q_INVOKABLE void finishEditing(bool discardChanges = false);

    Q_INVOKABLE void customizeKit();
    Q_INVOKABLE void editSelectedPad();

signals:
    void enabledChanged();

    void soundTitleChanged();

    void currentPanelModeChanged(mu::notation::PanelMode::Mode panelMode);
    void useNotationPreviewChanged(bool useNotationPreview);
    void notationPreviewNumStaffLinesChanged();

    void padListModelChanged();
    void editedPadIndexChanged();

private:
    void onCurrentNotationChanged();

    void setEnabled(bool enabled);
    void setDrumset(mu::engraving::Drumset* drumset);
    int noteNamesMiddleCOctave() const;
    bool hasSelectionInCurrentKit() const;

    void updateSoundTitle(const InstrumentTrackId& trackId);
    void setSoundTitle(const QString& soundTitle);

    QVariantMap createColumnSubItem(int numColumns) const;

    bool eventFilter(QObject* watched, QEvent* event) override;

    void onPadTriggered(int pitch, const PercussionPanelPadModel::PadAction& action);
    void onDuplicatePadRequested(int pitch);
    void onDeletePadRequested(int pitch);
    void onDefinePadShortcutRequested(int pitch);
    void onEditPadRequested(int pitch);
    void openEditPadDialog(int pitch, int newPadRow, int newPadColumn);
    void updateEditedPadIndex();
    int padIndex(int pitch, int row, int column) const;
    int adjacentPadPitch(const muse::rcommand::Params& params);

    void writePitch(int pitch, const NoteAddingMode& addingMode);
    void playPitch(int pitch);

    void resetLayout();
    Drumset standardDefaultDrumset() const;
    Drumset museSamplerDefaultDrumset() const;

    void setColumns(int numColumns);

    mu::engraving::InstrumentTrackId currentTrackId() const;

    const project::IProjectAudioSettingsPtr audioSettings() const;

    std::pair<mu::engraving::Instrument*, mu::engraving::Part*> getCurrentInstrumentAndPart() const;

    const mu::notation::INotationPtr notation() const;
    const mu::notation::INotationInteractionPtr interaction() const;

    mu::engraving::Score* score() const;

    bool m_enabled = false;

    QString m_soundTitle;

    PanelMode::Mode m_currentPanelMode = PanelMode::Mode::WRITE;

    //! NOTE: an existing pad by its pitch, a new one by its position (see PERCUSSION_PANEL_SET_EDITED_PAD_COMMAND)
    //! NOTE: the pad (by pitch) or empty slot (by position) clicked last: what "Edit pad" edits
    int m_selectedPadPitch = -1;
    int m_selectedPadRow = -1;
    int m_selectedPadColumn = -1;
    muse::ID m_selectedPadPartId; // the selection belongs to this part's kit
    int m_editedPadPitch = -1;
    int m_editedPadRow = -1;
    int m_editedPadColumn = -1;
    int m_editedPadIndex = -1;
    PanelMode::Mode m_panelModeToRestore = PanelMode::Mode::WRITE;

    PercussionPanelPadListModel* m_padListModel = nullptr;
};
}
