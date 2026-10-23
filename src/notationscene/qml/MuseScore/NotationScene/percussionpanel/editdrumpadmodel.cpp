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
#include "editdrumpadmodel.h"

#include <QRegularExpression>

#include "global/io/buffer.h"
#include "global/io/file.h"
#include "translation.h"

#include "engraving/articulationmap/articulationmapdata.h"
#include "engraving/articulationmap/articulationmapparser.h"
#include "engraving/articulationmap/articulationmapwriter.h"
#include "engraving/dom/chord.h"
#include "engraving/dom/instrument.h"
#include "engraving/dom/note.h"
#include "engraving/dom/part.h"
#include "engraving/dom/score.h"
#include "engraving/dom/segment.h"
#include "engraving/dom/staff.h"
#include "engraving/infrastructure/smufl.h"
#include "engraving/rw/xmlreader.h"
#include "engraving/rw/xmlwriter.h"
#include "engraving/types/constants.h"
#include "engraving/types/symnames.h"
#include "engraving/types/typesconv.h"

#include "notation/imasternotation.h"
#include "notation/inotation.h"
#include "notation/inotationarticulationmaps.h"
#include "notation/inotationelements.h"
#include "notation/inotationinteraction.h"
#include "notation/inotationnoteinput.h"
#include "notation/inotationparts.h"
#include "notation/inotationselection.h"
#include "notation/inotationundostack.h"

#include "notationscene/notationcommands.h"
#include "notationscene/utilities/percussionutilities.h"

#include "../instrumenteditor.h"
#include "drumkitarticulationmap.h"

using namespace mu::notation;
using namespace mu::engraving;
using namespace muse;

//! NOTE: "Sol" and "Alt. Brevis" omitted, as not being useful for drums; the custom group is the "Custom noteheads" option
static const NoteHeadGroup DRUM_PAD_NOTEHEAD_GROUPS[] = {
    NoteHeadGroup::HEAD_NORMAL,
    NoteHeadGroup::HEAD_CROSS,
    NoteHeadGroup::HEAD_PLUS,
    NoteHeadGroup::HEAD_XCIRCLE,
    NoteHeadGroup::HEAD_WITHX,
    NoteHeadGroup::HEAD_TRIANGLE_UP,
    NoteHeadGroup::HEAD_TRIANGLE_DOWN,
    NoteHeadGroup::HEAD_SLASH,
    NoteHeadGroup::HEAD_SLASHED1,
    NoteHeadGroup::HEAD_SLASHED2,
    NoteHeadGroup::HEAD_DIAMOND,
    NoteHeadGroup::HEAD_DIAMOND_OLD,
    NoteHeadGroup::HEAD_CIRCLED,
    NoteHeadGroup::HEAD_CIRCLED_LARGE,
    NoteHeadGroup::HEAD_LARGE_ARROW,
    NoteHeadGroup::HEAD_DO,
    NoteHeadGroup::HEAD_RE,
    NoteHeadGroup::HEAD_MI,
    NoteHeadGroup::HEAD_FA,
    NoteHeadGroup::HEAD_LA,
    NoteHeadGroup::HEAD_TI,
    NoteHeadGroup::HEAD_SWISS_RUDIMENTS_FLAM,
    NoteHeadGroup::HEAD_SWISS_RUDIMENTS_DOUBLE,
};

static const NoteHeadType DRUM_PAD_NOTEHEAD_TYPES[] = {
    NoteHeadType::HEAD_WHOLE, NoteHeadType::HEAD_HALF, NoteHeadType::HEAD_QUARTER, NoteHeadType::HEAD_BREVIS
};

EditDrumPadModel::EditDrumPadModel(QObject* parent)
    : QObject(parent), muse::Contextable(muse::iocCtxForQmlObject(this))
{
}

EditDrumPadModel::~EditDrumPadModel()
{
    setLearningMidi(false);

    if (m_padPitch >= 0) {
        notifyEditedPad(/*editing*/ false);
    }
}

void EditDrumPadModel::init(int padPitch, int newPadRow, int newPadColumn, bool staffContext)
{
    m_notation = projectSession()->currentNotation();
    if (!m_notation || padPitch >= DRUM_INSTRUMENTS || !initDrumsetAndKey(staffContext)) {
        emit closeRequested();
        return;
    }

    const bool newPad = newPadRow >= 0 && newPadColumn >= 0;
    if (padPitch < 0 && !newPad) {
        padPitch = defaultPadPitch();
    }

    // an empty kit: a new pad in the first slot
    if (padPitch < 0) {
        padPitch = firstFreePitch(m_editedDrumset);
        newPadRow = 0;
        newPadColumn = 0;
    }

    if (padPitch < 0) {
        emit closeRequested();
        return;
    }

    m_padPitch = padPitch;

    if (newPadRow >= 0 && newPadColumn >= 0 && !m_editedDrumset.isValid(padPitch)) {
        createNewPad(newPadRow, newPadColumn);
    }

    if (!m_editedDrumset.isValid(padPitch)) {
        emit closeRequested();
        return;
    }

    m_soundPitch = m_editedDrumset.playedPitch(padPitch);

    // a pad clicked in the Percussion panel
    commandDispatcher()->onRequest(this, PERCUSSION_PANEL_EDIT_PAD_COMMAND, [this](const muse::rcommand::Params& params) {
        const bool newPad = params.contains("panel_row") && params.contains("panel_column");
        KitSource kitSource = KitSource::Current;
        if (params.contains("staff_context") && params.at("staff_context").toBool()) {
            kitSource = KitSource::Staff;
        } else if (params.contains("panel_context") && params.at("panel_context").toBool()) {
            kitSource = KitSource::NoteInput;
        }

        switchToPad(params.at("pitch").toInt(), newPad ? params.at("panel_row").toInt() : -1,
                    newPad ? params.at("panel_column").toInt() : -1, kitSource);
        return muse::make_ok();
    });

    m_notation->undoStack()->stackChanged().onNotify(this, [this]() {
        syncWithScore();
        syncWithArticulationMap();
    });

    const ExpressionMap* map = articulationMap();
    m_articulationMapText = map ? map->sourceText.toQString() : QString();

    projectSession()->currentNotationChanged().onNotify(this, [this]() {
        if (projectSession()->currentNotation() != m_notation) {
            m_notation = nullptr;
            emit closeRequested();
        }
    });

    midiInPort()->eventReceived().onReceive(this, [this](const muse::midi::tick_t, const muse::midi::Event& event) {
        if (!m_learningMidi) {
            return;
        }

        const std::vector<muse::midi::Event> events = event.isChannelVoice20() ? event.toMIDI10() : std::vector { event };
        for (const muse::midi::Event& e : events) {
            if (e.opcode() == muse::midi::Event::Opcode::NoteOn && e.velocity7() > 0) {
                onMidiNoteLearnt(e.note());
                return;
            }
        }
    });

    updateMiddleCOctave();
    onPadEdited();
    notifySoundChanged();
    notifyEditedPad(/*editing*/ true);
}

