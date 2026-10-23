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

#include <QObject>
#include <QColor>
#include <QVariant>
#include <qqmlintegration.h>

#include "async/asyncable.h"
#include "modularity/ioc.h"

#include "project/iprojectsession.h"
#include "interactive/iinteractive.h"
#include "midi/imidiinport.h"
#include "rcommand/icommanddispatcher.h"
#include "rcommand/commandable.h"
#include "global/iglobalconfiguration.h"
#include "global/io/ifilesystem.h"
#include "notation/inotationmidiinput.h"
#include "notation/types/instrumentstypes.h"
#include "playback/iplaybackcontroller.h"

#include "engraving/articulationmap/articulationmaptypes.h"
#include "engraving/dom/drumset.h"
#include "engraving/iengravingfontsprovider.h"

namespace mu::notation {
//! NOTE: the "Edit pad" dialog of the Percussion panel: one pad of the kit, its sound (a note of the sound library,
//! chosen on a keyboard, played or learnt from the MIDI keyboard, and the articulation of the instrument's articulation
//! map it's in) and its appearance. Applied by OK (or when going to another pad), in one undo step
class EditDrumPadModel : public QObject, public muse::Contextable, public muse::async::Asyncable, public muse::rcommand::Commandable
{
    Q_OBJECT
    QML_ELEMENT

    Q_PROPERTY(QString padName READ padName WRITE setPadName NOTIFY padChanged)
    //! NOTE: edits not applied yet (Apply)
    Q_PROPERTY(bool hasChanges READ hasChanges NOTIFY hasChangesChanged)

    Q_PROPERTY(int soundPitch READ soundPitch WRITE setSoundPitch NOTIFY soundChanged)
    Q_PROPERTY(QString soundPitchText READ soundPitchText NOTIFY soundChanged)
    Q_PROPERTY(QString soundInfo READ soundInfo NOTIFY soundChanged)
    Q_PROPERTY(QVariantList keyboardKeys READ keyboardKeys NOTIFY soundChanged)
    //! NOTE: the octave of middle C (60) in the notes' names: the articulation map's (as its keyswitches are named), else 4
    Q_PROPERTY(int middleCOctave READ middleCOctave NOTIFY padChanged)
    Q_PROPERTY(bool learningMidi READ learningMidi WRITE setLearningMidi NOTIFY learningMidiChanged)
    Q_PROPERTY(bool hasInstrumentEditor READ hasInstrumentEditor NOTIFY padChanged)

    Q_PROPERTY(bool hasArticulationMap READ hasArticulationMap NOTIFY padChanged)
    Q_PROPERTY(QVariantList articulationMenuItems READ articulationMenuItems NOTIFY padChanged)
    Q_PROPERTY(QString articulationName READ articulationName NOTIFY padChanged)
    Q_PROPERTY(QString articulationTrigger READ articulationTrigger NOTIFY padChanged)

    Q_PROPERTY(QVariantList noteheadGroups READ noteheadGroups CONSTANT)
    Q_PROPERTY(int noteheadGroup READ noteheadGroup WRITE setNoteheadGroup NOTIFY padChanged)
    Q_PROPERTY(bool customNoteheads READ customNoteheads WRITE setCustomNoteheads NOTIFY padChanged)
    //! NOTE: {text, value, glyph}: the glyph in musicFontFamily
    Q_PROPERTY(QVariantList noteheadSymbols READ noteheadSymbols CONSTANT)
    Q_PROPERTY(QString musicFontFamily READ musicFontFamily CONSTANT)
    //! NOTE: by NoteHeadType: whole, half, quarter, double whole - the group's ones when not custom
    Q_PROPERTY(QVariantList customNoteheadSymbols READ customNoteheadSymbols NOTIFY padChanged)

    Q_PROPERTY(int staffLine READ staffLine WRITE setStaffLine NOTIFY padChanged)
    Q_PROPERTY(int voice READ voice WRITE setVoice NOTIFY padChanged)
    Q_PROPERTY(int stemDirection READ stemDirection WRITE setStemDirection NOTIFY padChanged)
    Q_PROPERTY(QString shortcut READ shortcut NOTIFY padChanged)

    Q_PROPERTY(QVariant previewItem READ previewItem NOTIFY padChanged)
    Q_PROPERTY(int numStaffLines READ numStaffLines NOTIFY padChanged)

    muse::GlobalInject<muse::midi::IMidiInPort> midiInPort;
    muse::GlobalInject<muse::IGlobalConfiguration> globalConfiguration;
    muse::GlobalInject<muse::io::IFileSystem> fileSystem;
    muse::GlobalInject<engraving::IEngravingFontsProvider> engravingFonts;
    muse::ContextInject<project::IProjectSession> projectSession = { this };
    muse::ContextInject<muse::IInteractive> interactive = { this };
    muse::ContextInject<muse::rcommand::ICommandDispatcher> commandDispatcher = { this };
    muse::ContextInject<playback::IPlaybackController> playbackController = { this };

public:
    explicit EditDrumPadModel(QObject* parent = nullptr);
    ~EditDrumPadModel() override;

