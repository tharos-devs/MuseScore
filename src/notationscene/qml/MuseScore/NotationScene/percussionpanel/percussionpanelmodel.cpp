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

#include "percussionpanelmodel.h"

#include <QCoreApplication>
#include <QKeyEvent>

#include "audio/common/audioutils.h"

#include "notationscene/utilities/percussionutilities.h"

#include "ui/view/iconcodes.h"

#include "engraving/iengravingconfiguration.h" // IWYU pragma: keep
#include "engraving/dom/factory.h"
#include "engraving/dom/utils.h"

#include "notation/inotation.h"
#include "notation/inotationelements.h" // IWYU pragma: keep
#include "notation/inotationinteraction.h"
#include "notation/inotationnoteinput.h"
#include "notation/inotationparts.h" // IWYU pragma: keep
#include "notation/inotationselection.h"
#include "drumkitarticulationmap.h"
#include "notationscene/notationcommands.h"

#include "project/inotationproject.h" // IWYU pragma: keep
#include "project/iprojectaudiosettings.h"

#include "notationscene/notationcommands.h"
#include "palette/palettecommands.h"

static const QString PAD_NAMES_CODE("percussion-pad-names");
static const QString NOTATION_PREVIEW_CODE("percussion-notation-preview");
static const QString SET_COLUMNS_CODE("percussion-set-columns");
static const QString EDIT_LAYOUT_CODE("percussion-edit-layout");
static const QString RESET_LAYOUT_CODE("percussion-reset-layout");

using namespace muse;
using namespace muse::ui;
using namespace mu::notation;
using namespace mu::palette;

static const char* EDIT_DRUM_PAD_URI = "musescore://notation/editdrumpad";

static const std::unordered_map<PercussionPanelPadModel::PadAction, NoteAddingMode> WRITE_ACTION_MAP = {
    { PercussionPanelPadModel::PadAction::TRIGGER_STANDARD, NoteAddingMode::NextChord },
    { PercussionPanelPadModel::PadAction::TRIGGER_ADD, NoteAddingMode::CurrentChord },
    { PercussionPanelPadModel::PadAction::TRIGGER_INSERT, NoteAddingMode::InsertChord }
};

PercussionPanelModel::PercussionPanelModel(QObject* parent)
    : QObject(parent), muse::Contextable(muse::iocCtxForQmlObject(this))
{
    m_padListModel = new PercussionPanelPadListModel(this);
    qApp->installEventFilter(this);
}

bool PercussionPanelModel::enabled() const
{
    return m_enabled;
}

void PercussionPanelModel::setEnabled(bool enabled)
{
    if (m_enabled == enabled) {
        return;
    }
    m_enabled = enabled;
    emit enabledChanged();
}

QString PercussionPanelModel::soundTitle() const
{
    return m_soundTitle;
}

void PercussionPanelModel::setSoundTitle(const QString& soundTitle)
{
    if (m_soundTitle == soundTitle) {
        return;
    }
    m_soundTitle = soundTitle;
    emit soundTitleChanged();
}

PanelMode::Mode PercussionPanelModel::currentPanelMode() const
{
    return m_currentPanelMode;
}

void PercussionPanelModel::setCurrentPanelMode(const PanelMode::Mode& panelMode)
{
    if (m_currentPanelMode == panelMode) {
        return;
    }

    // After editing, return to the last non-edit mode...
    if (panelMode != PanelMode::Mode::EDIT_LAYOUT && m_panelModeToRestore != panelMode) {
        m_panelModeToRestore = panelMode;
    }

    m_currentPanelMode = panelMode;
    emit currentPanelModeChanged(m_currentPanelMode);
}

bool PercussionPanelModel::useNotationPreview() const
{
    return configuration()->percussionPanelUseNotationPreview();
}

void PercussionPanelModel::setUseNotationPreview(bool useNotationPreview)
{
    if (configuration()->percussionPanelUseNotationPreview() == useNotationPreview) {
        return;
    }
    configuration()->setPercussionPanelUseNotationPreview(useNotationPreview);
}

int PercussionPanelModel::notationPreviewNumStaffLines() const
{
    if (!interaction()) {
        return 0;
    }

    const NoteInputState& inputState = interaction()->noteInput()->state();
    const Staff* staff = inputState.staff();

    return staff ? staff->lines(inputState.tick()) : 0;
}