//! NOTE: the instrument of the Percussion panel: the note input's, else the clicked element's
bool EditDrumPadModel::initDrumsetAndKey(bool staffContext)
{
    if (!resolveInstrumentKey(staffContext, m_instrumentKey, m_editedDrumset)) {
        return false;
    }

    m_scoreDrumset = m_editedDrumset;
    return true;
}

bool EditDrumPadModel::resolveInstrumentKey(bool staffContext, InstrumentKey& key, Drumset& drumset) const
{
    const INotationInteractionPtr interaction = m_notation->interaction();
    const NoteInputState& state = interaction->noteInput()->state();
    const INotationInteraction::HitElementContext context = interaction->hitElementContext();

    const bool noteInputValid = state.staff() && state.segment();
    const bool hitContextValid = context.staff && context.element;
    if (!noteInputValid && !hitContextValid) {
        return false;
    }

    const bool useHitContext = hitContextValid && (staffContext || !noteInputValid);
    Part* part = useHitContext ? context.staff->part() : state.staff()->part();
    const Fraction tick = useHitContext ? context.element->tick() : state.segment()->tick();
    const Instrument* instrument = part ? part->instrument(tick) : nullptr;
    if (!instrument || !instrument->useDrumset() || !instrument->drumset()) {
        return false;
    }

    key = { instrument->id(), part->id(), tick };
    drumset = *instrument->drumset();

    return true;
}

//! NOTE: the pad of the note selected in the score (of this instrument), else the first one of the Percussion panel
int EditDrumPadModel::defaultPadPitch() const
{
    for (const Note* note : m_notation->interaction()->selection()->notes()) {
        if (note->part() && note->part()->id() == m_instrumentKey.partId && m_editedDrumset.isValid(note->pitch())) {
            return note->pitch();
        }
    }

    return adjacentPadByPosition(-1, 1);
}

int EditDrumPadModel::firstFreePitch(const Drumset& drumset)
{
    for (int pitch = 0; pitch < DRUM_INSTRUMENTS; ++pitch) {
        if (!drumset.isValid(pitch)) {
            return pitch;
        }
    }

    return -1;
}

const Drumset* EditDrumPadModel::scoreDrumset() const
{
    if (!m_notation) {
        return nullptr;
    }

    const Part* part = m_notation->parts()->part(m_instrumentKey.partId);
    const Instrument* instrument = part ? part->instrument(m_instrumentKey.tick) : nullptr;
    if (!instrument || instrument->id() != m_instrumentKey.instrumentId) {
        return nullptr;
    }

    return instrument->drumset();
}

bool EditDrumPadModel::hasPad() const
{
    return m_padPitch >= 0 && m_padPitch < DRUM_INSTRUMENTS;
}

//! NOTE: QML reads the properties before init(): an empty drum until then
DrumInstrument& EditDrumPadModel::pad()
{
    return hasPad() ? m_editedDrumset.drum(m_padPitch) : m_noPad;
}

const DrumInstrument& EditDrumPadModel::pad() const
{
    return hasPad() ? m_editedDrumset.drum(m_padPitch) : m_noPad;
}

void EditDrumPadModel::onPadEdited()
{
    if (!hasPad()) {
        return;
    }

    updatePreview();
    emit padChanged();
    emit hasChangesChanged();
}

bool EditDrumPadModel::hasChanges() const
{
    const Drumset* drumset = scoreDrumset();
    return hasPad() && drumset && m_editedDrumset != *drumset;
}

void EditDrumPadModel::updatePreview()
{
    if (!hasPad()) {
        m_previewItem = nullptr;
        return;
    }

    std::shared_ptr<Chord> chord = PercussionUtilities(iocContext()).getDrumNoteForPreview(&m_editedDrumset, m_padPitch);

    //! NOTE: as the Customize kit dialog's example: not marked (a marked note is drawn as selected, in its voice's color)
    for (Note* note : chord->notes()) {
        note->setMark(false);
    }

    m_previewItem = chord;
}

// ---- sound

int EditDrumPadModel::soundPitch() const
{
    return m_soundPitch;
}

//! NOTE: the note the pad plays (see DrumInstrument::playedPitch): its pitch in the score doesn't change
//! NOTE: the note the pad plays (see DrumInstrument::playedPitch). A new pad (not applied yet) is also given it as its pitch
//! in the score when it's free: it then gets back the notes already written there (e.g. after New kit) and MIDI input
void EditDrumPadModel::setSoundPitch(int pitch)
{
    if (pitch < 0 || pitch >= DRUM_INSTRUMENTS || !hasPad()) {
        return;
    }

    if (m_soundPitch != pitch) {
        m_soundPitch = pitch;

        const bool newPad = m_newPadRow >= 0 && m_newPadColumn >= 0;
        if (newPad && pitch != m_padPitch && !m_editedDrumset.isValid(pitch)) {
            DrumInstrument drum = pad();
            drum.playedPitch = -1;
            m_editedDrumset.setDrum(m_padPitch, DrumInstrument());
            m_padPitch = pitch;
            m_editedDrumset.setDrum(pitch, drum);
            onPadEdited();
        } else {
            pad().playedPitch = pitch == m_padPitch ? -1 : pitch;
        }

        notifySoundChanged();
    }

    playSound();
}

