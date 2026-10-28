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

#include <array>

#include "rcommand/commandtypes.h"
#include "uicomponents/qml/Muse/UiComponents/abstractmenumodel.h"

#include "modularity/ioc.h"
#include "iplaybackcontroller.h"
#include "iplaybackconfiguration.h"
#include "notation/inotationconfiguration.h"

namespace mu::notation {
class PlaybackUiTickCall;
}

namespace mu::playback {
class PlaybackToolBarModel : public muse::uicomponents::AbstractMenuModel
{
    Q_OBJECT

    Q_PROPERTY(bool isToolbarFloating READ isToolbarFloating WRITE setIsToolbarFloating NOTIFY isToolbarFloatingChanged)
    Q_PROPERTY(bool isPlayAllowed READ isPlayAllowed NOTIFY isPlayAllowedChanged)
    Q_PROPERTY(bool returnToStartOnStop READ returnToStartOnStop NOTIFY returnToStartOnStopChanged)

    Q_PROPERTY(QTime maxPlayTime READ maxPlayTime NOTIFY maxPlayTimeChanged)

    Q_PROPERTY(QTime playTime READ playTime WRITE setPlayTime NOTIFY playTimeChanged)
    Q_PROPERTY(qreal playPosition READ playPosition WRITE setPlayPosition NOTIFY playPositionChanged)
    Q_PROPERTY(int measureNumber READ measureNumber WRITE setMeasureNumber NOTIFY measureBeatChanged)
    Q_PROPERTY(int maxMeasureNumber READ maxMeasureNumber NOTIFY measureBeatChanged)
    Q_PROPERTY(int beatNumber READ beatNumber WRITE setBeatNumber NOTIFY measureBeatChanged)
    Q_PROPERTY(int maxBeatNumber READ maxBeatNumber NOTIFY measureBeatChanged)

    Q_PROPERTY(QVariant tempo READ tempo NOTIFY tempoChanged)
    Q_PROPERTY(qreal tempoMultiplier READ tempoMultiplier WRITE setTempoMultiplier NOTIFY tempoChanged)

    QML_ELEMENT

    muse::GlobalInject<notation::INotationConfiguration> notationConfiguration;
    muse::GlobalInject<IPlaybackConfiguration> playbackConfiguration;
    muse::ContextInject<IPlaybackController> playbackController = { this };

public:
    explicit PlaybackToolBarModel(QObject* parent = nullptr);

    bool isToolbarFloating() const;
    bool isPlayAllowed() const;
    bool returnToStartOnStop() const;

    QTime maxPlayTime() const;
    QTime playTime() const;
    qreal playPosition() const;

    int measureNumber() const;
    int maxMeasureNumber() const;
    int beatNumber() const;
    int maxBeatNumber() const;

    QVariant tempo() const;
    qreal tempoMultiplier() const;

    Q_INVOKABLE void load() override;

public slots:
    void setIsToolbarFloating(bool floating);
    void setPlayPosition(qreal position);
    void setPlayTime(const QTime& time);
    void setMeasureNumber(int measureNumber);
    void setBeatNumber(int beatNumber);
    void setTempoMultiplier(qreal multiplier);

signals:
    void isToolbarFloatingChanged(bool floating);
    void isPlayAllowedChanged();
    void returnToStartOnStopChanged();
    void maxPlayTimeChanged();
    void playPositionChanged();
    void playTimeChanged();
    void measureBeatChanged();
    void tempoChanged();

private:
    void setupConnections();
    muse::uicomponents::MenuItem* makeInputPitchMenu();

    void updateActions();

    bool isAdditionalCommand(const muse::rcommand::Command& command) const;

    muse::secs_t totalPlayTime() const;
    engraving::MeasureBeat measureBeat() const;

    muse::uicomponents::MenuItem* makePlayItem();
    void updatePlayItem();

    void updatePlayPosition(muse::audio::secs_t secs);
    std::array<int, 4> shownMeasureBeat() const;

    void rewind(muse::audio::secs_t secs);
    void rewindToBeat(const engraving::MeasureBeat& beat);

    bool m_isToolbarFloating = false;
    muse::secs_t m_playbackPositionSecs = 0.0;

    //! NOTE: the position received during playback is shown at the playback cursors' ticks (see setupConnections())
    muse::secs_t m_pendingPlaybackPositionSecs = 0.0;
    notation::PlaybackUiTickCall* m_playPositionTickCall = nullptr;

    //! NOTE: what the time and measure/beat fields show, notified only when it changes
    int64_t m_shownPlayTimeTenths = 0;
    std::array<int, 4> m_shownMeasureBeat = {};
};
}