QColor PercussionPanelModel::notationPreviewBackgroundColor() const
{
    const muse::Color color = engravingConfiguration()->noteBackgroundColor();
    return color.toQColor();
}

PercussionPanelPadListModel* PercussionPanelModel::padListModel() const
{
    return m_padListModel;
}

void PercussionPanelModel::init()
{
    setEnabled(m_padListModel->hasActivePads());

    m_padListModel->hasActivePadsChanged().onNotify(this, [this]() {
        setEnabled(m_padListModel->hasActivePads());
    });

    m_padListModel->padActionRequested().onReceive(this, [this](PercussionPanelPadModel::PadAction action, int pitch) {
        switch (action) {
            case PercussionPanelPadModel::PadAction::TRIGGER_STANDARD:
            case PercussionPanelPadModel::PadAction::TRIGGER_ADD:
            case PercussionPanelPadModel::PadAction::TRIGGER_INSERT:
                onPadTriggered(pitch, action);
                break;
            case PercussionPanelPadModel::PadAction::DUPLICATE:
                onDuplicatePadRequested(pitch);
                break;
            case PercussionPanelPadModel::PadAction::DELETE:
                onDeletePadRequested(pitch);
                break;
            case PercussionPanelPadModel::PadAction::DEFINE_SHORTCUT:
                onDefinePadShortcutRequested(pitch);
                break;
            case PercussionPanelPadModel::PadAction::EDIT:
                onEditPadRequested(pitch);
                break;
            default:
                break;
        }
    });

    commandDispatcher()->onRequest(this, PERCUSSION_PANEL_SET_EDITED_PAD_COMMAND, [this](const muse::rcommand::Params& params) {
        const bool newPad = params.contains("panel_row") && params.contains("panel_column");
        m_editedPadPitch = params.at("pitch").toInt();
        m_editedPadRow = newPad ? params.at("panel_row").toInt() : -1;
        m_editedPadColumn = newPad ? params.at("panel_column").toInt() : -1;
        updateEditedPadIndex();
        return muse::make_ok();
    });

    commandDispatcher()->onRequest(this, PERCUSSION_PANEL_ADJACENT_PAD_COMMAND, [this](const muse::rcommand::Request& request) {
        muse::rcommand::Response response = muse::rcommand::make_response(request, muse::make_ok());
        response.data = adjacentPadPitch(request.params);
        return response;
    });

    commandDispatcher()->onRequest(this, PERCUSSION_PANEL_FINISH_LAYOUT_EDITING_COMMAND, [this]() {
        if (m_currentPanelMode == PanelMode::Mode::EDIT_LAYOUT) {
            finishEditing();
        }
        return muse::make_ok();
    });

    // the pads move (layout edited, pad moved to another sound...)
    connect(m_padListModel, &QAbstractItemModel::modelReset, this, &PercussionPanelModel::updateEditedPadIndex);
    connect(m_padListModel, &QAbstractItemModel::layoutChanged, this, &PercussionPanelModel::updateEditedPadIndex);
    connect(m_padListModel, &QAbstractItemModel::dataChanged, this, &PercussionPanelModel::updateEditedPadIndex);

    configuration()->percussionPanelUseNotationPreviewChanged().onNotify(this, [this]() {
        const bool useNotationPreview = configuration()->percussionPanelUseNotationPreview();
        emit useNotationPreviewChanged(useNotationPreview);
    });

    onCurrentNotationChanged();

    projectSession()->currentNotationChanged().onNotify(this, [this] {
        onCurrentNotationChanged();
    });
}

