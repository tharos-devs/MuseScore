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

#include <QVariantMap>

#include "actions/actionable.h"
#include "uicomponents/qml/Muse/UiComponents/abstractmenumodel.h"

namespace mu::playback {
class VideoPanelContextMenuModel : public muse::uicomponents::AbstractMenuModel, public muse::actions::Actionable
{
    Q_OBJECT

    //! NOTE Full screen only makes sense once this panel is its own floating
    //! window -- when docked, "full screen" would apply to the whole MuseScore
    //! window instead (there's no separate window to fullscreen), which isn't
    //! what the option promises. Fed in from outside (this model has no way to
    //! know the panel's dock state on its own); the item is added/removed from
    //! the menu as this changes.
    Q_PROPERTY(bool floating READ floating WRITE setFloating NOTIFY floatingChanged)

    //! NOTE Whether the floating window is currently full screen -- fed in
    //! from outside (this model has no window of its own to check). Flips the
    //! menu item's label between "Full screen" and "Exit full screen" so it
    //! always describes what clicking it will do next, not just repeats a
    //! static toggle name.
    Q_PROPERTY(bool isFullScreen READ isFullScreen WRITE setIsFullScreen NOTIFY isFullScreenChanged)

    //! NOTE Whether the hit-points sidebar currently sits below the timeline
    //! rather than beside it -- fed in from VideoPanel.qml (this model doesn't
    //! own the panel's layout). Unlike Full screen, this item is available in
    //! both the docked and floating states: it's precisely the docked-narrow
    //! case (e.g. this panel docked to the left/right of the notation view)
    //! that most needs the sidebar out of the way of the horizontal space.
    Q_PROPERTY(
        bool hitPointsPanelBelowTimeline READ hitPointsPanelBelowTimeline WRITE setHitPointsPanelBelowTimeline NOTIFY hitPointsPanelBelowTimelineChanged)

    //! NOTE Whether the hit-points sidebar is currently shown at all -- fed in
    //! from VideoPanel.qml, same reasoning as hitPointsPanelBelowTimeline
    //! above. Flips the "Sidebar > Show/Hide" item's label, same pattern as
    //! Full screen's own label flip.
    Q_PROPERTY(bool hitPointsPanelVisible READ hitPointsPanelVisible WRITE setHitPointsPanelVisible NOTIFY hitPointsPanelVisibleChanged)

    QML_ELEMENT

public:
    explicit VideoPanelContextMenuModel(QObject* parent = nullptr);

    Q_INVOKABLE void load() override;

    //! NOTE The reachable area of whichever screen the point (windowX, windowY)
    //! is on, i.e. QScreen::availableGeometry() -- the screen's full geometry
    //! minus whatever the OS reserves for itself there (the menu bar on macOS,
    //! the taskbar on Windows, panels on Linux desktop environments). Used by
    //! the "Full screen" toggle to fill a floating window's screen without
    //! landing underneath any of that reserved space; Qt resolves this
    //! correctly per platform, so it stays correct on macOS, Windows and
    //! Linux alike without any platform-specific handling here. Falls back
    //! to the primary screen if no screen contains that point. Returns
    //! {"x", "y", "width", "height"}.
    Q_INVOKABLE QVariantMap screenAvailableGeometry(int windowX, int windowY) const;

    bool floating() const;
    void setFloating(bool floating);

    bool isFullScreen() const;
    void setIsFullScreen(bool isFullScreen);

    bool hitPointsPanelBelowTimeline() const;
    void setHitPointsPanelBelowTimeline(bool belowTimeline);

    bool hitPointsPanelVisible() const;
    void setHitPointsPanelVisible(bool visible);

signals:
    //! NOTE Actually toggling full screen needs a QQuickWindow (via the
    //! Window attached property), which this menu-item-list model has no
    //! business reaching into -- QML handles it on receiving this.
    void toggleFullScreenRequested();
    void floatingChanged();
    void isFullScreenChanged();

    //! NOTE Actually moving/showing the sidebar needs the panel's own
    //! SplitView, which this menu-item-list model has no business reaching
    //! into -- QML handles it on receiving these, same as
    //! toggleFullScreenRequested above. setHitPointsPanelBelowTimelineRequested
    //! sets the position directly (Right/Down are independent picks, not a
    //! toggle -- clicking the option that's already active is a no-op).
    void setHitPointsPanelBelowTimelineRequested(bool belowTimeline);
    void hitPointsPanelBelowTimelineChanged();
    void toggleHitPointsPanelVisibleRequested();
    void hitPointsPanelVisibleChanged();

private:
    void updateItems();

    bool m_floating = false;
    bool m_isFullScreen = false;
    bool m_hitPointsPanelBelowTimeline = false;
    bool m_hitPointsPanelVisible = true;
};
}