    //! NOTE: newPadRow/newPadColumn set: padPitch is free, a pad is created there, at this position of the Percussion panel;
    //! padPitch -1: the selected note's pad, else the first one. staffContext: the kit of the staff of the context menu,
    //! else the note input's (the Percussion panel's)
    Q_INVOKABLE void init(int padPitch, int newPadRow, int newPadColumn, bool staffContext = false);

    Q_INVOKABLE void playSound();
    Q_INVOKABLE void openInstrumentEditor();
    Q_INVOKABLE void openArticulationMapEditor();
    Q_INVOKABLE void setArticulation(const QString& entryId);
    Q_INVOKABLE void setCustomNoteheadSymbol(int noteheadType, int symId);
    Q_INVOKABLE void defineShortcut();

    Q_INVOKABLE void editAdjacentPad(int step);
    Q_INVOKABLE void newKit();
    Q_INVOKABLE void loadKit();
    Q_INVOKABLE void saveKit();

    //! NOTE: OK
    Q_INVOKABLE void apply();

    bool hasChanges() const;

    QString padName() const;
    void setPadName(const QString& name);

    int soundPitch() const;
    void setSoundPitch(int pitch);
    QString soundPitchText() const;
    QString soundInfo() const;
    QVariantList keyboardKeys() const;
    int middleCOctave() const;
    QString noteText(int pitch) const;

    bool learningMidi() const;
    void setLearningMidi(bool learning);

    bool hasInstrumentEditor() const;

    bool hasArticulationMap() const;
    QVariantList articulationMenuItems() const;
    QString articulationName() const;
    QString articulationTrigger() const;

    QVariantList noteheadGroups() const;
    int noteheadGroup() const;
    void setNoteheadGroup(int group);
    bool customNoteheads() const;
    void setCustomNoteheads(bool custom);
    QVariantList noteheadSymbols() const;
    QString musicFontFamily() const;
    QVariantList customNoteheadSymbols() const;

    int staffLine() const;
    void setStaffLine(int line);
    int voice() const;
    void setVoice(int voice);
    int stemDirection() const;
    void setStemDirection(int direction);
    QString shortcut() const;

    QVariant previewItem() const;
    int numStaffLines() const;

signals:
    void padChanged();
    void soundChanged();
    void learningMidiChanged();
    void hasChangesChanged();

    //! NOTE: the score is no longer the current one, or the instrument is gone
    void closeRequested();

private:
    bool initDrumsetAndKey(bool staffContext);
    bool resolveInstrumentKey(bool staffContext, InstrumentKey& key, engraving::Drumset& drumset) const;
    void updateMiddleCOctave();
    void finishPanelLayoutEditing();
    int defaultPadPitch() const;
    static int firstFreePitch(const engraving::Drumset& drumset);
    bool hasPad() const;
    engraving::DrumInstrument& pad();
    const engraving::DrumInstrument& pad() const;
    void onPadEdited();

    const engraving::ExpressionMap* articulationMap() const;

    void applyLoadedKit();
    muse::io::path_t kitsDir() const;
    QString defaultKitFileName() const;

    void syncWithScore();
    void syncWithArticulationMap();
    void editPad(int pitch);
    bool resolvePendingChanges();
    void createNewPad(int row, int column);
    //! NOTE: the kit a pad is asked from: the dialog's, a staff's context menu, the Percussion panel's (the note input's)
    enum class KitSource {
        Current,
        Staff,
        NoteInput,
    };

    void switchToPad(int pitch, int newPadRow, int newPadColumn, KitSource kitSource);
    void editDefaultPad();
    int adjacentPadByPosition(int fromPitch, int step) const;
    void notifyEditedPad(bool editing);
    void onMidiNoteLearnt(int pitch);
    void updatePreview();
    void notifySoundChanged();
    QVariantList buildKeyboardKeys() const;

    const engraving::Drumset* scoreDrumset() const;

    INotationPtr m_notation;
    InstrumentKey m_instrumentKey;

    engraving::Drumset m_editedDrumset;
    //! NOTE: the score's drumset as last set or read: a different one in the score was changed elsewhere
    engraving::Drumset m_scoreDrumset;

    int m_padPitch = -1;
    int m_soundPitch = -1;
    int m_newPadRow = -1;
    int m_newPadColumn = -1;

    bool m_learningMidi = false;
    INotationMidiInputPtr m_suspendedMidiInput;

    engraving::ElementPtr m_previewItem;

    engraving::DrumInstrument m_noPad; // see pad()

    int m_middleCOctave = 4;
    QString m_articulationMapText; // as last read: a different one was edited meanwhile
    QVariantList m_keyboardKeys;
};
}
