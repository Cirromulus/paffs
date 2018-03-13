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

#include "area.hpp"
#include "device.hpp"
#include "garbage_collection.hpp"
#include "paffs_trace.hpp"
#include "summaryCache.hpp"
#include <inttypes.h>
#include <string.h>

namespace paffs
{
const char* areaNames[] = {
        "UNSET", "SBLOCK", "JOURNAL", "INDEX", "DATA", "GC", "RETIRED",
        "YOUSHOULDNOTBESEEINGTHIS"};

const char* areaStatusNames[] = {"CLOSED", "ACTIVE", "EMPTY"};

const char* summaryEntryNames[] = {
        "FREE", "USED", "DIRTY", "ERROR",
};

// Returns the absolute page number from *indirect* address
PageAbs
getPageNumber(const Addr addr, Device& dev)
{
    if (extractLogicalArea(addr) >= areasNo)
    {
        PAFFS_DBG(PAFFS_TRACE_BUG,
                  "Tried accessing area %" PTYPE_AREAPOS ", but we have only %" PTYPE_AREAPOS,
                  extractLogicalArea(addr),
                  areasNo);
        return 0;
    }
    PageAbs page = dev.superblock.getPos(extractLogicalArea(addr)) * totalPagesPerArea;
    page += extractPageOffs(addr);
    if (page > areasNo * totalPagesPerArea)
    {
        PAFFS_DBG(PAFFS_TRACE_BUG, "calculated Page number out of range!");
        return 0;
    }
    return page;
}

// Returns the absolute page number from *direct* address
PageAbs
getPageNumberFromDirect(const Addr addr)
{
    PageAbs page = extractLogicalArea(addr) * totalPagesPerArea;
    page += extractPageOffs(addr);
    if (page > areasNo * totalPagesPerArea)
    {
        PAFFS_DBG(PAFFS_TRACE_BUG, "calculated Page number out of range!");
        return 0;
    }
    return page;
}

// Returns the absolute page number from *logical* address
BlockAbs
getBlockNumber(const Addr addr, Device& dev)
{
    return dev.superblock.getPos(extractLogicalArea(addr)) * blocksPerArea
           + extractPageOffs(addr) / pagesPerBlock;
}

// Returns the absolute page number from *direct* address
BlockAbs
getBlockNumberFromDirect(const Addr addr)
{
    return extractLogicalArea(addr) * blocksPerArea + extractPageOffs(addr) / pagesPerBlock;
}

// Combines the area number with the relative page starting from first page in area
Addr
combineAddress(const AreaPos logicalArea, const PageOffs page)
{
    Addr addr = 0;
    memcpy(&addr, &page, sizeof(PageOffs));
    memcpy(&reinterpret_cast<char*>(&addr)[sizeof(PageOffs)], &logicalArea, sizeof(AreaPos));
    return addr;
}

unsigned int
extractLogicalArea(const Addr addr)
{
    unsigned int area = 0;
    memcpy(&area, &reinterpret_cast<const char*>(&addr)[sizeof(PageOffs)], sizeof(AreaPos));
    return area;
}
unsigned int
extractPageOffs(const Addr addr)
{
    unsigned int page = 0;
    memcpy(&page, &addr, sizeof(PageOffs));
    return page;
}

unsigned int
AreaManagement::findWritableArea(AreaType areaType)
{
    if (dev->superblock.getActiveArea(areaType) != 0)
    {
        if (dev->superblock.getStatus(dev->superblock.getActiveArea(areaType)) != AreaStatus::active)
        {
            PAFFS_DBG(PAFFS_TRACE_BUG,
                      "ActiveArea of %s not active "
                      "(%s, %" PTYPE_AREAPOS " on %" PTYPE_AREAPOS ")",
                      areaNames[areaType],
                      areaStatusNames[dev->superblock.getStatus(dev->superblock.getActiveArea(areaType))],
                      dev->superblock.getActiveArea(areaType),
                      dev->superblock.getPos(dev->superblock.getActiveArea(areaType)));
        }
        // current Area has still space left
        if (dev->superblock.getType(dev->superblock.getActiveArea(areaType)) != areaType)
        {
            PAFFS_DBG(PAFFS_TRACE_BUG,
                      "ActiveArea does not contain correct "
                      "areaType! (Should %s, was %s)",
                      areaNames[areaType],
                      areaNames[dev->superblock.getType(dev->superblock.getActiveArea(areaType))]);
        }
        return dev->superblock.getActiveArea(areaType);
    }

    AreaPos secondBestArea = 0;
    uint32_t secondBestAreasDeletions = 0;
    if (dev->superblock.getUsedAreas() < areasNo - minFreeAreas)
    {
        /**We only take new areas, if we dont hit the reserved pool.
         * The exeption is Index area, which is needed for committing caches.
        **/
        for (unsigned int area = 0; area < areasNo; area++)
        {
            if (dev->superblock.getStatus(area) == AreaStatus::empty && dev->superblock.getPos(area) != AreaType::retired)
            {
                if(dev->superblock.getOverallDeletions() < areasNo * 2
                    || dev->superblock.getErasecount(area) <= dev->superblock.getOverallDeletions() / areasNo / 2)
                {
                    initAreaAs(area, areaType);
                    PAFFS_DBG_S(
                            PAFFS_TRACE_AREA, "Found empty Area %" PTYPE_AREAPOS " for %s", area, areaNames[areaType]);
                    return area;
                }
                else
                {
                    if(dev->superblock.getErasecount(area) < secondBestAreasDeletions
                       || secondBestArea == 0)
                    {
                        secondBestArea = area;
                    }
                }
            }
        }
        if(secondBestArea != 0)
        {
            initAreaAs(secondBestArea, areaType);
            PAFFS_DBG_S(
                    PAFFS_TRACE_AREA, "Found empty (but frequently deleted) Area %" PTYPE_AREAPOS " for %s",
                    secondBestArea, areaNames[areaType]);
            return secondBestArea;
        }
    }
    else if (dev->superblock.getUsedAreas() < areasNo)
    {
        PAFFS_DBG_S(PAFFS_TRACE_AREA, "FindWritableArea ignored reserved area");
    }

    Result r = gc.collectGarbage(areaType);
    if (r != Result::ok)
    {
        dev->lasterr = r;
        return 0;
    }

    if (dev->superblock.getStatus(dev->superblock.getActiveArea(areaType)) > AreaStatus::empty)
    {
        PAFFS_DBG(PAFFS_TRACE_BUG,
                  "garbage Collection returned invalid Status! (was %" PRId16 ", should <%" PRId16 ")",
                  dev->superblock.getStatus(dev->superblock.getActiveArea(areaType)),
                  AreaStatus::empty);
        dev->lasterr = Result::bug;
        return 0;
    }

    if (dev->superblock.getActiveArea(areaType) != 0)
    {
        PAFFS_DBG_S(PAFFS_TRACE_AREA,
                    "Found GC'ed Area %" PTYPE_AREAPOS " for %s",
                    dev->superblock.getActiveArea(areaType),
                    areaNames[areaType]);
        if (dev->superblock.getStatus(dev->superblock.getActiveArea(areaType)) != AreaStatus::active)
        {
            PAFFS_DBG(PAFFS_TRACE_BUG,
                      "An Active Area is not active after GC!"
                      " (Area %" PTYPE_AREAPOS " on %" PTYPE_AREAPOS ")",
                      dev->superblock.getActiveArea(areaType),
                      dev->superblock.getPos(dev->superblock.getActiveArea(areaType)));
            dev->lasterr = Result::bug;
            return 0;
        }
        return dev->superblock.getActiveArea(areaType);
    }

    // If we arrive here, something buggy must have happened
    PAFFS_DBG(PAFFS_TRACE_BUG, "Garbagecollection pointed to invalid area!");
    dev->lasterr = Result::bug;
    return 0;
}

Result
AreaManagement::findFirstFreePage(PageOffs& page, AreaPos area)
{
    Result r;
    for (PageOffs i = 0; i < dataPagesPerArea; i++)
    {
        if (dev->sumCache.getPageStatus(area, i, r) == SummaryEntry::free)
        {
            page = i;
            return Result::ok;
        }
        if (r != Result::ok)
        {
            return r;
        }
    }
    return Result::noSpace;
}

Result
AreaManagement::manageActiveAreaFull(AreaType areaType)
{
    PageOffs ffp;
    AreaPos area = dev->superblock.getActiveArea(areaType);
    if (area != 0 && findFirstFreePage(ffp, area) != Result::ok)
    {
        PAFFS_DBG_S(PAFFS_TRACE_AREA, "Info: Area %" PTYPE_AREAPOS " (Type %s) full.", area, areaNames[areaType]);
        // Current Area is full!
        closeArea(area);
    }
    else
    {
        // PAFFS_DBG_S(PAFFS_TRACE_AREA, "Info: Area %" PRIareapos " still page %" PRIareapos " free.", *area, ffp);
    }

    return Result::ok;
}

void
AreaManagement::initAreaAs(AreaPos area, AreaType type)
{
    if (dev->superblock.getActiveArea(type) != 0
            && dev->superblock.getActiveArea(type) != area)
    {
        PAFFS_DBG(PAFFS_TRACE_BUG,
                  "Activating area %" PTYPE_AREAPOS " while different Area "
                  "(%" PTYPE_AREAPOS " on %" PTYPE_AREAPOS ") still active!",
                  area,
                  dev->superblock.getActiveArea(type),
                  dev->superblock.getPos(dev->superblock.getActiveArea(type)));
    }
    PAFFS_DBG_S(PAFFS_TRACE_AREA,
                "Info: Init Area %" PTYPE_AREAPOS " (pos %" PTYPE_AREAPOS ") as %s.",
                static_cast<unsigned int>(area),
                static_cast<unsigned int>(dev->superblock.getPos(area)),
                areaNames[dev->superblock.getType(area)]);

    dev->journal.addEvent(journalEntry::areaMgmt::InitAreaAs(area, type));
    dev->superblock.setType(area, type);
    if (dev->superblock.getStatus(area) == AreaStatus::empty)
    {
        dev->superblock.increaseUsedAreas();
    }
    dev->superblock.setStatus(area, AreaStatus::active);
    dev->superblock.setActiveArea(type, area);
    dev->journal.addEvent(journalEntry::Checkpoint(getTopic()));
}

Result
AreaManagement::closeArea(AreaPos area)
{
    dev->journal.addEvent(journalEntry::areaMgmt::CloseArea(area));
    dev->superblock.setStatus(area, AreaStatus::closed);
    dev->superblock.setActiveArea(dev->superblock.getType(area), 0);
    PAFFS_DBG_S(PAFFS_TRACE_AREA,
                "Info: Closed %s Area %" PTYPE_AREAPOS " at pos. %" PTYPE_AREAPOS ".",
                areaNames[dev->superblock.getType(area)],
                area,
                dev->superblock.getPos(area));
    dev->journal.addEvent(journalEntry::Checkpoint(getTopic()));
    return Result::ok;
}

void
AreaManagement::retireArea(AreaPos area)
{
    dev->journal.addEvent(journalEntry::areaMgmt::RetireArea(area));
    dev->superblock.setStatus(area, AreaStatus::closed);
    if(dev->superblock.getType(area) == AreaType::unset)
    {
        dev->superblock.increaseUsedAreas();
    }
    dev->superblock.setType(area, AreaType::retired);
    for (unsigned block = 0; block < blocksPerArea; block++)
    {
        if(dev->driver.checkBad(dev->superblock.getPos(area) * blocksPerArea + block) == Result::ok)
        {   //No badblock marker found
            dev->driver.markBad(dev->superblock.getPos(area) * blocksPerArea + block);
        }
    }
    dev->journal.addEvent(journalEntry::Checkpoint(getTopic()));
    PAFFS_DBG_S(PAFFS_TRACE_AREA, "Info: RETIRED Area %" PTYPE_AREAPOS " at pos. %" PTYPE_AREAPOS ".", area, dev->superblock.getPos(area));
}

Result
AreaManagement::deleteAreaContents(AreaPos area, bool noJournalLogging)
{
    if (area >= areasNo)
    {
        PAFFS_DBG(PAFFS_TRACE_BUG,
                  "Invalid area! "
                  "Was %" PTYPE_AREAPOS ", should < %" PTYPE_AREAPOS,
                  area,
                  areasNo);
        return Result::bug;
    }
    if (dev->superblock.getPos(area) == AreaType::retired)
    {
        PAFFS_DBG(PAFFS_TRACE_BUG,
                  "Tried deleting a retired area contents! %" PTYPE_AREAPOS " on %" PTYPE_AREAPOS,
                  area,
                  dev->superblock.getPos(area));
        return Result::bug;
    }
    if (area == dev->superblock.getActiveArea(AreaType::data)
        || area == dev->superblock.getActiveArea(AreaType::index))
    {
        PAFFS_DBG(PAFFS_TRACE_BUG, "deleted content of active area %" PTYPE_AREAPOS ", is this OK?", area);
    }

    if(!noJournalLogging)
    {
        dev->journal.addEvent(journalEntry::areaMgmt::DeleteAreaContents(area));
    }
    Result r = Result::ok;
    for (unsigned int i = 0; i < blocksPerArea; i++)
    {
        r = dev->driver.eraseBlock(dev->superblock.getPos(area) * blocksPerArea + i);
        if (r != Result::ok)
        {
            PAFFS_DBG_S(PAFFS_TRACE_GC,
                        "Could not delete block n° %" PTYPE_AREAPOS " (Area %" PTYPE_AREAPOS ")!",
                        dev->superblock.getPos(area) * blocksPerArea + i,
                        area);
            retireArea(area);
            r = Result::badFlash;
            break;
        }
    }
    dev->superblock.increaseErasecount(area);
    //if area is not cached, this call gets ignored
    dev->sumCache.resetASWritten(area);

    if (r == Result::badFlash)
    {
        PAFFS_DBG_S(PAFFS_TRACE_GC,
                    "Could not delete block in area %" PTYPE_AREAPOS " "
                    "on position %" PTYPE_AREAPOS "! Retired Area.",
                    area,
                    dev->superblock.getPos(area));
        if (traceMask & (PAFFS_TRACE_AREA | PAFFS_TRACE_GC_DETAIL))
        {
            printf("Info: \n");
            for (unsigned int i = 0; i < areasNo; i++)
            {
                printf("\tArea %" PRId16 " on %" PTYPE_AREAPOS " as %10s with %3" PRIu32 " erases\n",
                       i,
                       dev->superblock.getPos(i),
                       areaNames[dev->superblock.getType(i)],
                       dev->superblock.getErasecount(i));
            }
        }
    }
    PAFFS_DBG_S(PAFFS_TRACE_AREA, "Info: Deleted Area %" PTYPE_AREAPOS " Contents at pos. %" PTYPE_AREAPOS ".", area, dev->superblock.getPos(area));
    dev->sumCache.deleteSummary(area);
    if(!noJournalLogging)
    {
        dev->journal.addEvent(journalEntry::Checkpoint(getTopic()));
    }
    return r;
}

Result
AreaManagement::deleteArea(AreaPos area)
{
    if (area >= areasNo)
    {
        PAFFS_DBG(PAFFS_TRACE_BUG,
                  "Invalid area! "
                  "Was %" PTYPE_AREAPOS ", should < %" PTYPE_AREAPOS,
                  area,
                  areasNo);
        return Result::bug;
    }
    if (dev->superblock.getType(area) == AreaType::retired)
    {
        PAFFS_DBG(PAFFS_TRACE_BUG,
                  "Tried deleting a retired area! %" PTYPE_AREAPOS " on %" PTYPE_AREAPOS,
                  area,
                  dev->superblock.getPos(area));
        return Result::bug;
    }
    if (area == dev->superblock.getActiveArea(AreaType::data)
        || area == dev->superblock.getActiveArea(AreaType::index))
    {
        PAFFS_DBG(PAFFS_TRACE_BUG, "deleted active area %" PTYPE_AREAPOS ", is this OK?", area);
    }

    dev->journal.addEvent(journalEntry::areaMgmt::DeleteArea(area));

    Result r = deleteAreaContents(area, true);
    if(r != Result::ok)
    {
        PAFFS_DBG_S(PAFFS_TRACE_AREA, "Could not delete Area %" PTYPE_AREAPOS
                    " at pos. %" PTYPE_AREAPOS ".", area, dev->superblock.getPos(area));
        return r;
    }

    dev->superblock.setStatus(area, AreaStatus::empty);
    dev->superblock.setType(area, AreaType::unset);
    dev->superblock.decreaseUsedAreas();
    PAFFS_DBG_S(PAFFS_TRACE_AREA, "Info: FREED Area %" PTYPE_AREAPOS
                " at pos. %" PTYPE_AREAPOS ".", area, dev->superblock.getPos(area));
    return r;
}

JournalEntry::Topic
AreaManagement::getTopic()
{
    return JournalEntry::Topic::areaMgmt;
}
void
AreaManagement::resetState()
{
    mUnfinishedTransaction = false;
    memset(&mLastOp, 0, sizeof(journalEntry::areaMgmt::Max));
    mLastExternOp = ExternOp::none;
}
bool
AreaManagement::isInterestedIn(const journalEntry::Max& entry)
{
    return mUnfinishedTransaction &&
            (entry.base.topic == JournalEntry::Topic::superblock ||
             entry.base.topic == JournalEntry::Topic::summaryCache);
}
Result
AreaManagement::processEntry(const journalEntry::Max& entry, JournalEntryPosition)
{
    if(entry.base.topic == JournalEntry::Topic::areaMgmt)
    {
        mUnfinishedTransaction = true;
        mLastOp = entry.areaMgmt_;
        return Result::ok;
    }

    if(!mUnfinishedTransaction)
    {   //We only allow extern entries when looking at own commands
        return Result::bug;
    }

    switch(entry.base.topic)
    {
    case JournalEntry::Topic::superblock:
        switch(entry.superblock.type)
        {
        case journalEntry::Superblock::Type::areaMap:
            switch (entry.superblock_.areaMap.operation)
            {
            case journalEntry::superblock::AreaMap::Operation::type:
                mLastExternOp = ExternOp::setType;
                break;
            case journalEntry::superblock::AreaMap::Operation::status:
                mLastExternOp = ExternOp::setStatus;
                break;
            case journalEntry::superblock::AreaMap::Operation::increaseErasecount:
                mLastExternOp = ExternOp::increaseErasecount;
                break;
            default:
                //ignore
                break;
            }
            break;
        case journalEntry::Superblock::Type::activeArea:
            mLastExternOp = ExternOp::setActiveArea;
            break;
        case journalEntry::Superblock::Type::usedAreas:
            mLastExternOp = ExternOp::changeUsedAreas;
            break;
        default:
            //ignore
            break;
        }
        break;
    case JournalEntry::Topic::summaryCache:
        switch(entry.summaryCache.subtype)
        {
        case journalEntry::SummaryCache::Subtype::remove:
            mLastExternOp = ExternOp::deleteSummary;
            break;
        case journalEntry::SummaryCache::Subtype::reset:
            mLastExternOp = ExternOp::resetASWritten;
            break;
        default:
            //ignore
            break;
        }
        break;
    default:
        //ignore
        break;
    }

    return Result::ok;
}
void
AreaManagement::signalEndOfLog()
{
    if(!mUnfinishedTransaction)
    {
        return;
    }
    switch(mLastOp.base.operation)
    {
    case journalEntry::AreaMgmt::Operation::initAreaAs:
        switch(mLastExternOp)
        {
        case ExternOp::none:
            dev->superblock.setType(mLastOp.initAreaAs.area, mLastOp.initAreaAs.type);
            //fall-through
        case ExternOp::setType:
            if (dev->superblock.getStatus(mLastOp.initAreaAs.area) == AreaStatus::empty)
            {
                dev->superblock.increaseUsedAreas();
            }
            //fall-through
        case ExternOp::changeUsedAreas:
            dev->superblock.setStatus(mLastOp.initAreaAs.area, AreaStatus::active);
            //fall-through
        case ExternOp::setStatus:
            dev->superblock.setActiveArea(mLastOp.initAreaAs.type, mLastOp.initAreaAs.area);
            break;
        default:
            //nothing
            break;
        }
        break;
    case journalEntry::AreaMgmt::Operation::closeArea:
        switch(mLastExternOp)
        {
        case ExternOp::none:
            dev->superblock.setStatus(mLastOp.closeArea.area, AreaStatus::closed);
            //fall-through
        case ExternOp::setStatus:
            dev->superblock.setActiveArea(dev->superblock.getType(mLastOp.closeArea.area), 0);
            break;
        default:
            //nothing
            break;
        }
        break;
    case journalEntry::AreaMgmt::Operation::retireArea:
        switch(mLastExternOp)
        {
        case ExternOp::none:
            dev->superblock.setStatus(mLastOp.retireArea.area, AreaStatus::closed);
            //fall-through
        case ExternOp::setStatus:
            if(dev->superblock.getType(mLastOp.retireArea.area) == AreaType::unset)
            {
                dev->superblock.increaseUsedAreas();
            }
            //fall-through
        case ExternOp::changeUsedAreas:
            dev->superblock.setType(mLastOp.retireArea.area, AreaType::retired);
            //fall-through
        case ExternOp::setType:
            for (unsigned block = 0; block < blocksPerArea; block++)
            {
                if(dev->driver.checkBad(dev->superblock.getPos(mLastOp.retireArea.area) * blocksPerArea + block) == Result::ok)
                {   //No badblock marker found
                    dev->driver.markBad(dev->superblock.getPos(mLastOp.retireArea.area) * blocksPerArea + block);
                }
            }
            break;
        default:
            //nothing
            break;
        }
        break;
    case journalEntry::AreaMgmt::Operation::deleteArea:
    case journalEntry::AreaMgmt::Operation::deleteAreaContents:
        switch(mLastExternOp)
        {
        case ExternOp::none:
            for (unsigned int i = 0; i < blocksPerArea; i++)
            {
                dev->driver.eraseBlock(dev->superblock.getPos(mLastOp.deleteAreaContents.area) * blocksPerArea + i);
            }
            dev->sumCache.resetASWritten(mLastOp.deleteAreaContents.area);
            //fall-through
        case ExternOp::resetASWritten:
            dev->sumCache.deleteSummary(mLastOp.deleteAreaContents.area);
            if(mLastOp.base.operation == journalEntry::AreaMgmt::Operation::deleteAreaContents)
            {
                break;
            }
            //fall-through
        case ExternOp::deleteSummary:
            dev->superblock.setStatus(mLastOp.deleteArea.area, AreaStatus::empty);
            //fall-through
        case ExternOp::setStatus:
            dev->superblock.setType(mLastOp.deleteArea.area, AreaType::unset);
            //fall-through
        case ExternOp::setType:
            dev->superblock.decreaseUsedAreas();
            break;
        default:
            //nothing
            break;
        }
        break;
    default:
        //nothing
        break;
    }
}
}  // namespace paffs
