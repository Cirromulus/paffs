/*
 * Copyright (c) 2017, German Aerospace Center (DLR)
 *
 * This file is part of the development version of OUTPOST.
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 *
 * Authors:
 * - 2017, Pascal Pieper (DLR RY-AVS)
 */
// ----------------------------------------------------------------------------

#pragma once
#include "commonTypes.hpp"
#include "journalEntry.hpp"
#include "journal.hpp"
#include "summaryCache.hpp"
#include "area.hpp"

namespace paffs
{
template <uint16_t maxPages, JournalEntry::Topic topic>
class PageStateMachine
{
    Journal& mJournal;
    SummaryCache& mSummaryCache;

    Addr newPageList[maxPages];
    Addr oldPageList[maxPages];
    uint16_t pageListHWM;
    enum class JournalState : uint8_t
    {
        ok,
        invalid,
        recover,
    } journalState;
public:
    inline
    PageStateMachine(Journal& journal, SummaryCache& summaryCache) :
        mJournal(journal),mSummaryCache(summaryCache)
    {
        pageListHWM = maxPages;
        clear();
    };

    inline void clear()
    {
        journalState = JournalState::ok;
        memset(newPageList, 0, sizeof(uint16_t) * pageListHWM);
        memset(oldPageList, 0, sizeof(uint16_t) * pageListHWM);
        pageListHWM = 0;
    }

    inline uint16_t getMinSpaceLeft()
    {
        return maxPages - pageListHWM;
    }

    inline Result
    replacePage(Addr neu, Addr old)
    {
        if(pageListHWM == maxPages)
        {
            return Result::lowmem;
        }
        PAFFS_DBG_S(PAFFS_TRACE_PAGESTATEM, "mark %" PTYPE_AREAPOS ":%" PTYPE_PAGEOFFS " NEW/USED at %" PRIu16,
                    extractLogicalArea(neu), extractPageOffs(neu), pageListHWM);
        PAFFS_DBG_S(PAFFS_TRACE_PAGESTATEM, "     %" PTYPE_AREAPOS ":%" PTYPE_PAGEOFFS "   old    at %" PRIu16,
                    extractLogicalArea(old), extractPageOffs(old), pageListHWM);
        newPageList[pageListHWM  ] = neu;
        oldPageList[pageListHWM++] = old;
        //TODO: unify both journal events saying the same thing
        mJournal.addEvent(journalEntry::pagestate::ReplacePage(topic, neu, old));
        return mSummaryCache.setPageStatus(neu, SummaryEntry::used);
    }

    inline Result
    invalidateOldPages()
    {
        Result r;
        bool any = false;
        for(uint16_t i = 0; i < pageListHWM; i++)
        {
            if(oldPageList[i] == 0)
            {
                continue;
            }
            any = true;
            PAFFS_DBG_S(PAFFS_TRACE_PAGESTATEM, "invalidate %" PTYPE_AREAPOS ":%" PTYPE_PAGEOFFS " at %" PRIu16,
                      extractLogicalArea(oldPageList[i]), extractPageOffs(oldPageList[i]), i);
            r = mSummaryCache.setPageStatus(oldPageList[i], SummaryEntry::dirty);
            if(r != Result::ok)
            {
                //TODO: rewind
                PAFFS_DBG(PAFFS_TRACE_ERROR, "Could not set old Page dirty!");
            }
        }
        if(any)
        {
            mJournal.addEvent(journalEntry::pagestate::InvalidateOldPages(topic));
        }
        PAFFS_DBG_S(PAFFS_TRACE_PAGESTATEM, "invalidate done");
        clear();
        return Result::ok;
    }

    inline Result
    processEntry(const journalEntry::Max& entry)
    {
        const journalEntry::pagestate::Max& action = entry.pagestate_;
        switch (journalState)
        {
        case JournalState::ok:
            switch(entry.pagestate.type)
            {
            case journalEntry::Pagestate::Type::replacePagePos:
                PAFFS_DBG(PAFFS_TRACE_BUG, "processed entry with Position");
                return Result::bug;
            case journalEntry::Pagestate::Type::replacePage:
                newPageList[pageListHWM  ] = action.replacePage.neu;
                oldPageList[pageListHWM++] = action.replacePage.old;
                journalState = JournalState::invalid;
                break;
            case journalEntry::Pagestate::Type::success:
                //This is only temporary as long as PAC produces success messages after valid
                return Result::ok;
            default:
                PAFFS_DBG(PAFFS_TRACE_ERROR, "Invalid operation in state OK");
                return Result::bug;
            }
            break;
        case JournalState::invalid:
            switch(entry.pagestate.type)
            {
            case journalEntry::Pagestate::Type::replacePagePos:
                PAFFS_DBG(PAFFS_TRACE_BUG, "processed entry with Position");
                return Result::bug;
            case journalEntry::Pagestate::Type::replacePage:
                newPageList[pageListHWM  ] = action.replacePage.neu;
                oldPageList[pageListHWM++] = action.replacePage.old;
                break;
            case journalEntry::Pagestate::Type::success:
                journalState = JournalState::recover;
                break;
            case journalEntry::Pagestate::Type::invalidateOldPages:
                PAFFS_DBG(PAFFS_TRACE_ERROR, "Invalid operation in state INVALID");
                return Result::bug;
            }
            break;
        case JournalState::recover:
            switch(entry.pagestate.type)
            {
            case journalEntry::Pagestate::Type::replacePagePos:
            case journalEntry::Pagestate::Type::replacePage:
            case journalEntry::Pagestate::Type::success:
                PAFFS_DBG(PAFFS_TRACE_ERROR, "Invalid operation in state RECOVER");
                return Result::bug;
            case journalEntry::Pagestate::Type::invalidateOldPages:
                //The only time we should see this is if we broke down before setting checkpoint
                //TODO: Should we produce a checkpoint now? Maybe not, because PAC has many cycles
                //through statemachine until checkpoint comes
                //mJournal.addEvent(journalEntry::Checkpoint(topic));
                journalState = JournalState::ok;
                clear();
                break;
            }
            break;
        }
        return Result::ok;
    }

    /**
     * @return true, if commit was successful
     * \warn this strongly relies on PAC having been set to the correct Inode!
     */
    inline bool
    signalEndOfLog()
    {
        switch (journalState)
        {
        case JournalState::ok:
            clear();
            return true;
        case JournalState::invalid:
            for(uint16_t i = 0; i < pageListHWM; i++)
            {
                mSummaryCache.setPageStatus(newPageList[i], SummaryEntry::dirty);
            }
            clear();
            return false;
        case JournalState::recover:
            for(uint16_t i = 0; i < pageListHWM; i++)
            {
                if(oldPageList[i] != 0)
                {
                    mSummaryCache.setPageStatus(oldPageList[i], SummaryEntry::dirty);
                }
            }
            clear();
            return true;
        }
        return false;
    }

};
}

