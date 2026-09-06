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

static TranslatableString mixerSectionTitle(MixerSectionType type)
{
    switch (type) {
    case MixerSectionType::Labels: return TranslatableString("playback", "Labels");
    case MixerSectionType::Sound: return TranslatableString("playback", "Sound");
    case MixerSectionType::AudioFX: return TranslatableString("playback", "Audio FX");
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

bool MixerPanelContextMenuModel::audioFxSectionVisible() const
{
    return isSectionVisible(MixerSectionType::AudioFX);
}

bool MixerPanelContextMenuModel::auxSendsSectionVisible() const
{
    for (aux_channel_idx_t idx = 0; idx < AUX_CHANNEL_NUM; ++idx) {
        if (configuration()->isAuxSendVisible(idx)) {
            return true;
        }
    }

    return false;
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

    configuration()->isAuxSendVisibleChanged().onReceive(this, [this](aux_channel_idx_t auxSendIndex, bool newVisibilityValue) {
        auto query = rcommand::make_query(TOGGLE_AUX_SEND_COMMAND, { { "auxsend-index", Val(auxSendIndex) } });
        setViewMenuItemChecked(query, newVisibilityValue);

        emit auxSendsSectionVisibleChanged();
    });

    configuration()->isAuxChannelVisibleChanged().onReceive(this, [this](aux_channel_idx_t auxChannelIndex, bool newVisibilityValue) {
        auto query = rcommand::make_query(TOGGLE_AUX_CHANNEL_COMMAND, { { "auxchannel-index", Val(auxChannelIndex) } });
        setViewMenuItemChecked(query, newVisibilityValue);
    });

    configuration()->isMixerSectionVisibleChanged().onReceive(this, [this](MixerSectionType sectionType, bool newVisibilityValue) {
        auto query = rcommand::make_query(TOGGLE_MIXER_SECTION_COMMAND, { { "section", Val(str_conv(sectionType)) } });
        setViewMenuItemChecked(query, newVisibilityValue);

        emitMixerSectionVisibilityChanged(sectionType);
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

MenuItem* MixerPanelContextMenuModel::buildAuxSendVisibleItem(aux_channel_idx_t index)
{
    MenuItem* item = new MenuItem(this);
    item->setTitle(TranslatableString("playback", String("Aux send %1").arg(index + 1)));
    item->setCheckable(true);
    item->setChecked(configuration()->isAuxSendVisible(index));
    item->setCommandQuery(rcommand::make_query(TOGGLE_AUX_SEND_COMMAND, { { "auxsend-index", Val(index) } }));
    return item;
}

MenuItem* MixerPanelContextMenuModel::buildAuxChannelVisibleItem(aux_channel_idx_t index)
{
    MenuItem* item = new MenuItem(this);
    item->setTitle(TranslatableString("playback", String("Aux channel %1").arg(index + 1)));
    item->setCheckable(true);
    item->setChecked(configuration()->isAuxChannelVisible(index));
    item->setCommandQuery(rcommand::make_query(TOGGLE_AUX_CHANNEL_COMMAND, { { "auxchannel-index", Val(index) } }));
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

void MixerPanelContextMenuModel::emitMixerSectionVisibilityChanged(MixerSectionType sectionType)
{
    switch (sectionType) {
    case MixerSectionType::Labels:
        emit labelsSectionVisibleChanged();
        break;
    case MixerSectionType::Sound:
        emit soundSectionVisibleChanged();
        break;
    case MixerSectionType::AudioFX:
        emit audioFxSectionVisibleChanged();
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
        buildSectionVisibleItem(MixerSectionType::Labels),
        buildSectionVisibleItem(MixerSectionType::Sound),
        buildSectionVisibleItem(MixerSectionType::AudioFX),
    };

    for (aux_channel_idx_t idx = 0; idx < AUX_CHANNEL_NUM; ++idx) {
        viewMenuItems.push_back(buildAuxSendVisibleItem(idx));
    }

    for (aux_channel_idx_t idx = 0; idx < AUX_CHANNEL_NUM; ++idx) {
        viewMenuItems.push_back(buildAuxChannelVisibleItem(idx));
    }

    viewMenuItems.push_back(buildSectionVisibleItem(MixerSectionType::Balance));
    viewMenuItems.push_back(buildSectionVisibleItem(MixerSectionType::Volume));
    viewMenuItems.push_back(buildSectionVisibleItem(MixerSectionType::Fader));
    viewMenuItems.push_back(buildSectionVisibleItem(MixerSectionType::MuteAndSolo));
    viewMenuItems.push_back(buildSectionVisibleItem(MixerSectionType::Title));

    MenuItemList items;

    if (m_floating) {
        UiAction fullScreenAction;
        fullScreenAction.title = m_isFullScreen ? TranslatableString("playback", "Exit full screen") : TranslatableString(
            "playback", "Full screen");
        fullScreenAction.code = TOGGLE_FULL_SCREEN_ACTION;

        MenuItem* fullScreenItem = new MenuItem(fullScreenAction, this);
        fullScreenItem->setId("mixer-panel-fullscreen");
        fullScreenItem->setState(UiActionState::make_enabled());

        items << fullScreenItem;
    }

    items << makeMenuItem(OPEN_PLAYBACK_SETUP_COMMAND);
    items << makeMenu(TranslatableString("playback", "View"), viewMenuItems, VIEW_MENU_ID);

    setItems(items);
}
