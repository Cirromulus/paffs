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
    uint16_t newPageListHWM;
    Addr oldPageList[maxPages];
    uint16_t oldPageListHWM;
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
        clear();
    };

    inline void clear()
    {
        journalState = JournalState::ok;
        newPageListHWM = 0;
        oldPageListHWM = 0;
    }

    inline uint16_t getMinSpaceLeft()
    {
        //newPageList is bigger or equal to oldPages, because we delete immediately
        return maxPages - newPageListHWM;
    }

    inline Result
    markPageUsed(Addr addr)
    {
        if(newPageListHWM == maxPages)
        {
            return Result::lowmem;
        }
        newPageList[newPageListHWM++] = addr;
        //TODO: unify both journal events saying the same thing
        mJournal.addEvent(journalEntry::pagestate::PageUsed(topic, addr));
        return mSummaryCache.setPageStatus(addr, SummaryEntry::used);
    }

    inline Result
    markPageOld(Addr addr)
    {
        if(oldPageListHWM == maxPages)
        {
            return Result::lowmem;
        }
        oldPageList[oldPageListHWM++] = addr;
        mJournal.addEvent(journalEntry::pagestate::PagePending(topic, addr));
        return Result::ok;
    }

    inline Result
    invalidateOldPages()
    {
        Result r;
        for(uint16_t i = 0; i < oldPageListHWM; i++)
        {
            PAFFS_DBG(PAFFS_TRACE_TREECACHE | PAFFS_TRACE_VERBOSE, "Dirtyfy %" PTYPE_AREAPOS ":%" PTYPE_PAGEOFFS,
                      extractLogicalArea(oldPageList[i]), extractPageOffs(oldPageList[i]));
            r = mSummaryCache.setPageStatus(oldPageList[i], SummaryEntry::dirty);
            if(r != Result::ok)
            {
                //TODO: rewind
                return r;
            }
        }
        mJournal.addEvent(journalEntry::pagestate::InvalidateOldPages(topic));
        oldPageListHWM = 0;
        newPageListHWM = 0;
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
                case journalEntry::Pagestate::Type::pageUsed:
                    newPageList[newPageListHWM++] = action.pageUsed.addr;
                    journalState = JournalState::invalid;
                    break;
                default:
                    PAFFS_DBG(PAFFS_TRACE_ERROR, "Invalid operation in state OK");
                    return Result::bug;
            }
            break;
        case JournalState::invalid:
            switch(entry.pagestate.type)
            {
                case journalEntry::Pagestate::Type::pageUsed:
                    newPageList[newPageListHWM++] = action.pageUsed.addr;
                    break;
                case journalEntry::Pagestate::Type::pagePending:
                    oldPageList[oldPageListHWM++] = action.pagePending.addr;
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
                case journalEntry::Pagestate::Type::pageUsed:
                case journalEntry::Pagestate::Type::pagePending:
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
            for(uint16_t i = 0; i < newPageListHWM; i++)
            {
                mSummaryCache.setPageStatus(newPageList[i], SummaryEntry::dirty);
            }
            clear();
            return false;
        case JournalState::recover:
            for(uint16_t i = 0; i < oldPageListHWM; i++)
            {
                mSummaryCache.setPageStatus(oldPageList[i], SummaryEntry::dirty);
            }
            clear();
            return true;
        }
        return false;
    }

};
}

