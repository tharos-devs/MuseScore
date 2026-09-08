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

#include "auxsenditem.h"

#include "translation.h"

using namespace mu::playback;
using namespace muse::audio;

static const QString NO_AUX_SEND_ID("noAuxSend");
static const QString ADD_AUX_SEND_ID("addAuxSend");
static const QString ADD_AUX_BUS_ID("addAuxBus");
static const QString ADD_GROUP_BUS_ID("addGroupBus");

AuxSendItem::AuxSendItem(QObject* parent)
    : AbstractAudioResourceItem(parent)
{
}

QString AuxSendItem::id() const
{
    return QString::number(m_auxIndex);
}

aux_channel_idx_t AuxSendItem::auxIndex() const
{
    return m_auxIndex;
}

void AuxSendItem::setAuxIndex(aux_channel_idx_t index)
{
    if (m_auxIndex == index) {
        return;
    }

    bool wasBlank = isBlank();

    m_auxIndex = index;
    emit auxIndexChanged();

    if (isBlank() != wasBlank) {
        emit isBlankChanged();
    }
}

QString AuxSendItem::title() const
{
    if (m_isDragging) {
        return QString::number(m_audioSignalPercentage) + "%";
    }

    return m_title;
}

bool AuxSendItem::isBlank() const
{
    //! NOTE: based on auxIndex() alone, not isActive/audioSignalPercentage - a real,
    //! assigned send that's bypassed and pulled to 0% must stay visually distinct from a
    //! slot that was never assigned to any bus in the first place
    return m_auxIndex == NO_BUS;
}

bool AuxSendItem::isActive() const
{
    return m_isActive;
}

int AuxSendItem::audioSignalPercentage() const
{
    return m_audioSignalPercentage;
}

bool AuxSendItem::isDragging() const
{
    return m_isDragging;
}

void AuxSendItem::setMenuDataProvider(const MenuDataProvider& provider)
{
    m_menuDataProvider = provider;
}

void AuxSendItem::setMenuItemHandler(const MenuItemHandler& handler)
{
    m_menuItemHandler = handler;
}

void AuxSendItem::requestAvailableResources()
{
    if (!m_menuDataProvider) {
        return;
    }

    MenuData data = m_menuDataProvider();

    //! NOTE: whether a bus is actually targeted must be based on auxIndex(), not isBlank() -
    //! isBlank() reflects isActive+percentage (needed for the engine's blank-slot sentinel)
    //! and can be true even for a slot that is still assigned to a real bus (e.g. bypassed
    //! with its knob at 0%), which would otherwise show "No aux send" checked while a real
    //! bus's name is still displayed, and could let another slot claim the same bus
    bool hasTarget = (m_auxIndex != NO_BUS);

    QVariantList result;

    result << buildMenuItem(NO_AUX_SEND_ID, muse::qtrc("playback", "No aux send"), !hasTarget);

    if (!data.availableBuses.empty()) {
        result << buildSeparator();

        for (const BusOption& option : data.availableBuses) {
            bool checked = hasTarget && option.index == m_auxIndex;
            result << buildMenuItem(QString::number(option.index), option.title, checked);
        }
    }

    if (data.canAddSend || data.canAddBus || data.canAddGroupBus) {
        result << buildSeparator();

        if (data.canAddSend) {
            result << buildMenuItem(ADD_AUX_SEND_ID, muse::qtrc("playback", "Add Aux send"), false);
        }

        if (data.canAddBus) {
            result << buildMenuItem(ADD_AUX_BUS_ID, muse::qtrc("playback", "Add Aux channel"), false);
        }

        if (data.canAddGroupBus) {
            result << buildMenuItem(ADD_GROUP_BUS_ID, muse::qtrc("playback", "Add Bus channel"), false);
        }
    }

    emit availableResourceListResolved(result);
}

void AuxSendItem::handleMenuItem(const QString& menuItemId)
{
    if (m_menuItemHandler) {
        m_menuItemHandler(menuItemId);
    }
}

void AuxSendItem::setTitle(const QString& title)
{
    if (m_title == title) {
        return;
    }

    m_title = title;
    emit titleChanged();
}

void AuxSendItem::setIsActive(bool active)
{
    if (m_isActive == active) {
        return;
    }

    m_isActive = active;
    emit isActiveChanged();
}

void AuxSendItem::setAudioSignalPercentage(int percentage)
{
    if (m_audioSignalPercentage == percentage) {
        return;
    }

    m_audioSignalPercentage = percentage;
    emit audioSignalPercentageChanged(percentage);

    if (m_isDragging) {
        emit titleChanged();
    }
}

void AuxSendItem::setIsDragging(bool dragging)
{
    if (m_isDragging == dragging) {
        return;
    }

    m_isDragging = dragging;
    emit isDraggingChanged();
    emit titleChanged();
}
