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
#include "secondarypalettespanelmodel.h"

#include "settings.h"
#include "translation.h"
#include "ui/view/iconcodes.h"

#include "internal/palette.h"
#include "internal/palettemodel.h"

using namespace mu::palette;
using namespace muse;
using namespace muse::ui;

static const Settings::Key CURRENT_PALETTE_KEY("palette", "palette/secondaryPanel/currentPalette");
static const Settings::Key COLLAPSED_KEY("palette", "palette/secondaryPanel/collapsed");

SecondaryPalettesPanelModel::SecondaryPalettesPanelModel(QObject* parent)
    : ZoomableMenuModel(Settings::Key("palette", "palette/secondaryPanelZoom"), 1.0, parent)
{
    settings()->setDefaultValue(CURRENT_PALETTE_KEY, Val(std::string()));
    settings()->setDefaultValue(COLLAPSED_KEY, Val(false));
}

void SecondaryPalettesPanelModel::load()
{
    ZoomableMenuModel::load();

    setItems({ makeZoomMenu() });
}

QString SecondaryPalettesPanelModel::currentPaletteName() const
{
    return settings()->value(CURRENT_PALETTE_KEY).toQString();
}

void SecondaryPalettesPanelModel::setCurrentPaletteName(const QString& name)
{
    if (name == currentPaletteName()) {
        return;
    }

    settings()->setSharedValue(CURRENT_PALETTE_KEY, Val(name));
    emit currentPaletteNameChanged();
}

bool SecondaryPalettesPanelModel::collapsed() const
{
    return settings()->value(COLLAPSED_KEY).toBool();
}

void SecondaryPalettesPanelModel::setCollapsed(bool collapsed)
{
    if (collapsed == this->collapsed()) {
        return;
    }

    settings()->setSharedValue(COLLAPSED_KEY, Val(collapsed));
    emit collapsedChanged();
}

int SecondaryPalettesPanelModel::paletteIconCode(const QModelIndex& paletteIndex) const
{
    const QVariant type = paletteIndex.data(PaletteTreeModel::PaletteTypeRole);
    if (!type.isValid()) {
        return static_cast<int>(IconCode::Code::NONE);
    }

    switch (type.value<Palette::Type>()) {
    case Palette::Type::Clef: return static_cast<int>(IconCode::Code::CLEF_TREBLE);
    case Palette::Type::KeySig: return static_cast<int>(IconCode::Code::KEY_SIGNATURE);
    case Palette::Type::TimeSig: return static_cast<int>(IconCode::Code::TIME_SIGNATURE);
    case Palette::Type::Bracket: return static_cast<int>(IconCode::Code::BRACKET);
    case Palette::Type::Accidental: return static_cast<int>(IconCode::Code::ACCIDENTAL_SHARP);
    case Palette::Type::Articulation: return static_cast<int>(IconCode::Code::ARTICULATION);
    case Palette::Type::Ornament: return static_cast<int>(IconCode::Code::ORNAMENT);
    case Palette::Type::Breath: return static_cast<int>(IconCode::Code::FERMATA);
    case Palette::Type::GraceNote: return static_cast<int>(IconCode::Code::ACCIACCATURA);
    case Palette::Type::NoteHead: return static_cast<int>(IconCode::Code::NOTE_HEAD);
    case Palette::Type::Line: return static_cast<int>(IconCode::Code::SLUR);
    case Palette::Type::BarLine: return static_cast<int>(IconCode::Code::DOUBLE_BAR_LINE);
    case Palette::Type::Tremolo: return static_cast<int>(IconCode::Code::TREMOLO_ONE_NOTE);
    case Palette::Type::Tempo: return static_cast<int>(IconCode::Code::METRONOME);
    case Palette::Type::Dynamic: return static_cast<int>(IconCode::Code::DYNAMIC_FORTE);
    case Palette::Type::Repeat: return static_cast<int>(IconCode::Code::REPEAT_START);
    case Palette::Type::FretboardDiagram: return static_cast<int>(IconCode::Code::FRETBOARD_DIAGRAM);
    case Palette::Type::Layout: return static_cast<int>(IconCode::Code::LINE_BREAK);
    case Palette::Type::Beam: return static_cast<int>(IconCode::Code::BEAM_JOIN);
    case Palette::Type::Guitar: return static_cast<int>(IconCode::Code::GUITAR_BEND);
    case Palette::Type::Keyboard: return static_cast<int>(IconCode::Code::PEDAL_MARKING);
    case Palette::Type::Pitch: return static_cast<int>(IconCode::Code::OTTAVA);
    case Palette::Type::StringTunings: return static_cast<int>(IconCode::Code::TUNING_FORK);
    case Palette::Type::Playback: return static_cast<int>(IconCode::Code::AUDIO);
    default:
        break;
    }

    //! NOTE: no fitting icon (Text: see paletteIconText(), Arpeggio, Fingering, Accordion, Bagpipe, Harp, Handbells,
    //! custom palettes...)
    return static_cast<int>(IconCode::Code::NONE);
}

QString SecondaryPalettesPanelModel::paletteIconText(const QModelIndex& paletteIndex) const
{
    const QVariant type = paletteIndex.data(PaletteTreeModel::PaletteTypeRole);
    if (type.isValid() && type.value<Palette::Type>() == Palette::Type::Text) {
        //: Stands for the Text palette in the narrow Secondary palettes panel: keep it short (4 or 5 letters)
        return muse::qtrc("palette", "TEXT");
    }

    return QString();
}
