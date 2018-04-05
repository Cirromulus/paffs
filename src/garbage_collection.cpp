/*
 * Copyright (c) 2016-2017, German Aerospace Center (DLR)
 *
 * This file is part of the development version of OUTPOST.
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 *
 * Authors:
 * - 2016-2017, Pascal Pieper (DLR RY-AVS)
 */
// ----------------------------------------------------------------------------

#include "garbage_collection.hpp"
#include "area.hpp"
#include "dataIO.hpp"
#include "device.hpp"
#include "driver/driver.hpp"
#include "summaryCache.hpp"
#include <inttypes.h>

namespace paffs
{

void
GarbageCollection::countDirtyAndUsedPages(PageOffs& dirty, PageOffs &used, SummaryEntry* summary)
{
    dirty = 0;
    used = 0;
    for (PageOffs i = 0; i < dataPagesPerArea; i++)
    {
        if (summary[i] == SummaryEntry::dirty)
        {
            dirty++;
        }
        if (summary[i] == SummaryEntry::used)
        {
            used++;
        }
    }
}

// Special Case 'unset': Find any Type and also extremely favour Areas with committed AS
AreaPos
GarbageCollection::findNextBestArea(AreaType target,
                                    SummaryEntry* summaryOut,
                                    bool& srcAreaContainsValidData)
{
    AreaPos favourite_area = 0;
    PageOffs favDirtyPages = 0;
    uint32_t favErases = ~0;
    srcAreaContainsValidData = false;
    SummaryEntry curr[dataPagesPerArea];

    // Look for the most dirty area.
    // This ignores unset (free) areas, if we look for data or index areas.
    for (AreaPos i = 0; i < areasNo; i++)
    {
        if (dev->superblock.getStatus(i) != AreaStatus::active
            && (dev->superblock.getType(i) == AreaType::data
                || dev->superblock.getType(i) == AreaType::index))
        {
            Result r = dev->sumCache.getSummaryStatus(i, curr);
            if (r != Result::ok)
            {
                PAFFS_DBG(PAFFS_TRACE_BUG,
                          "Could not load Summary of Area %" PRId16 " for Garbage collection!",
                          i);
                return 0;
            }
            PageOffs dirtyPages, usedPages;
            countDirtyAndUsedPages(dirtyPages, usedPages, curr);
            if (target != AreaType::unset)
            {
                // normal case
                if (dirtyPages == dataPagesPerArea
                    && ((dev->superblock.getOverallDeletions() < areasNo) ||  // Some wear leveling
                        (dev->superblock.getErasecount(i) < dev->superblock.getOverallDeletions() / areasNo)))
                {
                    // We can't find a block with more dirty pages in it
                    srcAreaContainsValidData = false;
                    memcpy(summaryOut, curr, dataPagesPerArea);
                    return i;
                }

                if (dev->superblock.getType(i) != target)
                {
                    continue;  // We cant change types if area is not completely empty
                }

                if (dirtyPages > favDirtyPages
                    || (dirtyPages != 0 && dirtyPages == favDirtyPages
                        && dev->superblock.getErasecount(i) < favErases)
                    || (dirtyPages != 0 && dirtyPages == favDirtyPages
                        && dev->sumCache.wasAreaSummaryWritten(i)))
                {
                    favourite_area = i;
                    favDirtyPages = dirtyPages;
                    srcAreaContainsValidData = usedPages > 0;
                    favErases = dev->superblock.getErasecount(i);
                    memcpy(summaryOut, curr, dataPagesPerArea);
                }
            }
            else
            {
                // Special Case for freeing committed AreaSummaries
                if (dev->sumCache.shouldClearArea(i) && dirtyPages >= favDirtyPages)
                {
                    //printf("GC shouldclear %" PTYPE_AREAPOS " with %" PTYPE_PAGEOFFS " dirty pages "
                    //        "(fav: Area %" PTYPE_AREAPOS " with %" PTYPE_PAGEOFFS ")\n",
                    //       i, dirtyPages, favourite_area, favDirtyPages);
                    favourite_area = i;
                    favDirtyPages = dirtyPages;
                    srcAreaContainsValidData = usedPages > 0;
                    memcpy(summaryOut, curr, dataPagesPerArea);
                }
            }
        }
    }

    return favourite_area;
}

/**
 * @param summary is input and output (with changed SummaryEntry::dirty to SummaryEntry::free)
 */
Result
GarbageCollection::moveValidDataToNewArea(AreaPos srcArea, AreaPos dstArea,
                                          bool& validDataLeft, SummaryEntry* summary)
{
    PAFFS_DBG_S(PAFFS_TRACE_GC_DETAIL,
                "Moving valid data from Area %" PRIu16 " (on %" PRIu16 ") to Area %" PRIu16 " (on %" PRIu16 ")",
                srcArea,
                dev->superblock.getPos(srcArea),
                dstArea,
                dev->superblock.getPos(dstArea));
    validDataLeft = false;
    FAILPOINT;
    dev->journal.addEvent(journalEntry::garbageCollection::MoveValidData(srcArea));
    FAILPOINT;
    Result ret = Result::ok;
    for (PageOffs page = 0; page < dataPagesPerArea; page++)
    {
        if (summary[page] == SummaryEntry::used)
        {
            PageAbs src = dev->superblock.getPos(srcArea) * totalPagesPerArea + page;
            PageAbs dst = dev->superblock.getPos(dstArea) * totalPagesPerArea + page;
            validDataLeft = true;
            uint8_t* buf = dev->driver.getPageBuffer();
            Result r = dev->driver.readPage(src, buf, totalBytesPerPage);
            // Any Biterror gets corrected here by being moved
            if (r != Result::ok && r != Result::biterrorCorrected)
            {
                 PAFFS_DBG(PAFFS_TRACE_ERROR, "Could not read page Area %" PTYPE_AREAPOS "(%" PTYPE_AREAPOS "):%" PTYPE_PAGEOFFS,
                 srcArea, dev->superblock.getPos(srcArea), page);
                 ret = r > ret ? r : ret;
            }
            r = dev->driver.writePage(dst, buf, totalBytesPerPage);
            if (r != Result::ok)
            {
                PAFFS_DBG(PAFFS_TRACE_ERROR,
                            "Could not write page n° %lu!",
                            static_cast<long unsigned>(dst));
                ret = Result::badFlash > ret ? Result::badFlash : ret;
            }
            FAILPOINT;
        }
        else
        {
            summary[page] = SummaryEntry::free;
        }
    }
    return ret;
}

/**
 * Changes active Area to one of the new freed areas.
 * Necessary to not have any get/setPageStatus calls!
 * This could lead to a Cache Flush which could itself cause a call on collectGarbage again
 */
Result
GarbageCollection::collectGarbage(AreaType targetType)
{
    SummaryEntry summary[dataPagesPerArea];
    memset(summary, 0xFF, dataPagesPerArea);
    bool srcAreaContainsValidData = false;
    AreaPos deletionTarget = 0;
    Result r;

    if (traceMask & PAFFS_TRACE_VERIFY_AS)
    {
        unsigned char buf[totalBytesPerPage];
        for (unsigned i = 0; i < totalPagesPerArea; i++)
        {
            Addr addr = combineAddress(dev->superblock.getActiveArea(AreaType::garbageBuffer), i);
            dev->driver.readPage(getPageNumber(addr, *dev), buf, totalBytesPerPage);
            for (unsigned j = 0; j < totalBytesPerPage; j++)
            {
                if (buf[j] != 0xFF)
                {
                    PAFFS_DBG(PAFFS_TRACE_BUG,
                              "Garbage buffer "
                              "on %" PRIu16 " is not empty!",
                              dev->superblock.getPos(
                                      dev->superblock.getActiveArea(AreaType::garbageBuffer)));
                    return Result::bug;
                }
            }
        }
    }

    deletionTarget = findNextBestArea(targetType, summary, srcAreaContainsValidData);
    if (deletionTarget == 0 ||
            (srcAreaContainsValidData && dev->superblock.getActiveArea(AreaType::garbageBuffer) == 0))
    {
        return Result::noSpace;
    }

    // TODO: more Safety switches like comparison of lastDeletion targetType

    if (srcAreaContainsValidData)
    {
        // still some valid data, copy to new area
        PAFFS_DBG_S(PAFFS_TRACE_GC_DETAIL,
                    "GC found just partially clean area %" PRIu16 " on pos %" PRIu16 "",
                    deletionTarget,
                    dev->superblock.getPos(deletionTarget));
        FAILPOINT;
        bool validDataLeft;
        r = moveValidDataToNewArea(deletionTarget,
                                   dev->superblock.getActiveArea(AreaType::garbageBuffer),
                                   validDataLeft, summary);
        //this should not differ to the information from findNextBestArea
        if(validDataLeft != true)
        {
            PAFFS_DBG(PAFFS_TRACE_BUG, "should copy valid data, but didnt"
                        "from area %" PRIu16 " to %" PRIu16 "!",
                        deletionTarget,
                        dev->superblock.getActiveArea(AreaType::garbageBuffer));
        }
        if (r != Result::ok)
        {
            PAFFS_DBG_S(PAFFS_TRACE_ERROR,
                        "Could not copy valid pages from area %" PRIu16 " to %" PRIu16 "!",
                        deletionTarget,
                        dev->superblock.getActiveArea(AreaType::garbageBuffer));
            // TODO: Handle something, maybe put area in ReadOnly or copy somewhere else..
            // TODO: Maybe copy rest of Pages before quitting
            return r;
        }
    }
    else
    {
        FAILPOINT;
         r = dev->areaMgmt.deleteArea(deletionTarget);
         if(r != Result::ok)
         {
             //No valid data lost, we are lucky.
             //Just call next round.
             return collectGarbage(targetType);
         }
    }

    FAILPOINT;
    // swap logical position of areas to keep addresses valid
    dev->superblock.swapAreaPosition(deletionTarget,
                                   dev->superblock.getActiveArea(AreaType::garbageBuffer));

    if(srcAreaContainsValidData)
    {
        FAILPOINT;
        r = dev->areaMgmt.deleteAreaContents(deletionTarget,
                         dev->superblock.getActiveArea(AreaType::garbageBuffer));
        if(r != Result::ok)
        {
            PAFFS_DBG_S(PAFFS_TRACE_ALWAYS, "Could not delete Area! Giving up Garbage buffer to continue...");
            //TODO Find new place for GC in desperate mode
            dev->superblock.setActiveArea(AreaType::garbageBuffer, 0);
        }
        // Copy the updated (no SummaryEntry::dirty pages) summary to the deletion_target
        // (it will be the fresh area!)
        r = dev->sumCache.setSummaryStatus(deletionTarget, summary);
        if (r != Result::ok)
        {
            PAFFS_DBG_S(PAFFS_TRACE_ERROR,
                        "Could not remove dirty entries in AS of area %" PRId16 "",
                        deletionTarget);
            return r;
        }
    }

    if (targetType != AreaType::unset)
    {
        FAILPOINT;
        dev->areaMgmt.initAreaAs(deletionTarget, targetType);
    }
    FAILPOINT;
    dev->journal.addEvent(journalEntry::Checkpoint(getTopic()));

    PAFFS_DBG_S(PAFFS_TRACE_GC_DETAIL,
                "Garbagecollection erased pos %" PRIu16 " and gave area %" PRIu16 " pos %" PRIu16 ".",
                dev->superblock.getPos(dev->superblock.getActiveArea(AreaType::garbageBuffer)),
                deletionTarget,
                dev->superblock.getPos(deletionTarget));

    return Result::ok;
}

JournalEntry::Topic
GarbageCollection::getTopic()
{
    return JournalEntry::Topic::garbage;
}
void
GarbageCollection::resetState()
{
    state = Statemachine::ok;
    journalTargetArea = 0;
}
bool
GarbageCollection::isInterestedIn(const journalEntry::Max& entry)
{
    return state != Statemachine::ok &&
            (entry.base.topic == JournalEntry::Topic::areaMgmt ||
             entry.base.topic == JournalEntry::Topic::summaryCache ||
             entry.base.topic == JournalEntry::Topic::superblock);
}
Result
GarbageCollection::processEntry(const journalEntry::Max& entry, JournalEntryPosition)
{
    switch(entry.base.topic)
    {
    case JournalEntry::Topic::garbage:
        if(entry.garbage.operation == journalEntry::GarbageCollection::Operation::moveValidData)
        {
            state = Statemachine::moveValidData;
            journalTargetArea = entry.garbage_.moveValidData.from;
            journalTargetAreaType = dev->superblock.getType(journalTargetArea);
        }
        break;
    case JournalEntry::Topic::areaMgmt:
        switch(entry.areaMgmt.operation)
        {
        case journalEntry::AreaMgmt::Operation::deleteArea:
        case journalEntry::AreaMgmt::Operation::deleteAreaContents:
            state = Statemachine::deletedOldArea;
            break;
        default:
            //ignore
            break;
        }
        break;
    case JournalEntry::Topic::superblock:
        if(entry.superblock.type != journalEntry::Superblock::Type::areaMap)
        {
            break;
        }
        switch(entry.superblock_.areaMap.operation)
        {
        case journalEntry::superblock::AreaMap::Operation::swap:
            state = Statemachine::swappedPosition;
            break;
        default:
            //ignore
            break;
        }
        break;
    case JournalEntry::Topic::summaryCache:
        if(entry.summaryCache.subtype == journalEntry::SummaryCache::Subtype::setStatusBlock)
        {
            state = Statemachine::setNewSummary;
        }
        break;
    default:
        //ignore
        break;
    }
    return Result::ok;
}
void
GarbageCollection::signalEndOfLog()
{
    SummaryEntry summary[dataPagesPerArea];
    switch(state)
    {
    case Statemachine::ok:
        return;
    case Statemachine::moveValidData:
        PAFFS_DBG_S(PAFFS_TRACE_GC | PAFFS_TRACE_JOURNAL,
                    "deleting copied data");
        dev->areaMgmt.deleteAreaContents(dev->superblock.getActiveArea(AreaType::garbageBuffer), 0);
        break;
    case Statemachine::swappedPosition:
        PAFFS_DBG_S(PAFFS_TRACE_GC | PAFFS_TRACE_JOURNAL,
                    "appying copied data");
        dev->areaMgmt.deleteAreaContents(journalTargetArea,
                            dev->superblock.getActiveArea(AreaType::garbageBuffer));
        //fall-through
    case Statemachine::deletedOldArea:
        dev->sumCache.scanAreaForSummaryStatus(journalTargetArea, summary);
        {
            bool containsData = false;
            for(PageOffs i = 0; i < dataPagesPerArea; i++)
            {
                if(summary[i] != SummaryEntry::free)
                {
                    containsData = true;
                    break;
                }
            }
            if(containsData)
            {
                dev->sumCache.setSummaryStatus(journalTargetArea, summary);
                if(journalTargetAreaType == AreaType::unset)
                {
                    PAFFS_DBG(PAFFS_TRACE_BUG, "UNSET Type containing Data");
                    break;
                }
                if(dev->superblock.getActiveArea(journalTargetAreaType) == 0)
                {   //We were about to search for a writable area
                    dev->areaMgmt.initAreaAs(journalTargetArea, journalTargetAreaType);
                }
            }
            else
            {

            }
        }
        //fall-through
    case Statemachine::setNewSummary:
        break;
    }
    dev->journal.addEvent(journalEntry::Checkpoint(getTopic()));
}

}