QString EditDrumPadModel::soundPitchText() const
{
    if (m_soundPitch < 0) {
        return QString();
    }

    return noteText(m_soundPitch);
}

//! NOTE: only a real duplicate: another pad playing the same note in the same articulation (in another one, it's
//! another sound of the library)
QString EditDrumPadModel::soundInfo() const
{
    if (!hasPad() || m_soundPitch < 0) {
        return QString();
    }

    for (int pitch = 0; pitch < DRUM_INSTRUMENTS; ++pitch) {
        if (pitch != m_padPitch && m_editedDrumset.isValid(pitch) && m_editedDrumset.playedPitch(pitch) == m_soundPitch
            && m_editedDrumset.articulationMapEntry(pitch) == pad().articulationMapEntry) {
            return muse::qtrc("notation/percussion", "“%1” already plays this note in this articulation.")
                   .arg(m_editedDrumset.translatedName(pitch).toQString());
        }
    }

    return QString();
}

//! NOTE: per pitch: the other pad that plays it (if any), and whether it's a keyswitch of the articulation map
int EditDrumPadModel::middleCOctave() const
{
    return m_middleCOctave;
}

//! NOTE: the map's text parsed for its middle C only when the map changes
void EditDrumPadModel::updateMiddleCOctave()
{
    m_middleCOctave = drumkit::noteNamesMiddleCOctave(articulationMap());
}

//! NOTE: the keyboard computed once per change (each key reads it)
void EditDrumPadModel::notifySoundChanged()
{
    m_keyboardKeys = buildKeyboardKeys();
    emit soundChanged();
    emit hasChangesChanged();
}

QVariantList EditDrumPadModel::keyboardKeys() const
{
    return m_keyboardKeys;
}

//! NOTE: e.g. "C3 (60)"
QString EditDrumPadModel::noteText(int pitch) const
{
    return QString("%1 (%2)").arg(ArticulationMapWriter::noteName(pitch, middleCOctave()).toQString()).arg(pitch);
}

//! NOTE: per key: whether another pad plays it in the same articulation (the bank the pad is in), whether it triggers the
//! pad's articulation (only a keyswitch: an articulation may be triggered by CCs...), and what plays it, for its tooltip
QVariantList EditDrumPadModel::buildKeyboardKeys() const
{
    if (!hasPad()) {
        return {};
    }

    std::map<int, QStringList> details; // by pitch: what plays it, what it triggers
    std::set<int> padKeyswitches;
    if (const ExpressionMap* map = articulationMap()) {
        for (const ExpressionMapEntry& entry : map->entries) {
            for (const muse::mpe::MidiMessage& message : entry.messages) {
                if (message.type != muse::mpe::MidiMessage::Type::Note) {
                    continue;
                }

                details[message.number] << muse::qtrc("notation/percussion", "Keyswitch of “%1”").arg(entry.id.toQString());
                if (!pad().articulationMapEntry.empty() && entry.id == pad().articulationMapEntry) {
                    padKeyswitches.insert(message.number);
                }
            }
        }
    }

    std::set<int> sameArticulationPads;
    for (int pitch = 0; pitch < DRUM_INSTRUMENTS; ++pitch) {
        if (pitch == m_padPitch || !m_editedDrumset.isValid(pitch)) {
            continue;
        }

        const int played = m_editedDrumset.playedPitch(pitch);
        const String& articulation = m_editedDrumset.articulationMapEntry(pitch);
        if (articulation == pad().articulationMapEntry) {
            sameArticulationPads.insert(played);
        }

        const QString name = m_editedDrumset.translatedName(pitch).toQString();
        details[played] << (articulation.empty() ? name : QString("%1 (%2)").arg(name, articulation.toQString()));
    }

    QVariantList keys;
    for (int pitch = 0; pitch < DRUM_INSTRUMENTS; ++pitch) {
        keys << QVariantMap {
            { "sameArticulationPad", sameArticulationPads.count(pitch) > 0 },
            { "padKeyswitch", padKeyswitches.count(pitch) > 0 },
            { "detail", details.count(pitch) > 0 ? details.at(pitch).join(" · ") : QString() },
            { "noteText", noteText(pitch) },
            };
    }

    return keys;
}

void EditDrumPadModel::playSound()
{
    if (!m_notation || !hasPad() || m_soundPitch < 0) {
        return;
    }

    Score* score = m_notation->elements()->msScore();
    Part* part = score ? score->partById(m_instrumentKey.partId) : nullptr;
    Instrument* instrument = part ? part->instrument(m_instrumentKey.tick) : nullptr;
    Drumset* instrumentDrumset = instrument && instrument->useDrumset() ? instrument->drumset() : nullptr;
    if (!instrumentDrumset) {
        return;
    }

    // the pad itself: the playback plays its note, with its articulation's keyswitch
    std::shared_ptr<Chord> chord = PercussionUtilities(iocContext()).getDrumNoteForPreview(&m_editedDrumset, m_padPitch);

    const NoteInputState& state = m_notation->interaction()->noteInput()->state();
    if (state.segment()) {
        chord->setOwnershipParent(state.segment());
    } else {
        chord->setOwnershipParent(score->firstSegment(SegmentType::ChordRest));
    }

    chord->setTrack(part->trackRange().startTrack);

    //! NOTE: played with the pad as edited (e.g. the keyswitch of its articulation), not yet applied: the instrument has
    //! the dialog's drumset only while the note is rendered (synchronously), never seen elsewhere - changed in place,
    //! as the Percussion panel keeps a pointer to the instrument's drumset
    const Drumset savedDrumset = *instrumentDrumset;
    *instrumentDrumset = m_editedDrumset;
    playbackController()->playElements({ chord.get() });
    *instrumentDrumset = savedDrumset;
}

