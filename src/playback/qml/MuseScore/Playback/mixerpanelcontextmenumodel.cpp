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

#include "mixerpanelcontextmenumodel.h"

#include <algorithm>
#include <cmath>

#include <QGuiApplication>
#include <QScreen>

#include "types/translatablestring.h"

#include "playback/playbackcommands.h"

using namespace mu;
using namespace mu::playback;
using namespace muse;
using namespace muse::ui;
using namespace muse::uicomponents;
using namespace muse::actions;
using namespace muse::audio;

static const QString VIEW_MENU_ID("view-menu");
static const ActionCode TOGGLE_FULL_SCREEN_ACTION("mixer-panel-toggle-fullscreen");
static const ActionCode ZOOM_IN_ACTION("mixer-panel-zoom-in");
static const ActionCode ZOOM_OUT_ACTION("mixer-panel-zoom-out");
static const ActionCode ZOOM_RESET_ACTION("mixer-panel-zoom-reset");

static constexpr int MIN_ZOOM_PERCENT = 50;
static constexpr int MAX_ZOOM_PERCENT = 200;
static constexpr int ZOOM_STEP_PERCENT = 10;

static TranslatableString mixerSectionTitle(MixerSectionType type)
{
    switch (type) {
    case MixerSectionType::Labels: return TranslatableString("playback", "Labels");
    case MixerSectionType::Sound: return TranslatableString("playback", "Sound");
    case MixerSectionType::Gain: return TranslatableString("playback", "Gain");
    case MixerSectionType::AudioFX: return TranslatableString("playback", "Audio FX");
    case MixerSectionType::AuxSends: return TranslatableString("playback", "Aux sends");
    case MixerSectionType::Balance: return TranslatableString("playback", "Pan");
    case MixerSectionType::Volume: return TranslatableString("playback", "Volume");
    case MixerSectionType::Fader: return TranslatableString("playback", "Fader");
    case MixerSectionType::MuteAndSolo: return TranslatableString("playback", "Mute and solo");
    case MixerSectionType::Title: return TranslatableString("playback", "Name");
    case MixerSectionType::Unknown: break;
    }

    return {};
}

MixerPanelContextMenuModel::MixerPanelContextMenuModel(QObject* parent)
    : AbstractMenuModel(parent)
{
}

bool MixerPanelContextMenuModel::labelsSectionVisible() const
{
    return isSectionVisible(MixerSectionType::Labels);
}

bool MixerPanelContextMenuModel::soundSectionVisible() const
{
    return isSectionVisible(MixerSectionType::Sound);
}

bool MixerPanelContextMenuModel::gainSectionVisible() const
{
    return isSectionVisible(MixerSectionType::Gain);
}

bool MixerPanelContextMenuModel::audioFxSectionVisible() const
{
    return isSectionVisible(MixerSectionType::AudioFX);
}

bool MixerPanelContextMenuModel::auxSendsSectionVisible() const
{
    return isSectionVisible(MixerSectionType::AuxSends);
}

bool MixerPanelContextMenuModel::balanceSectionVisible() const
{
    return isSectionVisible(MixerSectionType::Balance);
}

bool MixerPanelContextMenuModel::volumeSectionVisible() const
{
    return isSectionVisible(MixerSectionType::Volume);
}

bool MixerPanelContextMenuModel::faderSectionVisible() const
{
    return isSectionVisible(MixerSectionType::Fader);
}

bool MixerPanelContextMenuModel::muteAndSoloSectionVisible() const
{
    return isSectionVisible(MixerSectionType::MuteAndSolo);
}

bool MixerPanelContextMenuModel::titleSectionVisible() const
{
    return isSectionVisible(MixerSectionType::Title);
}

bool MixerPanelContextMenuModel::condensedViewEnabled() const
{
    return configuration()->isMixerCondensedViewEnabled();
}

qreal MixerPanelContextMenuModel::zoom() const
{
    return configuration()->mixerZoom();
}

