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
#include "channeleditundo.h"

#include <QTimer>

#include "mixerchannelitem.h"

using namespace mu::playback;
using namespace mu::project;
using namespace muse::audio;

static bool isInstrumentChannel(const MixerChannelItem* channel)
{
    return channel->type() == MixerChannelItem::Type::PrimaryInstrument
           || channel->type() == MixerChannelItem::Type::SecondaryInstrument;
}

//! NOTE: re-invokes the channel's setter with the old/new value, on the channel found at undo/redo time
template<typename T>
static void pushChannelFieldUndo(const IProjectUndoStackPtr& undoStack, TrackId trackId, const muse::TranslatableString& actionName,
                                 void (MixerChannelItem::* setter)(T), T oldValue, T newValue)
{
    if (!undoStack) {
        return;
    }

    undoStack->push(actionName, [trackId, setter, newValue]() {
        if (MixerChannelItem* item = MixerChannelItem::findBoundChannel(trackId)) {
            (item->*setter)(newValue);
        }
    }, [trackId, setter, oldValue]() {
        if (MixerChannelItem* item = MixerChannelItem::findBoundChannel(trackId)) {
            (item->*setter)(oldValue);
        }
    });
}

void ChannelEditUndo::connectContinuousChanges(MixerChannelItem* item, QObject* context, const UndoStackGetter& undoStack)
{
    QObject::connect(item, &MixerChannelItem::volumeChangeCommitted, context, [item, undoStack](float oldValue, float newValue) {
        pushChannelFieldUndo<float>(undoStack(), item->trackId(), muse::TranslatableString("undoableAction", "Change Mixer channel volume"),
                                    &MixerChannelItem::setVolumeLevel, oldValue, newValue);
    });

    QObject::connect(item, &MixerChannelItem::balanceChangeCommitted, context, [item, undoStack](int oldValue, int newValue) {
        pushChannelFieldUndo<int>(undoStack(), item->trackId(), muse::TranslatableString("undoableAction", "Change Mixer channel pan"),
                                  &MixerChannelItem::setBalance, oldValue, newValue);
    });

    QObject::connect(item, &MixerChannelItem::gainChangeCommitted, context, [item, undoStack](int oldValue, int newValue) {
        pushChannelFieldUndo<int>(undoStack(), item->trackId(), muse::TranslatableString("undoableAction", "Change Mixer channel gain"),
                                  &MixerChannelItem::setGain, oldValue, newValue);
    });

    QObject::connect(item, &MixerChannelItem::eqChangeCommitted, context,
                     [item, undoStack](const EqParams& oldValue, const EqParams& newValue) {
        pushChannelFieldUndo<EqParams>(undoStack(), item->trackId(), muse::TranslatableString("undoableAction", "Change Mixer channel EQ"),
                                       &MixerChannelItem::setEqParams, oldValue, newValue);
    });

    QObject::connect(item, &MixerChannelItem::auxSendLevelChangeCommitted, context,
                     [item, undoStack](aux_channel_idx_t busIndex, int oldValue, int newValue) {
        const IProjectUndoStackPtr stack = undoStack();
        if (!stack) {
            return;
        }

        const TrackId trackId = item->trackId();
        auto setLevel = [trackId, busIndex](int value) {
            if (MixerChannelItem* channel = MixerChannelItem::findBoundChannel(trackId)) {
                if (AuxSendItem* send = channel->auxSendItemForBus(busIndex)) {
                    send->setAudioSignalPercentage(value);
                }
            }
        };

        stack->push(muse::TranslatableString("undoableAction", "Change Mixer aux send level"),
                    [setLevel, newValue]() { setLevel(newValue); },
                    [setLevel, oldValue]() { setLevel(oldValue); });
    });
}

