/*
 * SPDX-License-Identifier: GPL-3.0-only
 * MuseScore-Studio-CLA-applies
 *
 * MuseScore Studio
 * Music Composition & Notation
 *
 * Copyright (C) 2023 MuseScore Limited and others
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

#include <functional>
#include <limits>
#include <vector>

#include <qqmlintegration.h>

#include "audio/common/audiotypes.h"

#include "abstractaudioresourceitem.h"

namespace mu::playback {
class AuxSendItem : public AbstractAudioResourceItem
{
    Q_OBJECT

    Q_PROPERTY(QString id READ id NOTIFY auxIndexChanged)
    Q_PROPERTY(bool isActive READ isActive WRITE setIsActive NOTIFY isActiveChanged)
    Q_PROPERTY(int audioSignalPercentage READ audioSignalPercentage WRITE setAudioSignalPercentage NOTIFY audioSignalPercentageChanged)
    Q_PROPERTY(bool isDragging READ isDragging WRITE setIsDragging NOTIFY isDraggingChanged)

    QML_ELEMENT;
    QML_UNCREATABLE("Must be created in C++ only")

public:
    struct BusOption {
        muse::audio::aux_channel_idx_t index = 0;
        QString title;
        bool isGroupBus = false;
    };

    struct MenuData {
        std::vector<BusOption> availableBuses;
        bool canAddSend = false;
        bool canAddBus = false;
        bool canAddGroupBus = false;
    };

    using MenuDataProvider = std::function<MenuData ()>;
    using MenuItemHandler = std::function<void (const QString& menuItemId)>;

    //! NOTE: sentinel meaning "not yet routed to any bus" - a blank slot's stable
    //! visual position (its slot order) is independent from which bus it targets
    static constexpr muse::audio::aux_channel_idx_t NO_BUS = std::numeric_limits<muse::audio::aux_channel_idx_t>::max();

    explicit AuxSendItem(QObject* parent = nullptr);

    QString id() const;

    muse::audio::aux_channel_idx_t auxIndex() const;
    void setAuxIndex(muse::audio::aux_channel_idx_t index);

    QString title() const override;
    bool isBlank() const override;
    bool isActive() const override;

    int audioSignalPercentage() const;
    bool isDragging() const;

    void requestAvailableResources() override;
    void handleMenuItem(const QString& menuItemId) override;

    void setMenuDataProvider(const MenuDataProvider& provider);
    void setMenuItemHandler(const MenuItemHandler& handler);

public slots:
    void setTitle(const QString& title);
    void setIsActive(bool active);
    void setAudioSignalPercentage(int percentage);
    void setIsDragging(bool dragging);

signals:
    void audioSignalPercentageChanged(int percentage);
    void auxIndexChanged();
    void isDraggingChanged();

private:
    MenuDataProvider m_menuDataProvider;
    MenuItemHandler m_menuItemHandler;

    muse::audio::aux_channel_idx_t m_auxIndex = NO_BUS;
    QString m_title;
    bool m_isActive = false;
    int m_audioSignalPercentage = 0;
    //! NOTE: while true, title() reports the current percentage instead of the target
    //! bus name - lets the knob's own slot button show a live value readout in place
    //! while its value is being dragged, matching this control's pre-redesign behavior
    bool m_isDragging = false;
};
}