QList<QVariantMap> PercussionPanelModel::layoutMenuItems() const
{
    const auto editLayoutTitle = m_currentPanelMode == PanelMode::Mode::EDIT_LAYOUT
                                 ? muse::qtrc("notation/percussion", "Finish editing")
                                 : muse::qtrc("notation/percussion", "Edit layout");

    static constexpr int editLayoutIcon = static_cast<int>(IconCode::Code::CONFIGURE);
    static constexpr int resetLayoutIcon = static_cast<int>(IconCode::Code::UNDO);

    const QVariantList columnsSubmenu = {
        createColumnSubItem(4),
        createColumnSubItem(6),
        createColumnSubItem(8),
        createColumnSubItem(12),
        createColumnSubItem(16),
        createColumnSubItem(20),
        createColumnSubItem(24),
    };

    QList<QVariantMap> menuItems = {
        { { "id", PAD_NAMES_CODE }, { "title", muse::qtrc("notation/percussion", "Pad names") },
            { "checkable", true }, { "checked", !useNotationPreview() }, { "enabled", true } },

        { { "id", NOTATION_PREVIEW_CODE }, { "title", muse::qtrc("notation/percussion", "Notation preview") },
            { "checkable", true }, { "checked", useNotationPreview() }, { "enabled", true } },

        { }, // separator

        { { "title", muse::qtrc("notation/percussion",  "%1 columns").arg(m_padListModel->numColumns()) },
            { "subitems", columnsSubmenu }, { "enabled", true } },

        { { "id", EDIT_LAYOUT_CODE },
            { "title", editLayoutTitle }, { "icon", editLayoutIcon }, { "enabled", true } },

        { { "id", RESET_LAYOUT_CODE },
            { "title", muse::qtrc("notation/percussion", "Reset layout") }, { "icon", resetLayoutIcon }, { "enabled", true } },
    };

    return menuItems;
}

QVariantMap PercussionPanelModel::createColumnSubItem(int numColumns) const
{
    const QString title = QString::number(numColumns);
    const QString id = SET_COLUMNS_CODE + "-" + title;
    return { { "id", id }, { "title", title },
        { "checkable", true }, { "checked", m_padListModel->numColumns() == numColumns }, { "enabled", true } };
}

void PercussionPanelModel::handleMenuItem(const QString& itemId)
{
    if (itemId == PAD_NAMES_CODE) {
        setUseNotationPreview(false);
    } else if (itemId == NOTATION_PREVIEW_CODE) {
        setUseNotationPreview(true);
    } else if (itemId == EDIT_LAYOUT_CODE) {
        if (m_currentPanelMode == PanelMode::Mode::EDIT_LAYOUT) {
            finishEditing();
            m_padListModel->focusFirstActivePad();
            return;
        }
        setCurrentPanelMode(PanelMode::Mode::EDIT_LAYOUT);
        m_padListModel->padFocusRequested(0);
    } else if (itemId == RESET_LAYOUT_CODE) {
        resetLayout();
    } else if (itemId.contains(SET_COLUMNS_CODE)) {
        //! NOTE: In this case, the last section of the itemId refers to the number of columns
        auto stringSection = itemId.section(u'-', -1, -1);
        const int num = stringSection.toInt();
        setColumns(num);
    }
}

void PercussionPanelModel::finishEditing(bool discardChanges)
{
    setCurrentPanelMode(m_panelModeToRestore);

    if (!interaction()) {
        //! NOTE: Can happen if we close the project while editing the layout...
        setDrumset(nullptr);
        return;
    }

    m_padListModel->removeEmptyRows();

    const std::pair<Instrument*, Part*> instAndPart = getCurrentInstrumentAndPart();
    Instrument* inst = instAndPart.first;
    Part* part = instAndPart.second;

    if (discardChanges) {
        setDrumset(inst ? inst->drumset() : nullptr);
        return;
    }

    IF_ASSERT_FAILED(inst && inst->drumset() && part) {
        return;
    }

    Drumset updatedDrumset = *m_padListModel->drumset();

    for (int i = 0; i < m_padListModel->padList().size(); ++i) {
        const PercussionPanelPadModel* model = m_padListModel->padList().at(i);
        if (!model) {
            continue;
        }

        const int row = i / m_padListModel->numColumns();
        const int column = i % m_padListModel->numColumns();

        engraving::DrumInstrument& drum = updatedDrumset.drum(model->pitch());

        drum.panelRow = row;
        drum.panelColumn = column;

        drum.shortcut = model->keyboardShortcut();
    }

    // Return if nothing changed after edit...
    if (*inst->drumset() == updatedDrumset) {
        setCurrentPanelMode(m_panelModeToRestore);
        m_padListModel->focusLastActivePad();
        return;
    }

    const InstrumentKey key = { inst->id(), part->id() };
    notation()->parts()->replaceDrumset(key, updatedDrumset);

    m_padListModel->focusLastActivePad();
}

