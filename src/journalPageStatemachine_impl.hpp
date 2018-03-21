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
#include "journalPageStatemachine.hpp"
#include "pageAddressCache.hpp"

namespace paffs
{

template <uint16_t maxPages, uint16_t maxPositions, JournalEntry::Topic topic>
PageStateMachine<maxPages, maxPositions, topic>::PageStateMachine(Journal& journal, SummaryCache& summaryCache) :
        mJournal(journal),mSummaryCache(summaryCache), mPac(nullptr)
{
    pageListHWM = maxPages;
    clear();

};

template <uint16_t maxPages, uint16_t maxPositions, JournalEntry::Topic topic>
PageStateMachine<maxPages, maxPositions, topic>::PageStateMachine(Journal& journal, SummaryCache& summaryCache,
                                                                  PageAddressCache* pac) :
        mJournal(journal),mSummaryCache(summaryCache), mPac(pac)
{
    pageListHWM = maxPages;
    clear();
};

template <uint16_t maxPages, uint16_t maxPositions, JournalEntry::Topic topic>
inline void
PageStateMachine<maxPages, maxPositions, topic>::clear()
{
    journalState = JournalState::ok;
    memset(newPageList, 0, sizeof(uint16_t) * pageListHWM);
    memset(oldPageList, 0, sizeof(uint16_t) * pageListHWM);
    pageListHWM = 0;
    currentInode = 0;
}

template <uint16_t maxPages, uint16_t maxPositions, JournalEntry::Topic topic>
inline uint16_t
PageStateMachine<maxPages, maxPositions, topic>::getMinSpaceLeft()
{
    return maxPages - pageListHWM;
}

template <uint16_t maxPages, uint16_t maxPositions, JournalEntry::Topic topic>
inline JournalState
PageStateMachine<maxPages, maxPositions, topic>::getState()
{
    return journalState;
}

template <uint16_t maxPages, uint16_t maxPositions, JournalEntry::Topic topic>
inline Result
PageStateMachine<maxPages, maxPositions, topic>::replacePage(Addr neu, Addr old)
{
    if(withPosition)
    {
        PAFFS_DBG(PAFFS_TRACE_BUG, "Tried replacing page without position in file!");
        return Result::bug;
    }
    return replacePage(neu, old, 0, 0);
}

template <uint16_t maxPages, uint16_t maxPositions, JournalEntry::Topic topic>
inline Result
PageStateMachine<maxPages, maxPositions, topic>::replacePage(Addr neu, Addr old,
                                                             InodeNo inodeNo, PageAbs pos)
{
    if(pageListHWM == maxPages)
    {
        return Result::lowMem;
    }
    oldPageList[pageListHWM] = old;
    newPageList[pageListHWM] = neu;
    if(withPosition)
    {
        PAFFS_DBG_S(PAFFS_TRACE_PAGESTATEM,
                    "mark %" PTYPE_AREAPOS ":%" PTYPE_PAGEOFFS " NEW/USED"
                    " (old %" PTYPE_AREAPOS ":%" PTYPE_PAGEOFFS " at %" PRIu16 ")"
                    " filoffs: %" PTYPE_PAGEABS,
                    extractLogicalArea(neu), extractPageOffs(neu),
                    extractLogicalArea(old), extractPageOffs(old), pageListHWM, pos);
        position   [pageListHWM] = pos;
        mJournal.addEvent(journalEntry::pagestate::ReplacePagePos(topic, neu, old, inodeNo, pos));
    }
    else
    {
        PAFFS_DBG_S(PAFFS_TRACE_PAGESTATEM, "mark %" PTYPE_AREAPOS ":%" PTYPE_PAGEOFFS " NEW/USED at %" PRIu16,
                    extractLogicalArea(neu), extractPageOffs(neu), pageListHWM);
        PAFFS_DBG_S(PAFFS_TRACE_PAGESTATEM, "\told %" PTYPE_AREAPOS ":%" PTYPE_PAGEOFFS " at %" PRIu16,
                    extractLogicalArea(old), extractPageOffs(old), pageListHWM);
        //TODO: unify both journal events saying the same thing
        mJournal.addEvent(journalEntry::pagestate::ReplacePage(topic, neu, old));
    }
    pageListHWM++;
    if(neu != 0)
    {
        return mSummaryCache.setPageStatus(neu, SummaryEntry::used);
    }
    //If we are just deleting pages
    return Result::ok;
}

template <uint16_t maxPages, uint16_t maxPositions, JournalEntry::Topic topic>
inline Result
PageStateMachine<maxPages, maxPositions, topic>::invalidateOldPages()
{
    Result r;
    bool any = false;
    for(uint16_t i = 0; i < pageListHWM; i++)
    {
        if(oldPageList[i] == 0)
        {   //skip a non-overwritten element
            continue;
        }
        any = true;
        PAFFS_DBG_S(PAFFS_TRACE_PAGESTATEM, "invalidate %" PTYPE_AREAPOS ":%" PTYPE_PAGEOFFS " at %" PRIu16,
                  extractLogicalArea(oldPageList[i]), extractPageOffs(oldPageList[i]), i);
        r = mSummaryCache.setPageStatus(oldPageList[i], SummaryEntry::dirty);
        if(r != Result::ok)
        {
            //TODO: Restore some kind of validity by rewinding
            PAFFS_DBG(PAFFS_TRACE_ERROR, "Could not set old Page dirty!");
        }
    }
    if(any)
    {
        mJournal.addEvent(journalEntry::pagestate::InvalidateOldPages(topic));
        PAFFS_DBG_S(PAFFS_TRACE_PAGESTATEM, "invalidate done");
    }
    clear();
    return Result::ok;
}

template <uint16_t maxPages, uint16_t maxPositions, JournalEntry::Topic topic>
inline Result
PageStateMachine<maxPages, maxPositions, topic>::processEntry(const journalEntry::Max& entry)
{
    const journalEntry::pagestate::Max& action = entry.pagestate_;
    switch (journalState)
    {
    case JournalState::ok:
        switch(entry.pagestate.type)
        {
        case journalEntry::Pagestate::Type::replacePagePos:
            currentInode = action.replacePagePos.nod;
            position   [pageListHWM  ] = action.replacePagePos.pos;
            //fall-through
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
            position   [pageListHWM  ] = action.replacePagePos.pos;
            //fall-through
        case journalEntry::Pagestate::Type::replacePage:
            if(pageListHWM > maxPages)
            {
                PAFFS_DBG(PAFFS_TRACE_BUG, "Too many pages to scan! (max: %" PRIu16 ")", maxPages);
            }
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
template <uint16_t maxPages, uint16_t maxPositions, JournalEntry::Topic topic>
inline JournalState
PageStateMachine<maxPages, maxPositions, topic>::signalEndOfLog()
{
    switch (journalState)
    {
    case JournalState::ok:
        clear();
        PAFFS_DBG_S(PAFFS_TRACE_PAGESTATEM, "%s All clear", topicNames[topic]);
        return JournalState::ok;
    case JournalState::invalid:
        PAFFS_DBG_S(PAFFS_TRACE_PAGESTATEM, "%s reverting written pages", topicNames[topic]);
        for(uint16_t i = 0; i < pageListHWM; i++)
        {
            if(newPageList[i] != 0)
            {   //a newPage may be 0 if we are deleting pages
                mSummaryCache.setPageStatus(newPageList[i], SummaryEntry::dirty);
            }
        }

        if(withPosition)
        {
            mPac->setJournallingInode(currentInode);
            for(uint16_t i = 0; i < pageListHWM; i++)
            {
                mPac->setPage(position[i], oldPageList[i]);
            }
            mPac->commit();
        }
        mJournal.addEvent(journalEntry::pagestate::InvalidateOldPages(topic));
        clear();
        return JournalState::invalid;
    case JournalState::recover:
        PAFFS_DBG_S(PAFFS_TRACE_PAGESTATEM, "%s recovering written pages", topicNames[topic]);
        for(uint16_t i = 0; i < pageListHWM; i++)
        {
            if(oldPageList[i] != 0)
            {
                mSummaryCache.setPageStatus(oldPageList[i], SummaryEntry::dirty);
            }
        }
        mJournal.addEvent(journalEntry::pagestate::InvalidateOldPages(topic));
        clear();
        return JournalState::recover;
    }
    return JournalState::invalid;
}

}