bool EditDrumPadModel::learningMidi() const
{
    return m_learningMidi;
}

//! NOTE: while learning, the MIDI keyboard is ignored by the score: no note written behind the dialog, and the key
//! isn't played as a pad of the kit (the dialog plays the library's note itself)
void EditDrumPadModel::setLearningMidi(bool learning)
{
    if (m_learningMidi == learning) {
        return;
    }

    m_learningMidi = learning;

    //! NOTE: kept: the notation can be gone (closed score) while learning
    if (learning && m_notation) {
        m_suspendedMidiInput = m_notation->midiInput();
        m_suspendedMidiInput->setNoteInputSuspended(true);
    } else if (!learning && m_suspendedMidiInput) {
        m_suspendedMidiInput->setNoteInputSuspended(false);
        m_suspendedMidiInput = nullptr;
    }

    emit learningMidiChanged();
}

void EditDrumPadModel::onMidiNoteLearnt(int pitch)
{
    setLearningMidi(false);
    setSoundPitch(pitch);
}

bool EditDrumPadModel::hasInstrumentEditor() const
{
    const engraving::InstrumentTrackId trackId { m_instrumentKey.partId, m_instrumentKey.instrumentId };
    return instrumenteditor::target(projectSession()->project(), playbackController().get(), trackId).has_value();
}

void EditDrumPadModel::openInstrumentEditor()
{
    const engraving::InstrumentTrackId trackId { m_instrumentKey.partId, m_instrumentKey.instrumentId };
    const project::INotationProjectPtr project = projectSession()->project();
    const std::optional<instrumenteditor::Target> target = instrumenteditor::target(project, playbackController().get(), trackId);
    if (target) {
        instrumenteditor::dispatch(commandDispatcher().get(), project, *target, "open");
    }
}

// ---- articulation

//! NOTE: the track's map, or a new one bound to it - its articulations and their triggers are shown here once saved
void EditDrumPadModel::openArticulationMapEditor()
{
    muse::rcommand::CommandQuery query(articulationMap() ? EDIT_ARTICULATION_MAP_COMMAND : NEW_ARTICULATION_MAP_COMMAND);
    query.addParam("partId", muse::Val(std::to_string(m_instrumentKey.partId.toUint64())));
    query.addParam("instrumentId", muse::Val(m_instrumentKey.instrumentId.toStdString()));
    commandDispatcher()->dispatch(query);
}

//! NOTE: the map edited (or loaded, removed) meanwhile: its articulations, triggers and keyswitches shown again
void EditDrumPadModel::syncWithArticulationMap()
{
    if (!hasPad()) {
        return;
    }

    const ExpressionMap* map = articulationMap();
    const QString text = map ? map->sourceText.toQString() : QString();
    if (text == m_articulationMapText) {
        return;
    }

    m_articulationMapText = text;
    updateMiddleCOctave();
    onPadEdited();
    notifySoundChanged();
}

const ExpressionMap* EditDrumPadModel::articulationMap() const
{
    return drumkit::articulationMap(projectSession()->masterNotation(), { m_instrumentKey.partId, m_instrumentKey.instrumentId });
}

bool EditDrumPadModel::hasArticulationMap() const
{
    return articulationMap() != nullptr;
}

//! NOTE: two columns: the articulation, and how it's triggered (as in the articulation map editor)
QVariantList EditDrumPadModel::articulationMenuItems() const
{
    const ExpressionMap* map = articulationMap();
    if (!map) {
        return {};
    }

    const QString linkedId = pad().articulationMapEntry.toQString();
    const auto item = [&linkedId](const QString& id, const QString& title, const QString& trigger) {
        return QVariantMap {
            { "id", id }, { "title", title }, { "shortcuts", trigger },
            { "checkable", true }, { "checked", id == linkedId }, { "enabled", true }
        };
    };

    QVariantList items { item(QString(), muse::qtrc("notation/percussion", "None"), QString()) };
    bool linkedFound = linkedId.isEmpty();

    const int middleCOctave = m_middleCOctave;
    for (const ExpressionMapEntry& entry : map->entries) {
        if (entry.disabled) {
            continue;
        }

        linkedFound = linkedFound || entry.id.toQString() == linkedId;
        items << item(entry.id.toQString(), entry.id.toQString(),
                      ArticulationMapWriter::triggerText(entry.messages, entry.channel, middleCOctave).toQString());
    }

    // a link to an articulation no longer in the map: kept, until another one is chosen
    if (!linkedFound) {
        items << item(linkedId, muse::qtrc("notation/percussion", "%1 (not in the map)").arg(linkedId), QString());
    }

    return items;
}

QString EditDrumPadModel::articulationName() const
{
    const QString linkedId = pad().articulationMapEntry.toQString();
    return linkedId.isEmpty() ? muse::qtrc("notation/percussion", "None") : linkedId;
}

QString EditDrumPadModel::articulationTrigger() const
{
    const ExpressionMap* map = articulationMap();
    const ExpressionMapEntry* entry = map ? map->entry(pad().articulationMapEntry) : nullptr;
    return entry ? ArticulationMapWriter::triggerText(entry->messages, entry->channel, m_middleCOctave).toQString()
           : QString();
}

void EditDrumPadModel::setArticulation(const QString& entryId)
{
    pad().articulationMapEntry = entryId;
    onPadEdited();
    notifySoundChanged(); // the keyboard shows its keyswitch
    playSound();
}

// ---- appearance

QString EditDrumPadModel::padName() const
{
    return pad().name.toQString();
}

void EditDrumPadModel::setPadName(const QString& name)
{
    if (name.isEmpty() || pad().name == name) {
        return;
    }

    pad().name = name;
    onPadEdited();
}

