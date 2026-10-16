/*
 * SPDX-License-Identifier: GPL-3.0-only
 * MuseScore-Studio-CLA-applies
 *
 * MuseScore Studio
 * Music Composition & Notation
 *
 * Copyright (C) 2021 MuseScore Limited and others
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

#include <map>
#include <optional>

#include "ui/view/widgetdialog.h"

#include "async/asyncable.h"

#include "ui_customizekitdialog.h"

#include "modularity/ioc.h"
#include "interactive/iinteractive.h"
#include "context/iglobalcontext.h"
#include "notation/inotationconfiguration.h"
#include "engraving/iengravingfontsprovider.h"
#include "engraving/rendering/isinglerenderer.h"
#include "engraving/ipalettescoreprovider.h"
#include "ui/iuiconfiguration.h"

#include "engraving/dom/drumset.h"
#include "notation/types/instrumentstypes.h"

namespace mu::palette {
//---------------------------------------------------------
//   CustomizeKitDialog
//---------------------------------------------------------

class CustomizeKitDialog : public muse::ui::WidgetDialog, private Ui::CustomizeKitDialog, public muse::async::Asyncable
{
    Q_OBJECT

public:
    inline static muse::GlobalInject<engraving::IEngravingFontsProvider> engravingFonts;
    inline static muse::GlobalInject<muse::ui::IUiConfiguration> uiConfiguration;
    muse::GlobalInject<notation::INotationConfiguration> notationConfiguration;
    muse::GlobalInject<engraving::rendering::ISingleRenderer> engravingRenderer;
    muse::ContextInject<muse::IInteractive> interactive = { this };
    muse::ContextInject<context::IGlobalContext> globalContext = { this };
    muse::ContextInject<engraving::IPaletteScoreProvider> paletteScoreProvider = { this };
public:
    CustomizeKitDialog(QWidget* parent = nullptr);

    void componentComplete() override;

    //! NOTE: like Cancel: the window's close button and Escape revert the changes
    void reject() override;

protected:
    void closeEvent(QCloseEvent* event) override;

private slots:
    void bboxClicked(QAbstractButton* button);
    void itemChanged(QTreeWidgetItem* current, QTreeWidgetItem* previous);
    void nameChanged(const QString&);
    void valueChanged();
    void load();
    void save();
    void customGboxToggled(bool);
    void customQuarterChanged(int);

    void defineShortcut();
    void setAlwaysOnTop(bool on);

private:
    void initDrumsetAndKey();

    void apply();
    void cancel();

    const engraving::Drumset* scoreDrumset() const;
    void replaceScoreDrumset(const engraving::Drumset& drumset);
    void syncWithScore();
    void closeWithoutReverting();

    void loadPitchesList(int pitchToSelect); // Selects the first item if pitchToSelect isn't found

    void updateExample();

    void fillCustomNoteheadsDataFromComboboxes(int pitch);
    void setCustomNoteheadsGUIEnabled(bool enabled);

    void setEnabledPitchControls(bool enable);
    void fillNoteheadsComboboxes(bool customGroup, int pitch);

    void notifyAboutNoteInputStateChanged();

    notation::INotationPtr m_notation;
    notation::InstrumentKey m_instrumentKey;

    //! NOTE: what Cancel reverts: the pitches the dialog changed since it was opened or applied, each as it
    //! was before the dialog's first change (the other pitches may have been edited elsewhere meanwhile)
    std::map<int, engraving::DrumInstrument> m_originDrums;
    std::optional<size_t> m_originPercussionPanelColumns;
    engraving::Drumset m_editedDrumset;

    //! NOTE: the score's drumset as last set or read by the dialog: a different one in the score
    //! was changed elsewhere (undo/redo, Percussion panel) while the dialog is open
    engraving::Drumset m_scoreDrumset;
};
}
