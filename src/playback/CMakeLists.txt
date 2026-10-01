# SPDX-License-Identifier: GPL-3.0-only
# MuseScore-Studio-CLA-applies
#
# MuseScore Studio
# Music Composition & Notation
#
# Copyright (C) 2026 MuseScore Limited and others
#
# This program is free software: you can redistribute it and/or modify
# it under the terms of the GNU General Public License version 3 as
# published by the Free Software Foundation.
#
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
# GNU General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with this program. If not, see <https://www.gnu.org/licenses/>.

muse_create_module(playback)

target_sources(playback PRIVATE
    playbackmodule.cpp
    playbackmodule.h
    iplaybackcontroller.h
    imidiccrecorder.h
    iplaybackconfiguration.h
    isoundprofilesrepository.h
    playbacktypes.h
    playbackcommands.h

    internal/iplaybackcommandscontroller.h
    internal/playbackuiactions.cpp
    internal/playbackuiactions.h
    internal/playbackcommandscontroller.cpp
    internal/playbackcommandscontroller.h
    internal/playbackcontroller.cpp
    internal/playbackcontroller.h
    internal/midiccrecorder.cpp
    internal/midiccrecorder.h
    internal/onlinesoundscontroller.cpp
    internal/onlinesoundscontroller.h
    internal/playbackcommandsregister.cpp
    internal/playbackcommandsregister.h
    internal/playbackcommandsstate.cpp
    internal/playbackcommandsstate.h
    internal/playbackconfiguration.cpp
    internal/playbackconfiguration.h
    internal/soundprofilesrepository.cpp
    internal/soundprofilesrepository.h
    internal/drumsetloader.cpp
    internal/drumsetloader.h
    internal/knownaudiopluginsconfigurator.cpp
    internal/knownaudiopluginsconfigurator.h
    internal/videoaudiodecoder.cpp
    internal/videoaudiodecoder.h
)

if (TARGET Qt6::Multimedia)
    target_link_libraries(playback PRIVATE Qt6::Multimedia)
    target_compile_definitions(playback PRIVATE MUE_PLAYBACK_HAS_QT_MULTIMEDIA)
endif()

if (MUE_BUILD_PLAYBACK_TESTS)
    add_subdirectory(tests)
endif()

if (MUE_BUILD_PLAYBACK_QML)
    add_subdirectory(qml/MuseScore/Playback)
endif()