QVariantList EditDrumPadModel::noteheadGroups() const
{
    QVariantList groups;
    for (NoteHeadGroup group : DRUM_PAD_NOTEHEAD_GROUPS) {
        groups << QVariantMap { { "text", TConv::translatedUserName(group).toQString() }, { "value", static_cast<int>(group) } };
    }

    return groups;
}

int EditDrumPadModel::noteheadGroup() const
{
    return static_cast<int>(pad().notehead);
}

void EditDrumPadModel::setNoteheadGroup(int group)
{
    pad().notehead = static_cast<NoteHeadGroup>(group);
    onPadEdited();
}

bool EditDrumPadModel::customNoteheads() const
{
    return pad().notehead == NoteHeadGroup::HEAD_CUSTOM;
}

//! NOTE: custom: from the group's noteheads; not custom anymore: back to the normal group
void EditDrumPadModel::setCustomNoteheads(bool custom)
{
    if (custom == customNoteheads()) {
        return;
    }

    if (custom) {
        const NoteHeadGroup group = pad().notehead;
        for (NoteHeadType type : DRUM_PAD_NOTEHEAD_TYPES) {
            pad().noteheads[static_cast<int>(type)] = Note::noteHead(0, group, type);
        }
        pad().notehead = NoteHeadGroup::HEAD_CUSTOM;
    } else {
        pad().notehead = NoteHeadGroup::HEAD_NORMAL;
    }

    onPadEdited();
}

//! NOTE: the noteheads of the SMuFL ranges useful for drums, the most common first
QVariantList EditDrumPadModel::noteheadSymbols() const
{
    static const std::vector<SymId> PRIMARY = {
        SymId::noteheadXOrnate, SymId::noteheadXBlack, SymId::noteheadXHalf, SymId::noteheadXWhole, SymId::noteheadXDoubleWhole,
        SymId::noteheadSlashedBlack1, SymId::noteheadSlashedHalf1, SymId::noteheadSlashedWhole1, SymId::noteheadSlashedDoubleWhole1,
        SymId::noteheadSlashedBlack2, SymId::noteheadSlashedHalf2, SymId::noteheadSlashedWhole2, SymId::noteheadSlashedDoubleWhole2,
        SymId::noteheadSquareBlack, SymId::noteheadMoonBlack, SymId::noteheadTriangleUpRightBlack, SymId::noteheadTriangleDownBlack,
        SymId::noteheadTriangleUpBlack, SymId::noteheadTriangleLeftBlack, SymId::noteheadTriangleRoundDownBlack,
        SymId::noteheadDiamondBlack, SymId::noteheadDiamondHalf, SymId::noteheadDiamondWhole, SymId::noteheadDiamondDoubleWhole,
        SymId::noteheadRoundWhiteWithDot, SymId::noteheadVoidWithX, SymId::noteheadHalfWithX, SymId::noteheadWholeWithX,
        SymId::noteheadDoubleWholeWithX, SymId::noteheadLargeArrowUpBlack, SymId::noteheadLargeArrowUpHalf,
        SymId::noteheadLargeArrowUpWhole, SymId::noteheadLargeArrowUpDoubleWhole
    };
    static const std::vector<String> RANGES = {
        u"Noteheads", u"Round and square noteheads", u"Slash noteheads", u"Shape note noteheads",
        u"Shape note noteheads supplement", u"Techniques noteheads"
    };
    static const std::set<SymId> EXCLUDED = {
        SymId::noteheadParenthesisLeft, SymId::noteheadParenthesisRight, SymId::noteheadParenthesis, SymId::noteheadNull
    };

    std::vector<SymId> symbols = PRIMARY;
    for (const String& range : RANGES) {
        for (SymId symId : Smufl::smuflRanges().at(range)) {
            if (!EXCLUDED.count(symId) && std::find(PRIMARY.begin(), PRIMARY.end(), symId) == PRIMARY.end()) {
                symbols.push_back(symId);
            }
        }
    }

    QVariantList result;
    for (SymId symId : symbols) {
        const char32_t code = engravingFonts()->fallbackFont()->symCode(symId);
        result << QVariantMap {
            { "text", SymNames::translatedUserNameForSymId(symId).toQString() },
            { "value", static_cast<int>(symId) },
            { "glyph", QString::fromUcs4(&code, 1) },
            };
    }

    return result;
}

QString EditDrumPadModel::musicFontFamily() const
{
    return QString::fromStdString(engravingFonts()->fallbackFont()->family());
}

QVariantList EditDrumPadModel::customNoteheadSymbols() const
{
    QVariantList result;
    for (NoteHeadType type : DRUM_PAD_NOTEHEAD_TYPES) {
        const SymId symId = customNoteheads() ? pad().noteheads[static_cast<int>(type)] : Note::noteHead(0, pad().notehead, type);
        result << static_cast<int>(symId);
    }

    return result;
}

void EditDrumPadModel::setCustomNoteheadSymbol(int noteheadType, int symId)
{
    if (noteheadType < 0 || noteheadType >= static_cast<int>(NoteHeadType::HEAD_TYPES)) {
        return;
    }

    pad().noteheads[noteheadType] = static_cast<SymId>(symId);
    onPadEdited();
}

int EditDrumPadModel::staffLine() const
{
    return pad().line;
}

void EditDrumPadModel::setStaffLine(int line)
{
    pad().line = line;
    onPadEdited();
}

int EditDrumPadModel::voice() const
{
    return pad().voice;
}

void EditDrumPadModel::setVoice(int voice)
{
    pad().voice = voice;
    onPadEdited();
}

int EditDrumPadModel::stemDirection() const
{
    return static_cast<int>(pad().stemDirection);
}

void EditDrumPadModel::setStemDirection(int direction)
{
    pad().stemDirection = static_cast<DirectionV>(direction);
    onPadEdited();
}