//! NOTE: the Edit pad dialog, on the panel's first pad (a new one in the first slot of an empty kit)
void PercussionPanelModel::customizeKit()
{
    for (const PercussionPanelPadModel* pad : m_padListModel->padList()) {
        if (pad) {
            onEditPadRequested(pad->pitch());
            return;
        }
    }

    editEmptyPad(0);
}

//! NOTE: the pad or empty slot selected in the panel, else the pad of the note selected in the score (of this instrument),
//! else the first pad
void PercussionPanelModel::editSelectedPad()
{
    const Drumset* drumset = m_padListModel->drumset();
    if (!drumset || !interaction()) {
        return;
    }

    // selected in the panel: a pad, or an empty slot (a new pad there)
    const bool selection = hasSelectionInCurrentKit();
    if (selection && m_selectedPadPitch >= 0 && drumset->isValid(m_selectedPadPitch)) {
        onEditPadRequested(m_selectedPadPitch);
        return;
    }

    const int selectedSlot = padIndex(-1, m_selectedPadRow, m_selectedPadColumn);
    const QList<PercussionPanelPadModel*> pads = m_padListModel->padList();
    if (selection && selectedSlot >= 0 && selectedSlot < pads.size() && !pads.at(selectedSlot)) {
        editEmptyPad(selectedSlot);
        return;
    }

    const Part* part = getCurrentInstrumentAndPart().second;
    for (const Note* note : interaction()->selection()->notes()) {
        if (note->part() == part && drumset->isValid(note->pitch())) {
            onEditPadRequested(note->pitch());
            return;
        }
    }

    customizeKit();
}

void PercussionPanelModel::onCurrentNotationChanged()
{
    // another score's kit: nothing selected in it
    m_selectedPadPitch = -1;
    m_selectedPadRow = -1;
    m_selectedPadColumn = -1;

    const auto updatePadModels = [this](Drumset* drumset) {
        emit notationPreviewNumStaffLinesChanged();

        if (drumset && m_padListModel->drumset() && *drumset == *m_padListModel->drumset()) {
            return;
        }

        if (m_currentPanelMode == PanelMode::Mode::EDIT_LAYOUT) {
            finishEditing(/*discardChanges*/ true);
        } else {
            setDrumset(drumset);
        }

        updateSoundTitle(currentTrackId());
    };

    if (!notation()) {
        updatePadModels(nullptr);
        return;
    }

    const INotationNoteInputPtr noteInput = interaction()->noteInput();
    updatePadModels(noteInput->state().drumset());

    noteInput->stateChanged().onNotify(this, [this, updatePadModels]() {
        if (!notation()) {
            updatePadModels(nullptr);
            return;
        }
        const INotationNoteInputPtr ni = interaction()->noteInput();
        updatePadModels(ni->state().drumset());
    }, Asyncable::Mode::SetReplace /* FIXME */);

    if (audioSettings()) {
        audioSettings()->trackInputParamsChanged().onReceive(this, [this](InstrumentTrackId trackId) {
            if (trackId != currentTrackId()) {
                return;
            }
            updateSoundTitle(trackId);
        }, Asyncable::Mode::SetReplace /* FIXME */);
    }
}

//! NOTE: the pads' notes named as the instrument's articulation map names its keyswitches
int PercussionPanelModel::noteNamesMiddleCOctave() const
{
    const std::pair<Instrument*, Part*> instAndPart = getCurrentInstrumentAndPart();
    if (!instAndPart.first || !instAndPart.second) {
        return drumkit::noteNamesMiddleCOctave(nullptr);
    }

    return drumkit::noteNamesMiddleCOctave(drumkit::articulationMap(projectSession()->masterNotation(),
                                                                    { instAndPart.second->id(), instAndPart.first->id() }));
}

void PercussionPanelModel::setDrumset(engraving::Drumset* drumset)
{
    m_padListModel->setNoteNamesMiddleCOctave(noteNamesMiddleCOctave());
    m_padListModel->setDrumset(drumset);

    // If drumset contained drums with undefined values for panelRow/panelColumn, m_padListModel will have
    // assigned them now (see PercussionPanelPadListModel::load). In this case, we should update the score's
    // drumset so that it matches the one in the panel...

    const std::pair<Instrument*, Part*> instAndPart = getCurrentInstrumentAndPart();
    Instrument* inst = instAndPart.first;

    const Drumset* instDrumset = inst ? inst->drumset() : nullptr;
    const Drumset* panelDrumset = m_padListModel->drumset();

    if (instDrumset && panelDrumset && *instDrumset != *panelDrumset) {
        inst->setDrumset(panelDrumset);
    }
}

