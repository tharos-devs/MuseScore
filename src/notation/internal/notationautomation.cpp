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
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <https://www.gnu.org/licenses/>.
 */

#include "notationautomation.h"

#include "engraving/dom/masterscore.h"
#include "engraving/editing/transaction/transaction.h"

#include "global/containers.h"

#include "translation.h"
#include "log.h"

using namespace mu::notation;

NotationAutomation::NotationAutomation(INotationUndoStackPtr undoStack)
    : m_undoStack(std::move(undoStack))
{
}

bool NotationAutomation::isAutomationModeEnabled() const
{
    return m_isAutomationModeEnabled;
}

void NotationAutomation::setAutomationModeEnabled(bool enabled)
{
    if (m_isAutomationModeEnabled == enabled) {
        return;
    }
    m_isAutomationModeEnabled = enabled;
    m_automationModeEnabledChanged.notify();
}

muse::async::Notification NotationAutomation::automationModeEnabledChanged() const
{
    return m_automationModeEnabledChanged;
}

AutomationDataConstPtr NotationAutomation::automationData() const
{
    return m_masterScore ? m_masterScore->automationData() : nullptr;
}

void NotationAutomation::editPoints(const AutomationCurveKey& key, AutomationPointEdits& edits)
{
    IF_ASSERT_FAILED(m_masterScore && m_undoStack) {
        return;
    }

    m_undoStack->transaction(muse::TranslatableString("undoableAction", "Edit automation points"),
                             [&](engraving::Transaction&) {
        m_masterScore->editAutomationPoints(key, edits);
    });
}

void NotationAutomation::editPoints(std::vector<std::pair<AutomationCurveKey, AutomationPointEdits> >& editsByCurve)
{
    editPoints(editsByCurve, muse::TranslatableString("undoableAction", "Edit automation points"));
}

void NotationAutomation::editPoints(std::vector<std::pair<AutomationCurveKey, AutomationPointEdits> >& editsByCurve,
                                    const muse::TranslatableString& actionName)
{
    IF_ASSERT_FAILED(m_masterScore && m_undoStack) {
        return;
    }

    m_undoStack->transaction(actionName, [&](engraving::Transaction&) {
        for (auto& [key, edits] : editsByCurve) {
            if (!edits.empty()) {
                m_masterScore->editAutomationPoints(key, edits);
            }
        }
    });
}

std::vector<uint8_t> NotationAutomation::customMidiCcs() const
{
    const AutomationDataConstPtr data = automationData();
    return data ? data->customMidiCcs() : std::vector<uint8_t>();
}

void NotationAutomation::addCustomMidiCc(uint8_t controller)
{
    IF_ASSERT_FAILED(m_masterScore && m_undoStack) {
        return;
    }

    std::vector<uint8_t> controllers = customMidiCcs();
    if (muse::contains(controllers, controller)) {
        return;
    }
    controllers.push_back(controller);

    m_undoStack->transaction(muse::TranslatableString("undoableAction", "Add MIDI CC%1").arg(static_cast<int>(controller)),
                             [&](engraving::Transaction&) {
        m_masterScore->setCustomMidiCcs(controllers);
    });
}

void NotationAutomation::recordMidiCcTake(std::vector<std::pair<AutomationCurveKey, AutomationPointEdits> >& editsByCurve,
                                          const std::vector<uint8_t>& newCustomMidiCcs)
{
    IF_ASSERT_FAILED(m_masterScore && m_undoStack) {
        return;
    }

    std::vector<uint8_t> controllers = customMidiCcs();
    bool controllersChanged = false;
    for (const uint8_t controller : newCustomMidiCcs) {
        if (!muse::contains(controllers, controller)) {
            controllers.push_back(controller);
            controllersChanged = true;
        }
    }

    m_undoStack->transaction(muse::TranslatableString("undoableAction", "Record MIDI CC"), [&](engraving::Transaction&) {
        if (controllersChanged) {
            m_masterScore->setCustomMidiCcs(controllers);
        }

        for (auto& [key, edits] : editsByCurve) {
            if (!edits.empty()) {
                m_masterScore->editAutomationPoints(key, edits);
            }
        }
    });
}

const std::map<AutomationCurveKey, AutomationCurve>& NotationAutomation::recordingPreviews() const
{
    return m_recordingPreviews;
}

void NotationAutomation::setRecordingPreview(const AutomationCurveKey& key, const AutomationCurve& preview, int changedFromUtick,
                                             int changedToUtick)
{
    m_recordingPreviews.insert_or_assign(key, preview);

    mu::engraving::AutomationChanges changes;
    changes.affectedKeys.insert(key);
    changes.tickFrom = changedFromUtick;
    changes.tickTo = changedToUtick;
    m_recordingPreviewChanged.send(changes);
}

void NotationAutomation::clearRecordingPreviews()
{
    if (m_recordingPreviews.empty()) {
        return;
    }

    mu::engraving::AutomationChanges changes;
    for (const auto& [key, preview] : m_recordingPreviews) {
        changes.affectedKeys.insert(key);
        if (!preview.empty()) {
            changes.tickFrom = changes.tickFrom < 0 ? preview.cbegin()->first : std::min(changes.tickFrom, preview.cbegin()->first);
            changes.tickTo = std::max(changes.tickTo, std::prev(preview.cend())->first);
        }
    }

    m_recordingPreviews.clear();
    m_recordingPreviewChanged.send(changes);
}

muse::async::Channel<mu::engraving::AutomationChanges> NotationAutomation::recordingPreviewChanged() const
{
    return m_recordingPreviewChanged;
}

void NotationAutomation::setMasterScore(engraving::MasterScore* masterScore)
{
    m_masterScore = masterScore;
}