QString EditDrumPadModel::shortcut() const
{
    return pad().shortcut.toQString();
}

void EditDrumPadModel::defineShortcut()
{
    if (hasPad() && PercussionUtilities(iocContext()).editPercussionShortcut(m_editedDrumset, m_padPitch)) {
        onPadEdited();
    }
}

QVariant EditDrumPadModel::previewItem() const
{
    return QVariant::fromValue(m_previewItem);
}

int EditDrumPadModel::numStaffLines() const
{
    const Part* part = m_notation ? m_notation->parts()->part(m_instrumentKey.partId) : nullptr;
    const Staff* staff = part ? part->staff(0) : nullptr;
    return staff ? staff->lines(m_instrumentKey.tick) : 5;
}

// ---- applying

//! NOTE: one undo step; the score's notes are unchanged: a drum's played note isn't its pitch in the score
void EditDrumPadModel::apply()
{
    const Drumset* drumset = scoreDrumset();
    if (!drumset || !hasPad() || m_editedDrumset == *drumset) {
        return;
    }

    finishPanelLayoutEditing();
    if (m_editedDrumset == m_scoreDrumset) {
        return;
    }

    //! NOTE: before the change: the undo stack notifies it from inside (see syncWithScore())
    m_scoreDrumset = m_editedDrumset;
    m_notation->parts()->replaceDrumset(m_instrumentKey, m_editedDrumset);

    // a new pad is now in the kit: it's edited as any other one
    if (m_newPadRow >= 0 && m_newPadColumn >= 0) {
        m_newPadRow = -1;
        m_newPadColumn = -1;
        notifyEditedPad(/*editing*/ true);
    }

    emit hasChangesChanged();
}

//! NOTE: the drumset was changed elsewhere (undo/redo, Percussion panel...): the pad being edited is kept over it,
//! at the panel's position; the instrument gone: closed
void EditDrumPadModel::syncWithScore()
{
    if (!hasPad()) {
        return;
    }

    const Drumset* drumset = scoreDrumset();
    if (!drumset) {
        m_notation = nullptr;
        emit closeRequested();
        return;
    }

    if (*drumset == m_scoreDrumset) {
        return;
    }

    // only the dialog's own changes not applied yet are kept over the score's (an undone Apply stays undone), at the
    // panel's positions
    Drumset rebased = *drumset;
    for (int pitch = 0; pitch < DRUM_INSTRUMENTS; ++pitch) {
        if (m_editedDrumset.drum(pitch) == m_scoreDrumset.drum(pitch)) {
            continue;
        }

        DrumInstrument drum = m_editedDrumset.drum(pitch);
        if (drumset->isValid(pitch)) {
            drum.panelRow = drumset->panelRow(pitch);
            drum.panelColumn = drumset->panelColumn(pitch);
        }
        rebased.setDrum(pitch, drum);
    }

    m_scoreDrumset = *drumset;
    m_editedDrumset = rebased;

    // the pad gone (e.g. deleted in the panel): another one
    if (!m_editedDrumset.isValid(m_padPitch)) {
        editDefaultPad();
        return;
    }

    m_soundPitch = m_editedDrumset.playedPitch(m_padPitch);
    onPadEdited();
    notifySoundChanged();
}

//! NOTE: the selected note's pad, else the first one; an empty kit: a new pad in the first slot
void EditDrumPadModel::editDefaultPad()
{
    const int padPitch = defaultPadPitch();
    if (padPitch >= 0) {
        editPad(padPitch);
        return;
    }

    const Drumset* drumset = scoreDrumset();
    const int freePitch = firstFreePitch(drumset ? *drumset : m_editedDrumset);
    if (freePitch < 0) {
        return;
    }

    editPad(freePitch);
    createNewPad(0, 0);
    onPadEdited();
    notifyEditedPad(/*editing*/ true);
}

// ---- other pads

//! NOTE: in the Percussion panel's order, asked to it: its layout may be being edited (then applied first)
void EditDrumPadModel::editAdjacentPad(int step)
{
    muse::rcommand::CommandQuery query(PERCUSSION_PANEL_ADJACENT_PAD_COMMAND);
    const bool newPad = m_newPadRow >= 0 && m_newPadColumn >= 0;
    query.set("pitch", muse::Val(newPad ? -1 : m_padPitch));
    if (newPad) {
        query.set("panel_row", muse::Val(m_newPadRow));
        query.set("panel_column", muse::Val(m_newPadColumn));
    }
    query.set("step", muse::Val(step));

    commandDispatcher()->dispatch(query).onResolve(this, [this, step](const muse::rcommand::Response& response) {
        int adjacent = response.ret && response.data.has_value() ? std::any_cast<int>(response.data) : -1;
        if (adjacent >= 0 && !m_scoreDrumset.isValid(adjacent)) {
            adjacent = -1; // the panel shows another instrument's kit
        }

        if (!resolvePendingChanges()) {
            return;
        }

        if (adjacent < 0) {
            adjacent = adjacentPadByPosition(m_padPitch, step);
        }

        if (adjacent >= 0) {
            editPad(adjacent);
        }
    });
}

//! NOTE: without the Percussion panel: by the pads' positions in it, the pads without one last
int EditDrumPadModel::adjacentPadByPosition(int fromPitch, int step) const
{
    const Drumset* drumset = scoreDrumset();
    if (!drumset) {
        return -1;
    }

    std::vector<int> pads;
    for (int pitch = 0; pitch < DRUM_INSTRUMENTS; ++pitch) {
        if (drumset->isValid(pitch)) {
            pads.push_back(pitch);
        }
    }

    if (pads.empty()) {
        return -1;
    }

    const auto position = [drumset](int pitch) {
        const bool placed = drumset->panelRow(pitch) >= 0 && drumset->panelColumn(pitch) >= 0;
        return std::make_tuple(!placed, drumset->panelRow(pitch), drumset->panelColumn(pitch), pitch);
    };
    std::sort(pads.begin(), pads.end(), [&position](int p1, int p2) { return position(p1) < position(p2); });

    const auto it = std::find(pads.begin(), pads.end(), fromPitch);
    const int count = static_cast<int>(pads.size());
    const int current = it != pads.end() ? static_cast<int>(it - pads.begin()) : (step > 0 ? -1 : 0);

    return pads.at(((current + step) % count + count) % count);
}