void PercussionPanelModel::updateSoundTitle(const InstrumentTrackId& trackId)
{
    if (!trackId.isValid()) {
        setSoundTitle(QString());
        return;
    }

    const audio::AudioInputParams& params = audioSettings()->trackInputParams(trackId);
    const QString name = muse::audio::audioSourceName(params).toQString();
    if (name.isEmpty()) {
        setSoundTitle(QString());
        return;
    }

    const QString pack = muse::audio::audioSourcePackName(params).toQString();
    if (!pack.isEmpty() && pack != name) {
        setSoundTitle(pack + ": " + name);
        return;
    }

    const QString category = muse::audio::audioSourceCategoryName(params).toQString();
    if (!category.isEmpty() && category != name) {
        setSoundTitle(category + ": " + name);
        return;
    }

    setSoundTitle(name);
}

bool PercussionPanelModel::eventFilter(QObject* watched, QEvent* event)
{
    // Finish editing on escape...
    if (m_currentPanelMode != PanelMode::Mode::EDIT_LAYOUT || event->type() != QEvent::Type::ShortcutOverride
        || m_padListModel->swapInProgress()) {
        return QObject::eventFilter(watched, event);
    }
    QKeyEvent* keyEvent = dynamic_cast<QKeyEvent*>(event);
    if (!keyEvent || keyEvent->key() != Qt::Key_Escape) {
        return QObject::eventFilter(watched, event);
    }
    finishEditing();
    event->setAccepted(true);
    return true;
}

//! NOTE: like the Piano keyboard panel: a pad writes its note only while entering notes (whatever the input method),
//! otherwise it's only heard - the panel never starts note input itself
void PercussionPanelModel::onPadTriggered(int pitch, const PercussionPanelPadModel::PadAction& action)
{
    if (currentPanelMode() == PanelMode::Mode::EDIT_LAYOUT || !interaction()) {
        return;
    }

    m_selectedPadPitch = pitch;
    m_selectedPadRow = -1;
    m_selectedPadColumn = -1;
    m_selectedPadPartId = getCurrentInstrumentAndPart().second ? getCurrentInstrumentAndPart().second->id() : muse::ID();
    updateEditedPadIndex();

    if (interaction()->noteInput()->isNoteInputMode()) {
        writePitch(pitch, WRITE_ACTION_MAP.at(action));
    } else {
        playPitch(pitch);
    }

    // the Edit pad dialog, open, follows the pads clicked
    if (interactive()->isOpened(muse::Uri(EDIT_DRUM_PAD_URI)).val) {
        muse::rcommand::CommandQuery query(PERCUSSION_PANEL_EDIT_PAD_COMMAND);
        query.set("pitch", muse::Val(pitch));
        query.set("panel_context", muse::Val(true));
        commandDispatcher()->dispatch(query);
    }
}

void PercussionPanelModel::onDuplicatePadRequested(int pitch)
{
    const std::pair<Instrument*, Part*> instAndPart = getCurrentInstrumentAndPart();
    Instrument* inst = instAndPart.first;
    Part* part = instAndPart.second;

    IF_ASSERT_FAILED(inst && part) {
        return;
    }

    const int nextAvailablePitch = m_padListModel->nextAvailablePitch(pitch);
    if (nextAvailablePitch < 0) {
        // TODO: Show some sort of warning dialog?
        LOGE() << "No space to duplicate drum pad";
        return;
    }
    const int nextAvailableIndex = m_padListModel->nextAvailableIndex(pitch);

    Drumset updatedDrumset = *m_padListModel->drumset();

    engraving::DrumInstrument duplicateDrum = updatedDrumset.drum(pitch);

    duplicateDrum.shortcut.clear(); // Don't steal the shortcut
    duplicateDrum.panelRow = nextAvailableIndex / m_padListModel->numColumns();
    duplicateDrum.panelColumn = nextAvailableIndex % m_padListModel->numColumns();

    updatedDrumset.setDrum(nextAvailablePitch, duplicateDrum);

    const InstrumentKey key = { inst->id(), part->id() };
    notation()->parts()->replaceDrumset(key, updatedDrumset);
}