//! NOTE: works in whole percents so repeated steps land exactly on 100% again
//! (no floating-point drift), and snaps an off-grid stored value onto the grid.
void MixerPanelContextMenuModel::stepZoom(int direction)
{
    const int current = static_cast<int>(std::lround(zoom() * 100));
    int next = direction > 0
               ? (current / ZOOM_STEP_PERCENT + 1) * ZOOM_STEP_PERCENT
               : ((current + ZOOM_STEP_PERCENT - 1) / ZOOM_STEP_PERCENT - 1) * ZOOM_STEP_PERCENT;
    next = std::clamp(next, MIN_ZOOM_PERCENT, MAX_ZOOM_PERCENT);

    if (next != current) {
        configuration()->setMixerZoom(next / 100.0);
    }
}

bool MixerPanelContextMenuModel::floating() const
{
    return m_floating;
}

void MixerPanelContextMenuModel::setFloating(bool floating)
{
    if (m_floating == floating) {
        return;
    }

    m_floating = floating;
    emit floatingChanged();

    updateItems();
}

bool MixerPanelContextMenuModel::isFullScreen() const
{
    return m_isFullScreen;
}

void MixerPanelContextMenuModel::setIsFullScreen(bool isFullScreen)
{
    if (m_isFullScreen == isFullScreen) {
        return;
    }

    m_isFullScreen = isFullScreen;
    emit isFullScreenChanged();

    updateItems();
}

QVariantMap MixerPanelContextMenuModel::screenAvailableGeometry(int windowX, int windowY) const
{
    QScreen* screen = QGuiApplication::screenAt(QPoint(windowX, windowY));
    if (!screen) {
        screen = QGuiApplication::primaryScreen();
    }

    QVariantMap result;
    if (!screen) {
        return result;
    }

    const QRect geometry = screen->availableGeometry();
    result["x"] = geometry.x();
    result["y"] = geometry.y();
    result["width"] = geometry.width();
    result["height"] = geometry.height();
    return result;
}

void MixerPanelContextMenuModel::load()
{
    AbstractMenuModel::load();

    dispatcher()->reg(this, TOGGLE_FULL_SCREEN_ACTION, [this]() {
        emit toggleFullScreenRequested();
    });

    dispatcher()->reg(this, ZOOM_IN_ACTION, [this]() { stepZoom(+1); });
    dispatcher()->reg(this, ZOOM_OUT_ACTION, [this]() { stepZoom(-1); });
    dispatcher()->reg(this, ZOOM_RESET_ACTION, [this]() { configuration()->setMixerZoom(1.0); });

    configuration()->mixerZoomChanged().onReceive(this, [this](double) {
        emit zoomChanged();

        //! NOTE: rebuilt so Zoom in/out get disabled at the limits
        updateItems();
    });

    configuration()->areAuxChannelsVisibleChanged().onReceive(this, [this](bool newVisibilityValue) {
        auto query = rcommand::make_query(TOGGLE_AUX_CHANNELS_COMMAND, rcommand::Params());
        setViewMenuItemChecked(query, newVisibilityValue);
    });

    configuration()->isMixerSectionVisibleChanged().onReceive(this, [this](MixerSectionType sectionType, bool newVisibilityValue) {
        auto query = rcommand::make_query(TOGGLE_MIXER_SECTION_COMMAND, { { "section", Val(str_conv(sectionType)) } });
        setViewMenuItemChecked(query, newVisibilityValue);

        emitMixerSectionVisibilityChanged(sectionType);
    });

    configuration()->isMixerCondensedViewEnabledChanged().onReceive(this, [this](bool newEnabledValue) {
        auto query = rcommand::make_query(TOGGLE_MIXER_CONDENSED_VIEW_COMMAND, rcommand::Params());
        setViewMenuItemChecked(query, newEnabledValue);

        emit condensedViewEnabledChanged();
    });

    updateItems();
}

bool MixerPanelContextMenuModel::isSectionVisible(MixerSectionType sectionType) const
{
    return configuration()->isMixerSectionVisible(sectionType);
}

MenuItem* MixerPanelContextMenuModel::buildSectionVisibleItem(MixerSectionType sectionType)
{
    MenuItem* item = new MenuItem(this);
    item->setTitle(mixerSectionTitle(sectionType));
    item->setCheckable(true);
    item->setChecked(isSectionVisible(sectionType));
    item->setCommandQuery(rcommand::make_query(TOGGLE_MIXER_SECTION_COMMAND, { { "section", Val(str_conv(sectionType)) } }));
    return item;
}