//! NOTE: only in the dialog until OK: a plain note on the staff's middle line, at the pad's (free) pitch
void EditDrumPadModel::createNewPad(int row, int column)
{
    m_newPadRow = row;
    m_newPadColumn = column;

    DrumInstrument drum(muse::mtrc("notation/percussion", "New pad"), NoteHeadGroup::HEAD_NORMAL,
                        std::max(numStaffLines() - 1, 0), DirectionV::AUTO, row, column);
    m_editedDrumset.setDrum(m_padPitch, drum);
}

//! NOTE: as with Previous / Next pad; a new pad at a free pitch
void EditDrumPadModel::switchToPad(int pitch, int newPadRow, int newPadColumn, KitSource kitSource)
{
    const bool newPad = newPadRow >= 0 && newPadColumn >= 0;
    if (!hasPad()) {
        return;
    }

    // the slot of the new pad being created: nothing to do
    if (newPad && newPadRow == m_newPadRow && newPadColumn == m_newPadColumn) {
        return;
    }

    // another kit asked (a staff's context menu, or the Percussion panel showing another instrument): this pad first,
    // then that kit
    InstrumentKey key;
    Drumset kit;
    const bool otherKit = kitSource != KitSource::Current
                          && resolveInstrumentKey(kitSource == KitSource::Staff, key, kit)
                          && (key.partId != m_instrumentKey.partId || key.instrumentId != m_instrumentKey.instrumentId);

    if (!otherKit) {
        if (!newPad && pitch < 0) {
            pitch = defaultPadPitch();
        }

        // a pad of this kit only
        if (!newPad && (pitch < 0 || !m_editedDrumset.isValid(pitch) || (pitch == m_padPitch && m_newPadRow < 0))) {
            return;
        }
    }

    if (!resolvePendingChanges()) {
        return;
    }

    if (otherKit) {
        m_instrumentKey = key;
        m_editedDrumset = kit;
        m_scoreDrumset = kit;

        const ExpressionMap* map = articulationMap();
        m_articulationMapText = map ? map->sourceText.toQString() : QString();
        updateMiddleCOctave();

        if (!newPad && (pitch < 0 || !kit.isValid(pitch))) {
            editDefaultPad();
            return;
        }
    }

    if (!newPad) {
        editPad(pitch);
        return;
    }

    const Drumset* drumset = scoreDrumset();
    const int freePitch = drumset ? firstFreePitch(*drumset) : -1;
    if (freePitch < 0) {
        return;
    }

    editPad(freePitch);
    createNewPad(newPadRow, newPadColumn);
    onPadEdited();
    notifyEditedPad(/*editing*/ true);
}

//! NOTE: going to another pad doesn't apply this one (Apply does): its changes not applied yet are applied or dropped,
//! as chosen - Cancel stays on it
bool EditDrumPadModel::resolvePendingChanges()
{
    if (!hasChanges()) {
        return true;
    }

    // what each button does said on it (as Save / Don't Save / Cancel), Cancel always staying where it is
    using Button = muse::IInteractive::Button;
    constexpr int CANCEL = int(Button::CustomButton) + 1;
    constexpr int DONT_APPLY = int(Button::CustomButton) + 2;
    constexpr int APPLY = int(Button::CustomButton) + 3;

    const muse::IInteractive::Result result = interactive()->questionSync(
        muse::qtrc("notation/percussion", "Apply the changes of “%1” before editing another pad?").arg(padName()).toStdString(),
        muse::IInteractive::Text(),
    {
        muse::IInteractive::ButtonData(CANCEL, muse::trc("global", "Cancel")),
        muse::IInteractive::ButtonData(DONT_APPLY, muse::trc("notation/percussion", "Don't apply")),
        muse::IInteractive::ButtonData(APPLY, muse::trc("global", "Apply"), /*accent*/ true),
    },
        APPLY);

    switch (result.button()) {
    case APPLY:
        apply();
        return true;
    case DONT_APPLY:
        return true; // the next pad is read from the score
    default: // Cancel, Escape
        return false;
    }
}

void EditDrumPadModel::editPad(int pitch)
{
    setLearningMidi(false);

    m_padPitch = pitch;
    m_newPadRow = -1;
    m_newPadColumn = -1;

    if (const Drumset* drumset = scoreDrumset()) {
        m_scoreDrumset = *drumset;
        m_editedDrumset = *drumset;
    }

    m_soundPitch = m_editedDrumset.playedPitch(pitch);

    onPadEdited();
    notifySoundChanged();
    notifyEditedPad(/*editing*/ true);
}

//! NOTE: the Percussion panel discards a layout being edited when the kit changes: applied first
void EditDrumPadModel::finishPanelLayoutEditing()
{
    commandDispatcher()->dispatch(muse::rcommand::CommandQuery(PERCUSSION_PANEL_FINISH_LAYOUT_EDITING_COMMAND));
}

//! NOTE: the Percussion panel outlines it
void EditDrumPadModel::notifyEditedPad(bool editing)
{
    muse::rcommand::CommandQuery query(PERCUSSION_PANEL_SET_EDITED_PAD_COMMAND);

    const bool newPad = m_newPadRow >= 0 && m_newPadColumn >= 0;
    query.set("pitch", muse::Val(editing && !newPad ? m_padPitch : -1));
    if (editing && newPad) {
        query.set("panel_row", muse::Val(m_newPadRow));
        query.set("panel_column", muse::Val(m_newPadColumn));
    }

    commandDispatcher()->dispatch(query);
}

// ---- drumset files