void PercussionPanelModel::onDeletePadRequested(int pitch)
{
    const std::pair<Instrument*, Part*> instAndPart = getCurrentInstrumentAndPart();
    Instrument* inst = instAndPart.first;
    Part* part = instAndPart.second;

    IF_ASSERT_FAILED(inst && part) {
        return;
    }

    Drumset updatedDrumset = *m_padListModel->drumset();
    updatedDrumset.setDrum(pitch, engraving::DrumInstrument());

    const InstrumentKey key = { inst->id(), part->id() };
    notation()->parts()->replaceDrumset(key, updatedDrumset);
}

void PercussionPanelModel::onDefinePadShortcutRequested(int pitch)
{
    const std::pair<Instrument*, Part*> instAndPart = getCurrentInstrumentAndPart();
    Instrument* inst = instAndPart.first;
    Part* part = instAndPart.second;
    IF_ASSERT_FAILED(inst && part) {
        return;
    }

    Drumset updatedDrumset = *m_padListModel->drumset();
    if (!PercussionUtilities(iocContext()).editPercussionShortcut(updatedDrumset, pitch)) {
        return;
    }

    const InstrumentKey key = { inst->id(), part->id() };
    notation()->parts()->replaceDrumset(key, updatedDrumset);
}

void PercussionPanelModel::onEditPadRequested(int pitch)
{
    openEditPadDialog(pitch, -1, -1);
}

//! NOTE: a single dialog: already open (for another pad), it's raised
void PercussionPanelModel::openEditPadDialog(int pitch, int newPadRow, int newPadColumn)
{
    muse::rcommand::CommandQuery query(OPEN_EDIT_DRUM_PAD_COMMAND);
    query.set("panel_context", muse::Val(true));
    if (pitch >= 0) {
        query.set("pitch", muse::Val(pitch));
    }
    if (newPadRow >= 0 && newPadColumn >= 0) {
        query.set("panel_row", muse::Val(newPadRow));
        query.set("panel_column", muse::Val(newPadColumn));
    }

    commandDispatcher()->dispatch(query);
}

//! NOTE: the new pad gets a free pitch until its sound is chosen in the dialog
void PercussionPanelModel::editEmptyPad(int padIndex)
{
    if (!m_padListModel->drumset()) {
        return;
    }

    const int freePitch = m_padListModel->nextAvailablePitch(engraving::DRUM_INSTRUMENTS - 1);
    if (freePitch < 0) {
        LOGE() << "No free pitch for a new drum pad";
        return;
    }

    openEditPadDialog(freePitch, padIndex / m_padListModel->numColumns(), padIndex % m_padListModel->numColumns());
}

//! NOTE: selected (for "Edit pad"); the Edit pad dialog, open, follows the slots clicked too: a new pad there
void PercussionPanelModel::onEmptyPadClicked(int padIndex)
{
    if (currentPanelMode() == PanelMode::Mode::EDIT_LAYOUT) {
        return;
    }

    m_selectedPadPitch = -1;
    m_selectedPadRow = padIndex / m_padListModel->numColumns();
    m_selectedPadColumn = padIndex % m_padListModel->numColumns();
    m_selectedPadPartId = getCurrentInstrumentAndPart().second ? getCurrentInstrumentAndPart().second->id() : muse::ID();
    updateEditedPadIndex();

    if (interactive()->isOpened(muse::Uri(EDIT_DRUM_PAD_URI)).val) {
        editEmptyPad(padIndex);
    }
}

int PercussionPanelModel::editedPadIndex() const
{
    return m_editedPadIndex;
}

//! NOTE: an existing pad by its pitch, a new one by its position
int PercussionPanelModel::padIndex(int pitch, int row, int column) const
{
    if (pitch >= 0) {
        const QList<PercussionPanelPadModel*> pads = m_padListModel->padList();
        for (int i = 0; i < pads.size(); ++i) {
            if (pads.at(i) && pads.at(i)->pitch() == pitch) {
                return i;
            }
        }
    } else if (row >= 0 && column >= 0) {
        return row * m_padListModel->numColumns() + column;
    }

    return -1;
}