MenuItem* MixerPanelContextMenuModel::buildAuxChannelsVisibleItem()
{
    MenuItem* item = new MenuItem(this);
    item->setTitle(TranslatableString("playback", "Aux channels"));
    item->setCheckable(true);
    item->setChecked(configuration()->areAuxChannelsVisible());
    item->setCommandQuery(rcommand::make_query(TOGGLE_AUX_CHANNELS_COMMAND, rcommand::Params()));
    return item;
}

MenuItem* MixerPanelContextMenuModel::buildCondensedViewItem()
{
    MenuItem* item = new MenuItem(this);
    item->setTitle(TranslatableString("playback", "Condensed view"));
    item->setCheckable(true);
    item->setChecked(configuration()->isMixerCondensedViewEnabled());
    item->setCommandQuery(rcommand::make_query(TOGGLE_MIXER_CONDENSED_VIEW_COMMAND, rcommand::Params()));
    return item;
}

MenuItem* MixerPanelContextMenuModel::buildZoomItem(const TranslatableString& title, const ActionCode& code, bool enabled)
{
    UiAction action;
    action.title = title;
    action.code = code;

    MenuItem* item = new MenuItem(action, this);
    item->setId(QString::fromStdString(code));
    item->setState(UiActionState::make_enabled(enabled));
    return item;
}

void MixerPanelContextMenuModel::setViewMenuItemChecked(const muse::rcommand::CommandQuery& query, bool checked)
{
    MenuItem& viewMenu = findMenu(VIEW_MENU_ID);

    for (MenuItem* item : viewMenu.subitems()) {
        if (item->commandQuery() == query) {
            item->setChecked(checked);
            return;
        }
    }
}

void MixerPanelContextMenuModel::onCommandStateChanged(const muse::rcommand::Command& command, const muse::rcommand::CommandState& state)
{
    //! NOTE: TOGGLE_MIXER_SECTION_COMMAND/TOGGLE_AUX_CHANNELS_COMMAND back several
    //! independent per-section View-menu items (each carrying a different query param,
    //! e.g. "?section=gain"), but MenuItem::command() (via Uri) strips query params, so
    //! every one of those items shares the SAME bare command identity here and would
    //! otherwise all be overwritten with whatever single CommandState the base class last
    //! saw for that command (playbackcommandsstate.cpp has no case for either command, so
    //! it defaults to checked=false - permanently stomping every item's real, correctly
    //! per-section checked state set by buildSectionVisibleItem()/setViewMenuItemChecked()).
    //! Skip the base class handling entirely for these two; this model already keeps them
    //! in sync itself via configuration()->isMixerSectionVisibleChanged()/
    //! areAuxChannelsVisibleChanged() in load(), matched by full query (not just command()).
    if (command == TOGGLE_MIXER_SECTION_COMMAND || command == TOGGLE_AUX_CHANNELS_COMMAND
        || command == TOGGLE_MIXER_CONDENSED_VIEW_COMMAND) {
        return;
    }

    AbstractMenuModel::onCommandStateChanged(command, state);
}

#ifdef MUSE_MODULE_ACTIONS_SUPPORT
void MixerPanelContextMenuModel::onActionsStateChanges(const muse::actions::ActionCodeList& codes)
{
    //! NOTE: a second, independent generic reactive path, parallel to onCommandStateChanged()
    //! above and just as capable of stomping these items' checked state - MenuItem::actionCode()
    //! (used by AbstractMenuModel::updateState()'s ActionCodeList overload to match items) just
    //! returns the item's raw command-query intent string verbatim (see MenuItem::setActionCode()/
    //! actionCode()), so it's exposed under this mechanism too even though these items were built
    //! as commands, never as actual UiActions. Confirmed via LOGD tracing: MenuItem::setState()
    //! (the setter this path calls) was flipping the "Aux channels" item's checked to false with
    //! neither onCommandStateChanged() nor a real user toggle involved.
    ActionCodeList filtered;
    filtered.reserve(codes.size());

    const std::string mixerSectionPrefix = TOGGLE_MIXER_SECTION_COMMAND.toString();
    const std::string auxChannelsCode = TOGGLE_AUX_CHANNELS_COMMAND.toString();
    const std::string condensedViewCode = TOGGLE_MIXER_CONDENSED_VIEW_COMMAND.toString();

    for (const ActionCode& code : codes) {
        if (code == auxChannelsCode || code == condensedViewCode || code.rfind(mixerSectionPrefix, 0) == 0) {
            continue;
        }
        filtered.push_back(code);
    }

    if (!filtered.empty()) {
        AbstractMenuModel::onActionsStateChanges(filtered);
    }
}

