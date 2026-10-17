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
#include "engravingproject.h"

#include "global/allocator.h"

#include "rw/inoutdata.h"
#include "rw/mscloader.h"
#include "rw/mscsaver.h"
#include "style/defaultstyle.h"
#include "dom/chord.h"
#include "dom/masterscore.h"
#include "dom/note.h"
#include "dom/part.h"
#include "dom/segment.h"

#include "log.h"

using namespace muse;
using namespace mu;
using namespace mu::engraving;

std::shared_ptr<EngravingProject> EngravingProject::create(const modularity::ContextPtr& iocCtx)
{
    std::shared_ptr<EngravingProject> p = std::shared_ptr<EngravingProject>(new EngravingProject(iocCtx));
    if (p->engravingElementsProvider()) {
        p->engravingElementsProvider()->clearStatistic();
    }
    p->init(DefaultStyle::defaultStyle());
    return p;
}

std::shared_ptr<EngravingProject> EngravingProject::create(const MStyle& style, const modularity::ContextPtr& iocCtx)
{
    std::shared_ptr<EngravingProject> p = std::shared_ptr<EngravingProject>(new EngravingProject(iocCtx));
    if (p->engravingElementsProvider()) {
        p->engravingElementsProvider()->clearStatistic();
    }
    p->init(style);
    return p;
}

EngravingProject::EngravingProject(const modularity::ContextPtr& iocCtx)
    : muse::Contextable(iocCtx)
{
    muse::ObjectAllocator::used();
}

EngravingProject::~EngravingProject()
{
    delete m_masterScore;

    muse::ObjectAllocator::unused();

    // muse::AllocatorsRegister::instance()->printStatistic("=== Destroy engraving project ===");
    //! NOTE At the moment, the allocator is working as leak detector. No need to do cleanup, at the moment it can lead to crashes
    // AllocatorsRegister::instance()->cleanupAll("engraving");
}

void EngravingProject::init(const MStyle& style)
{
    m_masterScore = new MasterScore(iocContext(), style, weak_from_this());
}

IFileInfoProviderPtr EngravingProject::fileInfoProvider() const
{
    return m_masterScore->fileInfo();
}

void EngravingProject::setFileInfoProvider(IFileInfoProviderPtr fileInfoProvider)
{
    m_masterScore->setFileInfoProvider(fileInfoProvider);
}

String EngravingProject::appVersion() const
{
    return m_masterScore->mscoreVersion();
}

int EngravingProject::mscVersion() const
{
    return m_masterScore->mscVersion();
}

bool EngravingProject::readOnly() const
{
    return m_masterScore->readOnly();
}

void EngravingProject::setMasterScore(MasterScore* score)
{
    m_masterScore = score;
}

//! NOTE: these note properties only matter for playback, which is built from the master score: they are always
//! linked, but files saved before that may have different values in a part (edited there, never heard)
static void syncPartNotePlaybackPropertiesWithMaster(MasterScore* masterScore)
{
    static constexpr Pid PROPERTIES[] = {
        Pid::VELO_TYPE, Pid::USER_VELOCITY, Pid::TUNING, Pid::PLAYBACK_START_OFFSET, Pid::PLAYBACK_DURATION_OFFSET
    };

    const auto syncNote = [masterScore](Note* note) {
        const EngravingItem* masterNote = note->findLinkedInScore(masterScore);
        if (!masterNote) {
            return;
        }
        for (const Pid pid : PROPERTIES) {
            const PropertyValue masterValue = masterNote->getProperty(pid);
            if (note->getProperty(pid) != masterValue) {
                note->setProperty(pid, masterValue);
            }
        }
    };

    for (Score* score : masterScore->scoreList()) {
        if (score->isMaster()) {
            continue;
        }
        for (Segment* segment = score->firstSegment(SegmentType::ChordRest); segment;
             segment = segment->next1(SegmentType::ChordRest)) {
            for (EngravingItem* item : segment->elist()) {
                if (!item || !item->isChord()) {
                    continue;
                }
                Chord* chord = toChord(item);
                for (Chord* grace : chord->graceNotes()) {
                    for (Note* note : grace->notes()) {
                        syncNote(note);
                    }
                }
                for (Note* note : chord->notes()) {
                    syncNote(note);
                }
            }
        }
    }
}

Ret EngravingProject::setupMasterScore(bool forceMode)
{
    TRACEFUNC;

    syncPartNotePlaybackPropertiesWithMaster(m_masterScore);

    m_masterScore->updatePaddingTables();
    for (Score* s : m_masterScore->scoreList()) {
        s->updatePaddingTables();
    }

    m_masterScore->setLayoutAll();
    m_masterScore->update();
    m_masterScore->initAutomation();

    Ret ret = checkCorrupted();
    m_isCorruptedUponLoading = !ret;

    return forceMode ? muse::make_ok() : ret;
}

MasterScore* EngravingProject::masterScore() const
{
    return m_masterScore;
}

Ret EngravingProject::loadMscz(const MscReader& msc, rw::ReadInOutData* data, bool ignoreVersionError)
{
    TRACEFUNC;

    MScore::setError(MsError::MS_NO_ERROR);

    MscLoader loader;
    return loader.loadMscz(m_masterScore, msc, data, ignoreVersionError);
}

bool EngravingProject::writeMscz(MscWriter& writer, bool createThumbnail, const write::WriteContext* ctx)
{
    TRACEFUNC;

    MscSaver saver(iocContext());
    return saver.writeMscz(m_masterScore, writer, createThumbnail, ctx);
}

bool EngravingProject::isCorruptedUponLoading() const
{
    return m_isCorruptedUponLoading;
}

Ret EngravingProject::checkCorrupted() const
{
    TRACEFUNC;

    return m_masterScore->sanityCheck();
}
