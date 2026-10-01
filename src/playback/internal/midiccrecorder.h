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

#include <map>
#include <optional>
#include <vector>

#include <QElapsedTimer>
#include <QTimer>

#include "modularity/ioc.h"
#include "async/asyncable.h"
#include "context/iglobalcontext.h"
#include "interactive/iinteractive.h"
#include "midi/imidiinport.h"
#include "engraving/types/types.h"
#include "engraving/automation/automationtypes.h"

#include "../imidiccrecorder.h"
#include "../iplaybackcontroller.h"

namespace mu::playback {
class MidiCcRecorder : public IMidiCcRecorder, public muse::async::Asyncable, public muse::Contextable
{
    muse::GlobalInject<muse::midi::IMidiInPort> midiInPort;
    muse::ContextInject<context::IGlobalContext> globalContext = { this };
    muse::ContextInject<IPlaybackController> playbackController = { this };
    muse::ContextInject<muse::IInteractive> interactive = { this };

public:
    explicit MidiCcRecorder(const muse::modularity::ContextPtr& iocCtx);

    void init();

    bool isArmed() const override;
    void toggleArmed() override;
    muse::async::Notification armedChanged() const override;

private:
    struct Target {
        engraving::InstrumentTrackId trackId;
        engraving::staff_idx_t masterStaffIdx = 0; // in the master score, whatever the current notation (e.g. a part)
    };

    //! NOTE: a continuous stretch of a take: its values (the latest one wins at a given utick). A take gets a
    //! new segment whenever the playback position jumps (seek, loop, repeat jump, pause and resume) - each
    //! segment replaces the curve only within its own range, a later one over an earlier one
    using TakeSegment = std::map<int, double>;

    std::optional<Target> resolveTarget() const;
    void setArmed(bool armed);

    void onMidiEventReceived(const muse::midi::Event& event);
    void onPlaybackPositionChanged(muse::secs_t position);
    std::optional<int> currentUtick() const;

    //! NOTE: the curve as it is once a controller's take is applied to it, and the utick ranges the take covers.
    //! Final: as written (thinned, every segment joining the curve back), otherwise as drawn while recording
    engraving::AutomationCurve curveWithTake(const engraving::AutomationCurve& existing, const std::vector<TakeSegment>& segments,
                                             bool isFinal, std::vector<std::pair<int, int> >* ranges) const;

    void commitTake();
    void publishPreviews();
    void clearPreviews();
    void clearTake();

    std::optional<Target> m_target;
    muse::async::Notification m_armedChanged;

    std::map<uint8_t, std::vector<TakeSegment> > m_take;
    //! NOTE: incremented on every playback position jump, see TakeSegment
    int m_segmentGeneration = 0;
    std::map<uint8_t, int> m_controllerSegmentGeneration;

    //! NOTE: the engine only reports the playback position every few tens of milliseconds - the time
    //! elapsed since the last report refines it, so that a fast controller movement isn't flattened.
    //! Nothing is recorded before the first report since playback started (e.g. during a count-in, the
    //! position doesn't move yet)
    std::optional<muse::secs_t> m_lastReportedPosition;
    QElapsedTimer m_sinceLastReportedPosition;

    //! NOTE: the take is drawn live (see INotationAutomation::recordingPreviews), refreshed at most this often
    //! whatever the controller's rate - only the uticks changed since the last refresh get redrawn
    QTimer m_previewTimer;
    std::map<uint8_t, std::pair<int, int> > m_previewChangedRanges;
};
}
