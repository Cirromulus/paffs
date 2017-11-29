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
PageOffs
GarbageCollection::countDirtyPages(SummaryEntry* summary)
{
    PageOffs dirty = 0;
    for (PageOffs i = 0; i < dataPagesPerArea; i++)
    {
        if (summary[i] != SummaryEntry::used)
            dirty++;
    }
    return dirty;
}

// Special Case 'unset': Find any Type and also extremely favour Areas with committed AS
AreaPos
GarbageCollection::findNextBestArea(AreaType target,
                                    SummaryEntry* summaryOut,
                                    bool* srcAreaContainsData)
{
    AreaPos favourite_area = 0;
    PageOffs favDirtyPages = 0;
    uint32_t favErases = ~0;
    *srcAreaContainsData = true;
    SummaryEntry curr[dataPagesPerArea];

    // Look for the most dirty area.
    // This ignores unset (free) areas, if we look for data or index areas.
    for (AreaPos i = 0; i < areasNo; i++)
    {
        if (dev->areaMgmt.getStatus(i) != AreaStatus::active
            && (dev->areaMgmt.getType(i) == AreaType::data
                || dev->areaMgmt.getType(i) == AreaType::index))
        {
            Result r = dev->sumCache.getSummaryStatus(i, curr);
            PageOffs dirtyPages = countDirtyPages(curr);
            if (r != Result::ok)
            {
                PAFFS_DBG(PAFFS_TRACE_BUG,
                          "Could not load Summary of Area %" PRId16 " for Garbage collection!",
                          i);
                return 0;
            }
            if (dirtyPages == dataPagesPerArea
                && (dev->areaMgmt.getOverallDeletions() < areasNo ||  // Some wear leveling
                    dev->areaMgmt.getErasecount(i) < dev->areaMgmt.getOverallDeletions() / areasNo))
            {
                // We can't find a block with more dirty pages in it
                *srcAreaContainsData = false;
                memcpy(summaryOut, curr, dataPagesPerArea);
                return i;
            }

            if (target != AreaType::unset)
            {
                // normal case
                if (dev->areaMgmt.getType(i) != target)
                    continue;  // We cant change types if area is not completely empty

                if (dirtyPages > favDirtyPages
                    || (dirtyPages != 0 && dirtyPages == favDirtyPages
                        && dev->areaMgmt.getErasecount(i) < favErases)
                    || (dirtyPages != 0 && dirtyPages == favDirtyPages
                        && dev->sumCache.wasASWritten(i)))
                {
                    favourite_area = i;
                    favDirtyPages = dirtyPages;
                    favErases = dev->areaMgmt.getErasecount(i);
                    memcpy(summaryOut, curr, dataPagesPerArea);
                }
            }
            else
            {
                // Special Case for freeing committed AreaSummaries
                if (dev->sumCache.isCached(i) && dev->sumCache.wasASWritten(i)
                    && dirtyPages >= favDirtyPages)
                {
                    favourite_area = i;
                    favDirtyPages = dirtyPages;
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
GarbageCollection::moveValidDataToNewArea(AreaPos srcArea, AreaPos dstArea, SummaryEntry* summary)
{
    PAFFS_DBG_S(PAFFS_TRACE_GC_DETAIL,
                "Moving valid data from Area %" PRIu16 " (on %" PRIu16 ") to Area %" PRIu16 " (on %" PRIu16 ")",
                srcArea,
                dev->areaMgmt.getPos(srcArea),
                dstArea,
                dev->areaMgmt.getPos(dstArea));
    for (unsigned long page = 0; page < dataPagesPerArea; page++)
    {
        if (summary[page] == SummaryEntry::used)
        {
            uint64_t src = dev->areaMgmt.getPos(srcArea) * totalPagesPerArea + page;
            uint64_t dst = dev->areaMgmt.getPos(dstArea) * totalPagesPerArea + page;

            char buf[totalBytesPerPage];
            Result r = dev->driver.readPage(src, buf, totalBytesPerPage);
            // Any Biterror gets corrected here by being moved
            if (r != Result::ok && r != Result::biterrorCorrected)
            {
                PAFFS_DBG_S(PAFFS_TRACE_GC,
                            "Could not read page n° %lu!",
                            static_cast<long unsigned>(src));
                return r;
            }
            r = dev->driver.writePage(dst, buf, totalBytesPerPage);
            if (r != Result::ok)
            {
                PAFFS_DBG_S(PAFFS_TRACE_GC,
                            "Could not write page n° %lu!",
                            static_cast<long unsigned>(dst));
                return Result::badflash;
            }
        }
        else
        {
            summary[page] = SummaryEntry::free;
        }
    }
    return Result::ok;
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
    bool srcAreaContainsData = false;
    AreaPos deletionTarget = 0;
    Result r;
    AreaPos lastDeletionTarget = 0;

    if (traceMask & PAFFS_TRACE_VERIFY_AS)
    {
        unsigned char buf[totalBytesPerPage];
        for (unsigned i = 0; i < totalPagesPerArea; i++)
        {
            Addr addr = combineAddress(dev->areaMgmt.getActiveArea(AreaType::garbageBuffer), i);
            dev->driver.readPage(getPageNumber(addr, *dev), buf, totalBytesPerPage);
            for (unsigned j = 0; j < totalBytesPerPage; j++)
            {
                if (buf[j] != 0xFF)
                {
                    PAFFS_DBG(PAFFS_TRACE_BUG,
                              "Garbage buffer "
                              "on %" PRIu16 " is not empty!",
                              dev->areaMgmt.getPos(
                                      dev->areaMgmt.getActiveArea(AreaType::garbageBuffer)));
                    return Result::bug;
                }
            }
        }
    }
    while (1)
    {
        deletionTarget = findNextBestArea(targetType, summary, &srcAreaContainsData);
        if (deletionTarget == 0)
        {
            PAFFS_DBG_S(PAFFS_TRACE_GC,
                        "Could not find any GC'able pages for type %s!",
                        areaNames[targetType]);

            // TODO: Only use this Mode for the "higher needs", i.e. unmounting.
            // might as well be for INDEX also, as tree is cached and needs to be
            // committed even for read operations.

            if (targetType != AreaType::index)
            {
                PAFFS_DBG_S(PAFFS_TRACE_GC, "And we use reserved Areas for INDEX only.");
                return Result::nospace;
            }

            if (dev->areaMgmt.getUsedAreas() <= areasNo)
            {
                PAFFS_DBG_S(PAFFS_TRACE_GC, "and have no reserved Areas left.");
                return Result::nospace;
            }

            // This happens if we couldn't erase former srcArea which was not empty
            // The last resort is using our protected GC_BUFFER block...
            PAFFS_DBG_S(PAFFS_TRACE_GC,
                        "GC did not find next place for GC_BUFFER! "
                        "Using reserved Areas.");

            AreaPos nextPos;
            for (nextPos = 0; nextPos < areasNo; nextPos++)
            {
                if (dev->areaMgmt.getStatus(nextPos) == AreaStatus::empty)
                {
                    dev->areaMgmt.initAreaAs(nextPos, targetType);
                    PAFFS_DBG_S(PAFFS_TRACE_AREA, "Found empty Area %" PRIu16 "", nextPos);
                }
            }
            if (nextPos == areasNo)
            {
                PAFFS_DBG(PAFFS_TRACE_BUG,
                          "Used Areas said we had space left (%" PRIu16 " areas), "
                          "but no empty area was found!",
                          dev->areaMgmt.getUsedAreas());
                return Result::bug;
            }

            /* If lastArea contained data, it is already copied to gc_buffer. 'summary' is untouched
             * and valid.
             * It it did not contain data (or this is the first round), 'summary' contains
             * {SummaryEntry::free}.
             */
            if (lastDeletionTarget == 0)
            {
                // this is first round, without having something deleted.
                // Just init and return nextPos.
                dev->areaMgmt.setActiveArea(AreaType::index, nextPos);
                return Result::ok;
            }

            // Resurrect area, fill it with the former summary. In end routine, positions will be
            // swapped.
            dev->areaMgmt.initAreaAs(lastDeletionTarget, dev->areaMgmt.getType(deletionTarget));
            r = dev->sumCache.setSummaryStatus(lastDeletionTarget, summary);
            if (r != Result::ok)
            {
                PAFFS_DBG(PAFFS_TRACE_ERROR,
                          "Could not move former Summary to area %" PRIu16 "!",
                          lastDeletionTarget);
                return r;
            }
            deletionTarget = lastDeletionTarget;
            break;
        }

        if (traceMask & PAFFS_TRACE_VERIFY_AS)
        {
            // Just for debug, in production AS might be invalid and summary may be incomplete
            SummaryEntry tmp[dataPagesPerArea];
            r = dev->sumCache.getSummaryStatus(deletionTarget, tmp);
            if (r != Result::ok)
            {
                PAFFS_DBG(PAFFS_TRACE_VERIFY_AS,
                          "Could not verify AreaSummary of area %" PRIu16 "!",
                          deletionTarget);
            }
            if (memcmp(summary, tmp, dataPagesPerArea) != 0)
            {
                PAFFS_DBG(PAFFS_TRACE_BUG,
                          "Summary of findNextBestArea is different to actual areaSummary");
            }
        }

        if (traceMask & PAFFS_TRACE_VERIFY_AS)
        {
            for (unsigned int j = 0; j < dataPagesPerArea; j++)
            {
                if (summary[j] > SummaryEntry::dirty)
                    PAFFS_DBG(PAFFS_TRACE_BUG, "Summary of %" PRIu16 " contains invalid Entries!", j);
            }
        }

        // TODO: more Safety switches like comparison of lastDeletion targetType

        lastDeletionTarget = deletionTarget;

        if (srcAreaContainsData)
        {
            // still some valid data, copy to new area
            PAFFS_DBG_S(PAFFS_TRACE_GC_DETAIL,
                        "GC found just partially clean area %" PRIu16 " on pos %" PRIu16 "",
                        deletionTarget,
                        dev->areaMgmt.getPos(deletionTarget));

            r = moveValidDataToNewArea(
                    deletionTarget, dev->areaMgmt.getActiveArea(AreaType::garbageBuffer), summary);
            // while(getchar() == EOF);
            if (r != Result::ok)
            {
                PAFFS_DBG_S(PAFFS_TRACE_ERROR,
                            "Could not copy valid pages from area %" PRIu16 " to %" PRIu16 "!",
                            deletionTarget,
                            dev->areaMgmt.getActiveArea(AreaType::garbageBuffer));
                // TODO: Handle something, maybe put area in ReadOnly or copy somewhere else..
                // TODO: Maybe copy rest of Pages before quitting
                return r;
            }
            dev->areaMgmt.deleteAreaContents(deletionTarget);
            // Copy the updated (no SummaryEntry::dirty pages) summary to the deletion_target (it
            // will be the fresh area!)
            r = dev->sumCache.setSummaryStatus(deletionTarget, summary);
            if (r != Result::ok)
            {
                PAFFS_DBG_S(PAFFS_TRACE_ERROR,
                            "Could not remove dirty entries in AS of area %" PRId16 "",
                            deletionTarget);
                return r;
            }
            // Notify for used Pages
            if (targetType != AreaType::unset)
            {
                // Safe, because we can assume deletion targetType is same Type as we want (from
                // getNextBestArea)
                dev->areaMgmt.setStatus(deletionTarget, AreaStatus::active);
            }
        }
        else
        {
            dev->areaMgmt.deleteArea(deletionTarget);
        }

        // TODO: Maybe delete more available blocks. Mark them as UNSET+EMPTY
        break;
    }

    // swap logical position of areas to keep addresses valid
    dev->areaMgmt.swapAreaPosition(deletionTarget,
                                   dev->areaMgmt.getActiveArea(AreaType::garbageBuffer));

    if (targetType != AreaType::unset)
    {
        // This assumes that current activearea is closed...
        if (dev->areaMgmt.getActiveArea(targetType) != 0)
        {
            PAFFS_DBG(PAFFS_TRACE_BUG,
                      "old active Area (%" PRIu16 " on %" PRIu16 ") is not closed!",
                      dev->areaMgmt.getActiveArea(targetType),
                      dev->areaMgmt.getPos(dev->areaMgmt.getActiveArea(targetType)));
            return Result::bug;
        }
        // dev->areaMgmt.setActiveArea(targetType, deletion_target);
        dev->areaMgmt.initAreaAs(deletionTarget, targetType);
    }

    PAFFS_DBG_S(PAFFS_TRACE_GC_DETAIL,
                "Garbagecollection erased pos %" PRIu16 " and gave area %" PRIu16 " pos %" PRIu16 ".",
                dev->areaMgmt.getPos(dev->areaMgmt.getActiveArea(AreaType::garbageBuffer)),
                dev->areaMgmt.getActiveArea(targetType),
                dev->areaMgmt.getPos(dev->areaMgmt.getActiveArea(targetType)));

    return Result::ok;
}
}
