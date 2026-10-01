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

#include <QElapsedTimer>
#include <QTimer>

#include "modularity/ioc.h"
#include "async/asyncable.h"
#include "context/iglobalcontext.h"
#include "interactive/iinteractive.h"
#include "midi/imidiinport.h"
#include "engraving/types/types.h"

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
        engraving::staff_idx_t staffIdx = 0;
    };

    std::optional<Target> resolveTarget() const;
    void setArmed(bool armed);

    void onMidiEventReceived(const muse::midi::Event& event);
    std::optional<int> currentUtick() const;
    void commitTake();
    void publishPreviews();
    void clearPreviews();

    std::optional<Target> m_target;
    muse::async::Notification m_armedChanged;

    //! NOTE: per controller, the values received at each utick (the latest one wins)
    std::map<uint8_t, std::map<int, double> > m_take;

    //! NOTE: the engine only reports the playback position every few tens of milliseconds - the time
    //! elapsed since the last report refines it, so that a fast controller movement isn't flattened
    muse::secs_t m_lastReportedPosition = 0.;
    QElapsedTimer m_sinceLastReportedPosition;

    //! NOTE: the take is drawn live (see INotationAutomation::recordingPreviews), refreshed at most this often
    //! whatever the controller's rate - only the uticks changed since the last refresh get redrawn
    QTimer m_previewTimer;
    std::map<uint8_t, std::pair<int, int> > m_previewChangedRanges;
};
}