//! NOTE: the layout being edited is applied first: the Edit pad dialog then applies its pad, which would discard it
int PercussionPanelModel::adjacentPadPitch(const muse::rcommand::Params& params)
{
    if (m_currentPanelMode == PanelMode::Mode::EDIT_LAYOUT) {
        finishEditing();
    }

    const bool newPad = params.contains("panel_row") && params.contains("panel_column");
    const int index = padIndex(params.at("pitch").toInt(),
                               newPad ? params.at("panel_row").toInt() : -1,
                               newPad ? params.at("panel_column").toInt() : -1);
    const int step = params.at("step").toInt() < 0 ? -1 : 1;

    const QList<PercussionPanelPadModel*> pads = m_padListModel->padList();
    const int count = static_cast<int>(pads.size());
    if (count == 0) {
        return -1;
    }

    // the empty slots are skipped
    int i = index >= 0 ? index : (step > 0 ? -1 : count);
    for (int n = 0; n < count; ++n) {
        i = ((i + step) % count + count) % count;
        if (pads.at(i)) {
            return pads.at(i)->pitch();
        }
    }

    return -1;
}

//! NOTE: the panel shows another instrument's kit (note input moved): the selection made in the other one doesn't apply
bool PercussionPanelModel::hasSelectionInCurrentKit() const
{
    const Part* part = getCurrentInstrumentAndPart().second;
    return part && part->id() == m_selectedPadPartId;
}

void PercussionPanelModel::updateEditedPadIndex()
{
    const bool editing = m_editedPadPitch >= 0 || (m_editedPadRow >= 0 && m_editedPadColumn >= 0);
    int index = -1;
    if (editing) {
        index = padIndex(m_editedPadPitch, m_editedPadRow, m_editedPadColumn);
    } else if (hasSelectionInCurrentKit()) {
        index = padIndex(m_selectedPadPitch, m_selectedPadRow, m_selectedPadColumn);
    }

    if (m_editedPadIndex == index) {
        return;
    }

    m_editedPadIndex = index;
    emit editedPadIndexChanged();
}

void PercussionPanelModel::writePitch(int pitch, const NoteAddingMode& addingMode)
{
    INotationUndoStackPtr undoStack = notation()->undoStack();
    if (!interaction() || !undoStack) {
        return;
    }

    muse::rcommand::CommandQuery query(ADD_DRUM_NOTE_COMMAND);
    query.set("pitch", muse::Val(pitch));
    query.set("mode", muse::Val(str_conv(addingMode)));
    commandDispatcher()->dispatch(query);
}

void PercussionPanelModel::playPitch(int pitch)
{
    if (!interaction()) {
        return;
    }

    const NoteInputState& inputState = interaction()->noteInput()->state();
    std::shared_ptr<Chord> chord = PercussionUtilities(iocContext()).getDrumNoteForPreview(m_padListModel->drumset(), pitch);

    chord->setOwnershipParent(inputState.segment());
    chord->setTrack(inputState.track());

    playbackController()->playElements({ chord.get() });
}

void PercussionPanelModel::resetLayout()
{
    if (m_currentPanelMode == PanelMode::Mode::EDIT_LAYOUT) {
        finishEditing(/*discardChanges*/ true);
    }

    const std::pair<Instrument*, Part*> instAndPart = getCurrentInstrumentAndPart();
    Instrument* inst = instAndPart.first;
    Part* part = instAndPart.second;

    IF_ASSERT_FAILED(audioSettings() && inst && part) {
        return;
    }

    const muse::audio::AudioResourceMeta& resourceMeta = audioSettings()->trackInputParams(currentTrackId()).resourceMeta;
    const bool isMuseSamplerDrumset = muse::audio::isResourceType(resourceMeta,
                                                                  muse::audio::AudioResourceType::MuseSamplerSoundPack);

    Drumset defaultDrumset = isMuseSamplerDrumset ? museSamplerDefaultDrumset() : standardDefaultDrumset();

    Drumset defaultLayout = m_padListModel->constructDefaultLayout(defaultDrumset);
    if (defaultLayout == *m_padListModel->drumset()) {
        // Nothing changed after reset...
        return;
    }

    const InstrumentKey key = { inst->id(), part->id() };
    notation()->parts()->replaceDrumset(key, defaultLayout);
}