//! NOTE: the kits' own folder, next to the articulation maps' (created at first use)
muse::io::path_t EditDrumPadModel::kitsDir() const
{
    const muse::io::path_t dir = globalConfiguration()->userDataPath() + "/DrumKits";
    if (!fileSystem()->exists(dir)) {
        fileSystem()->makePath(dir);
    }

    return dir;
}

//! NOTE: the sound library's name (its articulation map's), else the instrument's part name
QString EditDrumPadModel::defaultKitFileName() const
{
    QString name;
    if (const ExpressionMap* map = articulationMap()) {
        name = map->name.toQString();
    }

    if (name.isEmpty() && m_notation) {
        if (const Part* part = m_notation->parts()->part(m_instrumentKey.partId)) {
            name = part->partName().toQString();
        }
    }

    if (name.isEmpty()) {
        name = muse::qtrc("notation/percussion", "Drum kit");
    }

    // not a path
    static const QRegularExpression forbidden(R"([/\\:*?"<>|])");
    return name.replace(forbidden, "-").trimmed();
}

void EditDrumPadModel::loadKit()
{
    const std::vector<std::string> filter = { muse::trc("palette", "MuseScore drumset file") + " (*.drm)" };
    const muse::io::path_t path = interactive()->selectOpeningFileSync(muse::trc("palette", "Load drumset"), kitsDir(), filter);
    if (path.empty()) {
        return;
    }

    muse::io::File file(path);
    if (!file.open(muse::io::IODevice::ReadOnly)) {
        return;
    }

    //! NOTE: read aside: a file left unread (e.g. the "too old" warning cancelled) keeps the kit as it was
    XmlReader e(&file);
    Drumset loaded;
    loaded.clear();

    while (e.readNextStartElement()) {
        if (e.name() != "museScore") {
            continue;
        }

        if (e.attribute("version") != Constants::MSC_VERSION_STR) {
            const muse::IInteractive::Result result = interactive()->warningSync(
                muse::trc("palette", "Drumset file too old"),
                muse::trc("palette", "MuseScore Studio may not be able to load this drumset file."),
                { muse::IInteractive::Button::Cancel, muse::IInteractive::Button::Ignore }, muse::IInteractive::Button::Cancel);

            if (result.standardButton() != muse::IInteractive::Button::Ignore) { // covers Cancel and Esc
                return;
            }
        }

        while (e.readNextStartElement()) {
            if (e.name() == "Drum") {
                loaded.loadDrum(e);
            } else if (e.name() == "percussionPanelColumns") {
                loaded.setPercussionPanelColumns(e.readInt());
            } else {
                e.unknown();
            }
        }
    }

    file.close();

    m_editedDrumset = loaded;
    applyLoadedKit();
}

//! NOTE: the loaded kit replaces the score's one at once (its own undo step, the score's notes unchanged as in the
//! Customize kit dialog), then its first pad is edited; the pad being edited is dropped
void EditDrumPadModel::applyLoadedKit()
{
    if (!m_notation) {
        return;
    }

    setLearningMidi(false);
    m_newPadRow = -1;
    m_newPadColumn = -1;

    const Drumset loaded = m_editedDrumset;
    finishPanelLayoutEditing(); // may resync m_editedDrumset with the score
    m_editedDrumset = loaded;

    m_scoreDrumset = m_editedDrumset;
    m_notation->parts()->replaceDrumset(m_instrumentKey, m_editedDrumset);

    const int firstPad = adjacentPadByPosition(-1, 1);
    if (firstPad >= 0) {
        editPad(firstPad);
        return;
    }

    editDefaultPad();
}

//! NOTE: an empty kit, to build one from scratch (its own undo step, as loading one); the score's notes keep their pitches,
//! without pads until pads are created for them
void EditDrumPadModel::newKit()
{
    using Button = muse::IInteractive::Button;
    constexpr int CANCEL = int(Button::CustomButton) + 1;
    constexpr int NEW_KIT = int(Button::CustomButton) + 2;

    const muse::IInteractive::Result result = interactive()->questionSync(
        muse::trc("notation/percussion", "Remove all the pads of this kit?"),
        muse::trc("notation/percussion", "The notes already written for them stay in the score: a new pad given one of their "
                                         "notes gets them back. This can be undone."),
    {
        muse::IInteractive::ButtonData(CANCEL, muse::trc("global", "Cancel")),
        muse::IInteractive::ButtonData(NEW_KIT, muse::trc("notation/percussion", "New kit"), /*accent*/ true),
    },
        CANCEL);

    if (result.button() != NEW_KIT) {
        return;
    }

    Drumset empty = m_scoreDrumset; // the panel's columns kept
    empty.clear();
    m_editedDrumset = empty;
    applyLoadedKit();
}

//! NOTE: the whole kit, with this pad as edited
void EditDrumPadModel::saveKit()
{
    const std::vector<std::string> filter = { muse::trc("palette", "MuseScore drumset file") + " (*.drm)" };
    muse::io::path_t path = interactive()->selectSavingFileSync(muse::trc("palette", "Save drumset"),
                                                                kitsDir() + "/" + defaultKitFileName() + ".drm", filter);
    if (path.empty()) {
        return;
    }

    if (muse::io::suffix(path) != "drm") {
        path = path.appendingSuffix("drm");
    }

    muse::io::Buffer buffer = muse::io::Buffer::opened(muse::io::IODevice::WriteOnly);
    XmlWriter xml(&buffer);
    xml.startDocument();
    xml.startElement("museScore", { { "version", Constants::MSC_VERSION_STR } });
    m_editedDrumset.save(xml);
    xml.endElement();
    xml.flush();

    const muse::Ret ret = muse::io::File::writeFile(path, buffer.data());
    if (!ret) {
        const QString message = muse::qtrc("palette", "Writing file failed: %1").arg(ret.toString());
        interactive()->error(muse::trc("palette", "Write drumset"), message.toStdString());
    }
}
