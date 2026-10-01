/*
 * SPDX-License-Identifier: GPL-3.0-only
 * MuseScore-Studio-CLA-applies
 *
 * MuseScore Studio
 * Music Composition & Notation
 *
 * Copyright (C) 2026 MuseScore Limited
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 3 as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program. If not, see <https://www.gnu.org/licenses/>.
 */

#pragma once

#include <map>

#include "async/channel.h"
#include "async/notification.h"
#include "global/types/translatablestring.h"

#include "engraving/automation/automationdata.h"
#include "engraving/automation/automationtypes.h"

namespace mu::notation {
using AutomationType = mu::engraving::AutomationType;
using AutomationCurveKey = mu::engraving::AutomationCurveKey;
using AutomationCurve = mu::engraving::AutomationCurve;
using AutomationCurveMap = mu::engraving::AutomationCurveMap;
using AutomationPointEdit = mu::engraving::AutomationPointEdit;
using AutomationPointEdits = mu::engraving::AutomationPointEdits;
using AutomationChanges = mu::engraving::AutomationChanges;
using AutomationData = mu::engraving::AutomationData;
using AutomationDataPtr = mu::engraving::AutomationDataPtr;
using AutomationDataConstPtr = mu::engraving::AutomationDataConstPtr;

class INotationAutomation
{
public:
    virtual ~INotationAutomation() = default;

    virtual bool isAutomationModeEnabled() const = 0;
    virtual void setAutomationModeEnabled(bool enabled) = 0;
    virtual muse::async::Notification automationModeEnabledChanged() const = 0;

    virtual AutomationDataConstPtr automationData() const = 0;
    virtual void editPoints(const AutomationCurveKey& key, AutomationPointEdits& edits) = 0;
    //! NOTE: edits to several curves at once, as a single undoable step
    virtual void editPoints(std::vector<std::pair<AutomationCurveKey, AutomationPointEdits> >& editsByCurve) = 0;
    virtual void editPoints(std::vector<std::pair<AutomationCurveKey, AutomationPointEdits> >& editsByCurve,
                            const muse::TranslatableString& actionName) = 0;

    //! NOTE: MIDI CCs the user picked for this score, besides the predefined ones (undoable, saved with the score)
    virtual std::vector<uint8_t> customMidiCcs() const = 0;
    virtual void addCustomMidiCc(uint8_t controller) = 0;

    //! NOTE: a MIDI CC recording take: its point edits plus the controllers it adds to the score's custom
    //! MIDI CCs, as a single undoable step
    virtual void recordMidiCcTake(std::vector<std::pair<AutomationCurveKey, AutomationPointEdits> >& editsByCurve,
                                  const std::vector<uint8_t>& newCustomMidiCcs) = 0;

    //! NOTE: a take being recorded, shown over its curve before it's written (the curve's points within the
    //! preview's own range are replaced by it, like the take will). Never saved nor played, nor undoable
    virtual const std::map<AutomationCurveKey, AutomationCurve>& recordingPreviews() const = 0;
    virtual void setRecordingPreview(const AutomationCurveKey& key, const AutomationCurve& preview, int changedFromUtick,
                                     int changedToUtick) = 0;
    virtual void clearRecordingPreviews() = 0;
    //! NOTE: same shape as AutomationData::changed(), so a view can refresh just what the preview changed
    virtual muse::async::Channel<engraving::AutomationChanges> recordingPreviewChanged() const = 0;
};

using INotationAutomationPtr = std::shared_ptr<INotationAutomation>;
}