Drumset PercussionPanelModel::standardDefaultDrumset() const
{
    const std::pair<Instrument*, Part*> instAndPart = getCurrentInstrumentAndPart();
    const Instrument* inst = instAndPart.first;
    const Part* part = instAndPart.second;

    IF_ASSERT_FAILED(inst && inst->drumset() && part) {
        return Drumset();
    }

    const InstrumentTemplate& instTemplate = instrumentsRepository()->instrumentTemplate(inst->id());
    IF_ASSERT_FAILED(instTemplate.drumset) {
        return Drumset();
    }

    return *instTemplate.drumset;
}

Drumset PercussionPanelModel::museSamplerDefaultDrumset() const
{
    IF_ASSERT_FAILED(audioSettings()) {
        return Drumset();
    }

    const muse::audio::AudioResourceMeta& resourceMeta = audioSettings()->trackInputParams(currentTrackId()).resourceMeta;

    const int instrumentId = muse::audio::intAttribute(resourceMeta, u"museUID");

    const muse::ByteArray drumMapping = museSampler()->drumMapping(instrumentId);
    IF_ASSERT_FAILED(!drumMapping.empty()) {
        return Drumset();
    }

    Drumset defaultDrumset;
    PercussionUtilities(iocContext()).readDrumset(drumMapping, defaultDrumset);

    return defaultDrumset;
}

void PercussionPanelModel::setColumns(int numColumns)
{
    IF_ASSERT_FAILED(numColumns > 0) {
        return;
    }

    if (numColumns == m_padListModel->numColumns()) {
        return;
    }

    if (m_currentPanelMode == PanelMode::Mode::EDIT_LAYOUT) {
        finishEditing(/*discardChanges*/ true);
    }

    const std::pair<Instrument*, Part*> instAndPart = getCurrentInstrumentAndPart();
    Instrument* inst = instAndPart.first;
    Part* part = instAndPart.second;
    IF_ASSERT_FAILED(inst && inst->drumset() && part) {
        return;
    }

    Drumset updatedDrumset = *m_padListModel->drumset();

    // Calculate new values for panelRow & panelColumn...
    for (int pitch = 0; pitch < mu::engraving::DRUM_INSTRUMENTS; ++pitch) {
        if (!updatedDrumset.isValid(pitch)) {
            continue;
        }
        mu::engraving::DrumInstrument& drum = updatedDrumset.drum(pitch);
        const int index = drum.panelRow * static_cast<int>(updatedDrumset.percussionPanelColumns()) + drum.panelColumn;
        drum.panelRow = index / numColumns;
        drum.panelColumn = index % numColumns;
    }

    updatedDrumset.setPercussionPanelColumns(numColumns);

    const InstrumentKey key = { inst->id(), part->id() };
    notation()->parts()->replaceDrumset(key, updatedDrumset);
}

InstrumentTrackId PercussionPanelModel::currentTrackId() const
{
    if (!interaction()) {
        return InstrumentTrackId();
    }

    const NoteInputState& inputState = interaction()->noteInput()->state();
    const Staff* staff = inputState.staff();

    if (!staff || !staff->part() || !inputState.segment()) {
        return InstrumentTrackId();
    }

    return { staff->part()->id(), staff->part()->instrumentId(inputState.segment()->tick()) };
}

std::pair<mu::engraving::Instrument*, mu::engraving::Part*> PercussionPanelModel::getCurrentInstrumentAndPart() const
{
    if (!interaction()) {
        return { nullptr, nullptr };
    }

    const NoteInputState& inputState = interaction()->noteInput()->state();

    const Staff* staff = inputState.staff();

    Part* part = staff ? staff->part() : nullptr;

    if (!inputState.segment()) {
        return { nullptr, part };
    }

    Instrument* inst = part ? part->instrument(inputState.segment()->tick()) : nullptr;

    return { inst, part };
}

const mu::project::IProjectAudioSettingsPtr PercussionPanelModel::audioSettings() const
{
    return projectSession()->project() ? projectSession()->project()->audioSettings() : nullptr;
}

const INotationPtr PercussionPanelModel::notation() const
{
    return projectSession()->currentNotation();
}

const INotationInteractionPtr PercussionPanelModel::interaction() const
{
    return notation() ? notation()->interaction() : nullptr;
}

mu::engraving::Score* PercussionPanelModel::score() const
{
    return notation() ? notation()->elements()->msScore() : nullptr;
}