#endif

void MixerPanelContextMenuModel::emitMixerSectionVisibilityChanged(MixerSectionType sectionType)
{
    switch (sectionType) {
    case MixerSectionType::Labels:
        emit labelsSectionVisibleChanged();
        break;
    case MixerSectionType::Sound:
        emit soundSectionVisibleChanged();
        break;
    case MixerSectionType::Gain:
        emit gainSectionVisibleChanged();
        break;
    case MixerSectionType::AudioFX:
        emit audioFxSectionVisibleChanged();
        break;
    case MixerSectionType::AuxSends:
        emit auxSendsSectionVisibleChanged();
        break;
    case MixerSectionType::Balance:
        emit balanceSectionVisibleChanged();
        break;
    case MixerSectionType::Volume:
        emit volumeSectionVisibleChanged();
        break;
    case MixerSectionType::Fader:
        emit faderSectionVisibleChanged();
        break;
    case MixerSectionType::MuteAndSolo:
        emit muteAndSoloSectionVisibleChanged();
        break;
    case MixerSectionType::Title:
        emit titleSectionVisibleChanged();
        break;
    case MixerSectionType::Unknown:
        break;
    }
}

void MixerPanelContextMenuModel::updateItems()
{
    MenuItemList viewMenuItems {
        buildCondensedViewItem(),
        makeSeparator(),
        buildSectionVisibleItem(MixerSectionType::Labels),
        buildSectionVisibleItem(MixerSectionType::Sound),
        buildSectionVisibleItem(MixerSectionType::Gain),
        buildSectionVisibleItem(MixerSectionType::AudioFX),
        buildSectionVisibleItem(MixerSectionType::AuxSends),
        buildAuxChannelsVisibleItem(),
    };

    viewMenuItems.push_back(buildSectionVisibleItem(MixerSectionType::Balance));
    viewMenuItems.push_back(buildSectionVisibleItem(MixerSectionType::Volume));
    viewMenuItems.push_back(buildSectionVisibleItem(MixerSectionType::Fader));
    viewMenuItems.push_back(buildSectionVisibleItem(MixerSectionType::MuteAndSolo));
    viewMenuItems.push_back(buildSectionVisibleItem(MixerSectionType::Title));

    MenuItemList items;

    if (m_floating) {
        UiAction fullScreenAction;
        fullScreenAction.title = TranslatableString("playback", "Full screen");
        fullScreenAction.code = TOGGLE_FULL_SCREEN_ACTION;
        fullScreenAction.checkable = Checkable::Yes;

        MenuItem* fullScreenItem = new MenuItem(fullScreenAction, this);
        fullScreenItem->setId("mixer-panel-fullscreen");
        fullScreenItem->setState(UiActionState::make_enabled(m_isFullScreen));

        items << fullScreenItem;
    }

    items << makeMenuItem(OPEN_PLAYBACK_SETUP_COMMAND);
    items << makeMenu(TranslatableString("playback", "View"), viewMenuItems, VIEW_MENU_ID);

    const int zoomPercent = static_cast<int>(std::lround(zoom() * 100));
    items << makeSeparator();
    items << buildZoomItem(TranslatableString("playback", "Zoom in"), ZOOM_IN_ACTION, zoomPercent < MAX_ZOOM_PERCENT);
    items << buildZoomItem(TranslatableString("playback", "Zoom out"), ZOOM_OUT_ACTION, zoomPercent > MIN_ZOOM_PERCENT);
    items << buildZoomItem(TranslatableString("playback", "Reset zoom"), ZOOM_RESET_ACTION, zoomPercent != 100);

    setItems(items);
}
