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

#include "videopanelcontextmenumodel.h"

#include <QGuiApplication>
#include <QScreen>

#include "types/translatablestring.h"

using namespace mu::playback;
using namespace muse;
using namespace muse::ui;
using namespace muse::uicomponents;
using namespace muse::actions;

static const ActionCode TOGGLE_FULL_SCREEN_ACTION("video-panel-toggle-fullscreen");
static const ActionCode SET_HITPOINTS_PANEL_RIGHT_ACTION("video-panel-set-hitpoints-right");
static const ActionCode SET_HITPOINTS_PANEL_DOWN_ACTION("video-panel-set-hitpoints-down");
static const ActionCode TOGGLE_HITPOINTS_PANEL_VISIBLE_ACTION("video-panel-toggle-hitpoints-visible");

VideoPanelContextMenuModel::VideoPanelContextMenuModel(QObject* parent)
    : AbstractMenuModel(parent)
{
}

void VideoPanelContextMenuModel::load()
{
    AbstractMenuModel::load();

    dispatcher()->reg(this, TOGGLE_FULL_SCREEN_ACTION, [this]() {
        emit toggleFullScreenRequested();
    });

    dispatcher()->reg(this, SET_HITPOINTS_PANEL_RIGHT_ACTION, [this]() {
        emit setHitPointsPanelBelowTimelineRequested(false);
    });

    dispatcher()->reg(this, SET_HITPOINTS_PANEL_DOWN_ACTION, [this]() {
        emit setHitPointsPanelBelowTimelineRequested(true);
    });

    dispatcher()->reg(this, TOGGLE_HITPOINTS_PANEL_VISIBLE_ACTION, [this]() {
        emit toggleHitPointsPanelVisibleRequested();
    });

    updateItems();
}

bool VideoPanelContextMenuModel::floating() const
{
    return m_floating;
}

void VideoPanelContextMenuModel::setFloating(bool floating)
{
    if (m_floating == floating) {
        return;
    }

    m_floating = floating;
    emit floatingChanged();

    updateItems();
}

bool VideoPanelContextMenuModel::isFullScreen() const
{
    return m_isFullScreen;
}

void VideoPanelContextMenuModel::setIsFullScreen(bool isFullScreen)
{
    if (m_isFullScreen == isFullScreen) {
        return;
    }

    m_isFullScreen = isFullScreen;
    emit isFullScreenChanged();

    updateItems();
}

bool VideoPanelContextMenuModel::hitPointsPanelBelowTimeline() const
{
    return m_hitPointsPanelBelowTimeline;
}

void VideoPanelContextMenuModel::setHitPointsPanelBelowTimeline(bool belowTimeline)
{
    if (m_hitPointsPanelBelowTimeline == belowTimeline) {
        return;
    }

    m_hitPointsPanelBelowTimeline = belowTimeline;
    emit hitPointsPanelBelowTimelineChanged();

    updateItems();
}

bool VideoPanelContextMenuModel::hitPointsPanelVisible() const
{
    return m_hitPointsPanelVisible;
}

void VideoPanelContextMenuModel::setHitPointsPanelVisible(bool visible)
{
    if (m_hitPointsPanelVisible == visible) {
        return;
    }

    m_hitPointsPanelVisible = visible;
    emit hitPointsPanelVisibleChanged();

    updateItems();
}

QVariantMap VideoPanelContextMenuModel::screenAvailableGeometry(int windowX, int windowY) const
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

void VideoPanelContextMenuModel::updateItems()
{
    MenuItemList items;

    //! NOTE Unlike Full screen below, this submenu isn't gated on m_floating --
    //! it's precisely the docked-narrow case (this panel docked to the
    //! left/right of the notation view) that most needs the sidebar out of
    //! the way of the horizontal space, so it must stay available there too.
    UiAction rightAction;
    rightAction.title = TranslatableString("playback", "Right");
    rightAction.code = SET_HITPOINTS_PANEL_RIGHT_ACTION;
    rightAction.checkable = Checkable::Yes;

    MenuItem* rightItem = new MenuItem(rightAction, this);
    rightItem->setId("video-panel-hitpoints-right");
    rightItem->setState(UiActionState::make_enabled(!m_hitPointsPanelBelowTimeline));

    UiAction downAction;
    downAction.title = TranslatableString("playback", "Down");
    downAction.code = SET_HITPOINTS_PANEL_DOWN_ACTION;
    downAction.checkable = Checkable::Yes;

    MenuItem* downItem = new MenuItem(downAction, this);
    downItem->setId("video-panel-hitpoints-down");
    downItem->setState(UiActionState::make_enabled(m_hitPointsPanelBelowTimeline));

    //! NOTE Show/Hide is a single toggle item (label flips, same pattern as
    //! Full screen) rather than its own Right/Down-style pair -- there's only
    //! one meaningful other state (hidden), not a choice between alternatives.
    UiAction visibilityAction;
    visibilityAction.title = m_hitPointsPanelVisible ? TranslatableString("playback", "Hide") : TranslatableString("playback", "Show");
    visibilityAction.code = TOGGLE_HITPOINTS_PANEL_VISIBLE_ACTION;

    MenuItem* visibilityItem = new MenuItem(visibilityAction, this);
    visibilityItem->setId("video-panel-hitpoints-visibility");
    visibilityItem->setState(UiActionState::make_enabled());

    MenuItemList sidebarItems { rightItem, downItem, makeSeparator(), visibilityItem };
    items << makeMenu(TranslatableString("playback", "Sidebar"), sidebarItems, "video-panel-sidebar-menu");

    if (m_floating) {
        UiAction fullScreenAction;
        fullScreenAction.title = m_isFullScreen ? TranslatableString("playback", "Exit full screen") : TranslatableString(
            "playback", "Full screen");
        fullScreenAction.code = TOGGLE_FULL_SCREEN_ACTION;

        MenuItem* fullScreenItem = new MenuItem(fullScreenAction, this);
        fullScreenItem->setId("video-panel-fullscreen");
        fullScreenItem->setState(UiActionState::make_enabled());

        items << fullScreenItem;
    }

    setItems(items);
}