void ChannelEditUndo::connectPickedChanges(MixerChannelItem* item, QObject* context, const UndoStackGetter& undoStack,
                                           const ChannelsGetter& channels)
{
    auto otherSelectedInstruments = [item, channels]() {
        QList<QPointer<MixerChannelItem> > others;
        if (item->selected() && isInstrumentChannel(item)) {
            for (MixerChannelItem* other : channels()) {
                if (other != item && other->selected() && isInstrumentChannel(other)) {
                    others.push_back(other);
                }
            }
        }
        return others;
    };

    QObject::connect(item, &MixerChannelItem::fxChangedByUser, context,
                     [item, context, undoStack, otherSelectedInstruments](const QString& oldResourceId,
                                                                          const AudioResourceMeta& newMeta) {
        const QList<QPointer<MixerChannelItem> > others = otherSelectedInstruments();

        //! NOTE: emitted before the change: these are the chains before it
        ChannelFxChains before;
        before.push_back({ item, item->outputParams().fxChain });
        for (const QPointer<MixerChannelItem>& other : others) {
            before.push_back({ other, other->outputParams().fxChain });
        }

        //! NOTE: after the slot's dropdown is done with its click (and has changed its own slot)
        QTimer::singleShot(0, context, [undoStack, others, oldResourceId, newMeta, before]() {
            for (const QPointer<MixerChannelItem>& other : others) {
                if (other) {
                    other->applyFxChange(oldResourceId, newMeta);
                }
            }

            pushFxChainsUndo(undoStack(), muse::TranslatableString("undoableAction", "Change Mixer audio effect"), before);
        });
    });

    QObject::connect(item, &MixerChannelItem::auxSendReassignedByUser, context,
                     [item, undoStack, otherSelectedInstruments](aux_channel_idx_t oldBusIndex, aux_channel_idx_t newBusIndex,
                                                                 const ChannelAuxSendsState& stateBefore) {
        //! NOTE: each changed channel's sends before and after, restored as a whole by undo/redo - levels,
        //! on/off states and slot positions come back exactly (re-assigning a bus would reset its level)
        std::vector<std::pair<TrackId, ChannelAuxSendsState> > before;
        std::vector<std::pair<TrackId, ChannelAuxSendsState> > after;
        before.push_back({ item->trackId(), stateBefore });
        after.push_back({ item->trackId(), item->auxSendsState() });

        for (const QPointer<MixerChannelItem>& other : otherSelectedInstruments()) {
            const ChannelAuxSendsState otherBefore = other->auxSendsState();

            //! NOTE: "No aux send" removes the same bus from the other selected tracks
            if (newBusIndex == AuxSendItem::NO_BUS) {
                other->clearAuxSendsTargeting(oldBusIndex);
            } else {
                other->assignAuxSend(newBusIndex);
            }

            const ChannelAuxSendsState otherAfter = other->auxSendsState();
            if (!(otherAfter == otherBefore)) {
                before.push_back({ other->trackId(), otherBefore });
                after.push_back({ other->trackId(), otherAfter });
            }
        }

        const IProjectUndoStackPtr stack = undoStack();
        if (!stack) {
            return;
        }

        auto restore = [](const std::vector<std::pair<TrackId, ChannelAuxSendsState> >& states) {
            for (const auto& [trackId, state] : states) {
                if (MixerChannelItem* channel = MixerChannelItem::findBoundChannel(trackId)) {
                    channel->restoreAuxSendsState(state);
                }
            }
        };

        stack->push(muse::TranslatableString("undoableAction", "Assign Mixer aux send"),
                    [restore, after]() { restore(after); },
                    [restore, before]() { restore(before); });
    });
}

void ChannelEditUndo::pushFxChainsUndo(const IProjectUndoStackPtr& undoStack, const muse::TranslatableString& actionName,
                                       const ChannelFxChains& before)
{
    using TrackFxChains = std::vector<std::pair<TrackId, AudioFxChain> >;
    TrackFxChains changedBefore;
    TrackFxChains changedAfter;
    for (const auto& [channel, chain] : before) {
        if (channel && channel->outputParams().fxChain != chain) {
            changedBefore.push_back({ channel->trackId(), chain });
            changedAfter.push_back({ channel->trackId(), channel->outputParams().fxChain });
        }
    }

    if (!undoStack || changedBefore.empty()) {
        return;
    }

    auto restore = [](const TrackFxChains& chains) {
        for (const auto& [trackId, chain] : chains) {
            if (MixerChannelItem* channel = MixerChannelItem::findBoundChannel(trackId)) {
                channel->restoreFxChain(chain);
            }
        }
    };

    undoStack->push(actionName,
                    [restore, changedAfter]() { restore(changedAfter); },
                    [restore, changedBefore]() { restore(changedBefore); });
}
