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

#include "summaryCache.hpp"
#include "area.hpp"
#include "device.hpp"
#include "driver/driver.hpp"
#include "superblock.hpp"

namespace paffs
{
AreaSummaryElem::AreaSummaryElem()
{
    mStatusBits = 0;
    clear();
};
AreaSummaryElem::~AreaSummaryElem()
{
    clear();
};
void
AreaSummaryElem::clear()
{
    if (isDirty())
    {
        PAFFS_DBG(PAFFS_TRACE_BUG, "Tried to clear a dirty cache elem!");
    }
    mEntries.clear();
    mStatusBits = 0;
    mDirtyPages = 0;
    mArea = 0;
}
SummaryEntry
AreaSummaryElem::getStatus(PageOffs page)
{
    if (!isUsed())
    {
        PAFFS_DBG(PAFFS_TRACE_BUG, "Tried to get Status of unused cache elem!");
        return SummaryEntry::error;
    }
    return getStatus(page, mEntries);
}
SummaryEntry
AreaSummaryElem::getStatus(PageOffs page, TwoBitList<dataPagesPerArea>& list)
{
    if (page >= dataPagesPerArea)
    {
        PAFFS_DBG(PAFFS_TRACE_BUG,
                  "Tried to read page %" PTYPE_PAGEOFFS ", but allowed < %" PTYPE_PAGEOFFS "!",
                  page,
                  dataPagesPerArea);
        return SummaryEntry::error;
    }
    return static_cast<SummaryEntry>(list.getValue(page));
}
void
AreaSummaryElem::setStatus(PageOffs page, SummaryEntry value)
{
    if (!isUsed())
    {
        PAFFS_DBG(PAFFS_TRACE_BUG, "Tried to set Status of unused cache elem!");
        return;
    }
    setStatus(page, value, mEntries);
    if (value == SummaryEntry::dirty)
    {
        mDirtyPages++;
    }
    setDirty();
    setLoadedFromSuperPage(false);
}
void
AreaSummaryElem::setStatus(PageOffs page, SummaryEntry value, TwoBitList<dataPagesPerArea>& list)
{

    if (page >= dataPagesPerArea)
    {
        PAFFS_DBG(PAFFS_TRACE_BUG,
                  "Tried to set page %" PTYPE_PAGEOFFS ", but allowed < %" PTYPE_PAGEOFFS "!",
                  page,
                  dataPagesPerArea);
        return;
    }
    //First mask bitfield byte leaving active bytes to zero, then insert value
    list.setValue(page, static_cast<uint8_t>(value));
}
bool
AreaSummaryElem::isDirty()
{
    return mStatusBits & 0b1;
}
void
AreaSummaryElem::setDirty(bool dirty)
{
    if (dirty)
    {
        mStatusBits |= 0b1;
    }
    else
    {
        mStatusBits &= ~0b1;
    }
}
bool
AreaSummaryElem::isAreaSummaryWritten()
{
    return mStatusBits & 0b10;
}
void
AreaSummaryElem::setAreaSummaryWritten(bool written)
{
    if (written)
    {
        mStatusBits |= 0b10;
    }
    else
    {
        mStatusBits &= ~0b10;
    }
}
/**
 * @brief used to determine, if AS
 * did not change since loaded from SuperPage
 */
bool
AreaSummaryElem::isLoadedFromSuperPage()
{
    return mStatusBits & 0b100;
}
void
AreaSummaryElem::setLoadedFromSuperPage(bool loaded)
{
    if (loaded)
    {
        mStatusBits |= 0b100;
    }
    else
    {
        mStatusBits &= ~0b100;
    }
}
bool
AreaSummaryElem::isUsed()
{
    return mStatusBits & 0b1000;
}
void
AreaSummaryElem::setUsed(bool used)
{
    if (used)
    {
        mStatusBits |= 0b1000;
    }
    else
    {
        mStatusBits &= ~0b1000;
    }
}
PageOffs
AreaSummaryElem::getDirtyPages()
{
    if (!isUsed())
    {
        PAFFS_DBG(PAFFS_TRACE_BUG, "Tried to get Dirty Pages of unused cache elem!");
    }
    return mDirtyPages;
}
void
AreaSummaryElem::setDirtyPages(PageOffs pages)
{
    if (!isUsed())
    {
        PAFFS_DBG(PAFFS_TRACE_BUG, "Tried to set Dirty Pages of unused cache elem!");
    }
    mDirtyPages = pages;
}

void
AreaSummaryElem::setArea(AreaPos areaPos)
{
    if (isUsed())
    {
        PAFFS_DBG(PAFFS_TRACE_BUG,
                  "Tried to set areaPos %" PTYPE_AREAPOS ", but "
                  "SummaryElem is set to area %" PTYPE_AREAPOS "!",
                  areaPos,
                  mArea);
        return;
    }
    mArea = areaPos;
    setUsed();
}

AreaPos
AreaSummaryElem::getArea()
{
    if (!isUsed())
    {
        PAFFS_DBG(PAFFS_TRACE_BUG, "Tried to get areaPos of unused SummaryElem!");
        return 0;
    }
    return mArea;
}

TwoBitList<dataPagesPerArea>*
AreaSummaryElem::exposeSummary()
{
    return &mEntries;
}

SummaryCache::SummaryCache(Device* mdev) : dev(mdev)
{
    if (areaSummaryCacheSize < 3)
    {
        PAFFS_DBG(PAFFS_TRACE_ERROR,
                  "AreaSummaryCacheSize is less than 3!\n"
                  "\tThis is not recommended, as Errors can happen.");
    }
    mTranslation.reserve(areaSummaryCacheSize);
}

SummaryCache::~SummaryCache()
{
    for (unsigned i = 0; i < areaSummaryCacheSize; i++)
    {
        if (mSummaryCache[i].isDirty())
        {
            PAFFS_DBG(PAFFS_TRACE_ERROR, "Clearing Summary cache with uncommitted elem!");
            mSummaryCache[i].setDirty(0);
        }
    }
}

Result
SummaryCache::commitAreaSummaryHard(int& clearedAreaCachePosition)
{
    PageOffs favDirtyPages = 0;
    AreaPos favouriteArea = 0;
    uint16_t cachePos = 0;
    for (std::pair<AreaPos, uint16_t> it : mTranslation)
    {
        // found a cached element
        cachePos = it.second;
        if (mSummaryCache[cachePos].isDirty() && mSummaryCache[cachePos].isAreaSummaryWritten()
            && dev->areaMgmt.getStatus(it.first) != AreaStatus::active
            && (dev->areaMgmt.getType(it.first) == AreaType::data
                || dev->areaMgmt.getType(it.first) == AreaType::index))
        {
            PageOffs dirtyPages = countDirtyPages(cachePos);
            PAFFS_DBG_S(PAFFS_TRACE_ASCACHE,
                        "Checking Area %" PTYPE_AREAPOS " "
                        "with %" PTYPE_PAGEOFFS " dirty pages",
                        it.first,
                        dirtyPages);
            if (dirtyPages >= favDirtyPages)
            {
                favouriteArea = it.first;
                clearedAreaCachePosition = it.second;
                favDirtyPages = dirtyPages;
            }
        }
        else
        {
            PAFFS_DBG_S(PAFFS_TRACE_ASCACHE,
                        "Ignored Area %" PTYPE_AREAPOS " "
                        "at cache pos %" PRIu16,
                        it.first,
                        it.second);
            if (!mSummaryCache[cachePos].isDirty())
            {
                PAFFS_DBG_S(PAFFS_TRACE_ASCACHE, "\tnot dirty");
            }
            if (!mSummaryCache[cachePos].isAreaSummaryWritten())
            {
                PAFFS_DBG_S(PAFFS_TRACE_ASCACHE, "\tnot AS written");
            }
            if (dev->areaMgmt.getStatus(it.first) == AreaStatus::active)
            {
                PAFFS_DBG_S(PAFFS_TRACE_ASCACHE,
                            "\tis active (%s)",
                            areaNames[dev->areaMgmt.getType(it.first)]);
            }
            if (dev->areaMgmt.getType(it.first) != AreaType::data
                && dev->areaMgmt.getType(it.first) != AreaType::index)
            {
                PAFFS_DBG_S(PAFFS_TRACE_ASCACHE, "\tnot data/index");
            }
        }
    }

    if (favouriteArea == 0)
    {
        PAFFS_DBG(PAFFS_TRACE_BUG, "Could not find any swappable candidats, why?");
        clearedAreaCachePosition = -1;
        return Result::bug;
    }
    if (mTranslation.find(favouriteArea) == mTranslation.end())
    {
        PAFFS_DBG(PAFFS_TRACE_BUG, "Could not find swapping area in cache?");
        clearedAreaCachePosition = -1;
        return Result::bug;
    }
    cachePos = mTranslation[favouriteArea];

    PAFFS_DBG_S(PAFFS_TRACE_ASCACHE,
                "Commit Hard swaps GC Area %" PTYPE_AREAPOS " (on %" PTYPE_AREAPOS ")"
                " from %" PTYPE_AREAPOS " (on %" PTYPE_AREAPOS ")",
                dev->areaMgmt.getActiveArea(AreaType::garbageBuffer),
                dev->areaMgmt.getPos(dev->areaMgmt.getActiveArea(AreaType::garbageBuffer)),
                favouriteArea,
                dev->areaMgmt.getPos(favouriteArea));

    SummaryEntry summary[dataPagesPerArea];
    unpackStatusArray(cachePos, summary);

    Result r = dev->areaMgmt.gc.moveValidDataToNewArea(
            favouriteArea, dev->areaMgmt.getActiveArea(AreaType::garbageBuffer), summary);

    if (r != Result::ok)
    {
        PAFFS_DBG(PAFFS_TRACE_ERROR, "Could not move Data for AS commit!");
        clearedAreaCachePosition = -1;
        return r;
    }
    dev->areaMgmt.deleteArea(favouriteArea);
    // swap logical position of areas to keep addresses valid
    dev->areaMgmt.swapAreaPosition(favouriteArea,
                                   dev->areaMgmt.getActiveArea(AreaType::garbageBuffer));
    packStatusArray(cachePos, summary);
    // AsWritten gets reset in delete Area, and dont set dirty bc now the AS is not committed, soley
    // in RAM

    return Result::ok;
}

void
SummaryCache::unpackStatusArray(uint16_t position, SummaryEntry* arr)
{
    for (uint16_t i = 0; i < dataPagesPerArea; i++)
    {
        arr[i] = mSummaryCache[position].getStatus(i);
    }
}

void
SummaryCache::packStatusArray(uint16_t position, SummaryEntry* arr)
{
    for (uint16_t i = 0; i < dataPagesPerArea; i++)
    {
        mSummaryCache[position].setStatus(i, arr[i]);
    }
}

int
SummaryCache::findNextFreeCacheEntry()
{
    // from summaryCache to AreaPosition
    for (uint16_t i = 0; i < areaSummaryCacheSize; i++)
    {
        if (!mSummaryCache[i].isUsed())
            return i;
    }
    return -1;
}

Result
SummaryCache::setPageStatus(Addr addr, SummaryEntry state)
{
    return setPageStatus(extractLogicalArea(addr), extractPageOffs(addr), state);
}

Result
SummaryCache::setPageStatus(AreaPos area, PageOffs page, SummaryEntry state)
{
    if (dev->readOnly)
    {
        PAFFS_DBG(PAFFS_TRACE_BUG, "Tried setting PageStatus in readOnly mode!");
        return Result::bug;
    }
    if (page > dataPagesPerArea)
    {
        PAFFS_DBG(PAFFS_TRACE_BUG,
                  "Tried to access page out of bounds! (was: %" PTYPE_PAGEOFFS ", should: < %" PTYPE_PAGEOFFS ")",
                  page,
                  dataPagesPerArea);
        return Result::invalidInput;
    }
    if (state == SummaryEntry::free)
    {
        PAFFS_DBG(PAFFS_TRACE_BUG,
                  "Area %" PTYPE_AREAPOS " was set to empty, "
                  "but apperarently not by deleting it!",
                  area);
    }
    if (mTranslation.find(area) == mTranslation.end())
    {
        Result r = loadUnbufferedArea(area, true);
        if (r != Result::ok)
        {
            PAFFS_DBG_S(PAFFS_TRACE_ERROR, "Could not load AS of area %" PRId16 "!", area);
            return r;
        }
    }
    if (dev->areaMgmt.getType(area) == AreaType::unset)
    {
        PAFFS_DBG(PAFFS_TRACE_BUG,
                  "Tried setting Pagestatus on UNSET area "
                  "%" PTYPE_AREAPOS " (on %" PTYPE_AREAPOS ", status %" PRIu8 ")",
                  area,
                  dev->areaMgmt.getPos(area),
                  dev->areaMgmt.getStatus(area));
        return Result::bug;
    }

    if (mSummaryCache[mTranslation[area]].getStatus(page) == state)
    {
        PAFFS_DBG_S(PAFFS_TRACE_ASCACHE, "Skipping set status b.c. status is the same");
        return Result::ok;
    }

    dev->journal.addEvent(journalEntry::summaryCache::SetStatus(area, page, state));

    mSummaryCache[mTranslation[area]].setStatus(page, state);
    if (state == SummaryEntry::dirty)
    {
        if (traceMask & PAFFS_WRITE_VERIFY_AS)
        {
            uint8_t* writebuf = dev->driver.getPageBuffer();
            memset(writebuf, 0xFF, dataBytesPerPage);
            memset(&writebuf[dataBytesPerPage], 0x0A, oobBytesPerPage);
            Addr addr = combineAddress(area, page);
            dev->driver.writePage(getPageNumber(addr, *dev), writebuf, totalBytesPerPage);
        }

        if (traceMask & PAFFS_TRACE_VERIFY_AS)
        {
            PageOffs dirtyPagesCheck = countDirtyPages(mTranslation[area]);
            if (dirtyPagesCheck != mSummaryCache[mTranslation[area]].getDirtyPages())
            {
                PAFFS_DBG(PAFFS_TRACE_BUG,
                          "DirtyPages differ from actual count! "
                          "(Area %" PTYPE_AREAPOS " on %" PTYPE_AREAPOS " was: %" PTYPE_PAGEOFFS ","
                                  " thought %" PTYPE_PAGEOFFS ")",
                          area,
                          dev->areaMgmt.getPos(area),
                          dirtyPagesCheck,
                          mSummaryCache[mTranslation[area]].getDirtyPages());
                return Result::fail;
            }
        }

        // Commit to Flash, nothing will change the data pages in flash
        if (mSummaryCache[mTranslation[area]].getDirtyPages() == dataPagesPerArea
            && !mSummaryCache[mTranslation[area]].isAreaSummaryWritten())
        {
            Result r = writeAreasummary(mSummaryCache[mTranslation[area]]);
            if (r != Result::ok)
            {
                PAFFS_DBG(PAFFS_TRACE_ERROR, "Could not write AreaSummary");
                return r;
            }
        }
    }
    return Result::ok;
}

SummaryEntry
SummaryCache::getPageStatus(Addr addr, Result* result)
{
    return getPageStatus(extractLogicalArea(addr), extractPageOffs(addr), result);
}

SummaryEntry
SummaryCache::getPageStatus(AreaPos area, PageOffs page, Result* result)
{
    if (page > dataPagesPerArea)
    {
        PAFFS_DBG(PAFFS_TRACE_BUG,
                  "Tried to access page out of bounds! (was: %" PRId16 ", should: < %" PRId16 "",
                  page,
                  dataPagesPerArea);
        *result = Result::invalidInput;
        return SummaryEntry::error;
    }
    if (mTranslation.find(area) == mTranslation.end())
    {
        Result r = loadUnbufferedArea(area, false);
        if (r == Result::nospace)
        {
            // load one-shot AS in read only
            TwoBitList<dataPagesPerArea> buf;
            r = readAreasummary(area, buf, true);
            if (r == Result::ok || r == Result::biterrorCorrected)
            {
                PAFFS_DBG_S(PAFFS_TRACE_ASCACHE,
                            "Loaded existing AreaSummary of %" PRId16 " without caching",
                            area);
                *result = Result::ok;
                // TODO: Handle biterror.
                return AreaSummaryElem::getStatus(page, buf);
            }
            else if (r == Result::notFound)
            {
                PAFFS_DBG_S(
                        PAFFS_TRACE_ASCACHE, "Loaded free AreaSummary of %" PRId16 " without caching", area);
                *result = Result::ok;
                return SummaryEntry::free;
            }
        }
        if (r != Result::ok)
        {
            *result = r;
            return SummaryEntry::error;
        }
    }
    if (page > dataPagesPerArea)
    {
        PAFFS_DBG(PAFFS_TRACE_BUG,
                  "Tried to access page out of bounds! (was: %" PRId16 ", should: < %" PRId16 "",
                  page,
                  dataPagesPerArea);
        *result = Result::invalidInput;
        return SummaryEntry::error;
    }

    *result = Result::ok;
    return mSummaryCache[mTranslation[area]].getStatus(page);
}

Result
SummaryCache::getSummaryStatus(AreaPos area, SummaryEntry* summary, bool complete)
{
    if (mTranslation.find(area) == mTranslation.end())
    {
        TwoBitList<dataPagesPerArea> list;
        // This one does not have to be copied into Cache
        // Because it is just for a one-shot of Garbage collection looking for the best area
        Result r = readAreasummary(area, list, complete);
        if (r == Result::ok || r == Result::biterrorCorrected)
        {
            // TODO: Handle biterror
            PAFFS_DBG_S(PAFFS_TRACE_ASCACHE, "Loaded existing AreaSummary of "
                    "Area %" PTYPE_AREAPOS " on %" PTYPE_AREAPOS, area, dev->areaMgmt.getPos(area));
        }
        else if (r == Result::notFound)
        {
            PAFFS_DBG_S(PAFFS_TRACE_ASCACHE, "Loaded empty AreaSummary of "
                        "Area %" PTYPE_AREAPOS " on %" PTYPE_AREAPOS, area, dev->areaMgmt.getPos(area));
            r = Result::ok;
        }
        for(uint16_t i = 0; i < dataPagesPerArea; i++)
        {
            summary[i] = AreaSummaryElem::getStatus(i, list);
        }
        return r;
    }else
    {
        unpackStatusArray(mTranslation[area], summary);
        return Result::ok;
    }
}

Result
SummaryCache::getEstimatedSummaryStatus(AreaPos area, SummaryEntry* summary)
{
    return getSummaryStatus(area, summary, false);
}

Result
SummaryCache::setSummaryStatus(AreaPos area, SummaryEntry* summary)
{
    // Dont set Dirty, because GC just deleted AS and dirty Pages
    // This area ist most likely to be used soon
    if (mTranslation.find(area) == mTranslation.end())
    {
        Result r = loadUnbufferedArea(area, true);
        if (r != Result::ok)
            return r;
    }
    packStatusArray(mTranslation[area], summary);
    mSummaryCache[mTranslation[area]].setDirtyPages(countDirtyPages(mTranslation[area]));
    return Result::ok;
}

Result
SummaryCache::deleteSummary(AreaPos area)
{
    if (mTranslation.find(area) == mTranslation.end())
    {
        // This is not a bug, because an uncached area can be deleted
        PAFFS_DBG_S(PAFFS_TRACE_ASCACHE, "Tried to delete nonexisting Area %" PRId16 "", area);
        return Result::ok;
    }

    dev->journal.addEvent(journalEntry::summaryCache::Remove(area));

    mSummaryCache[mTranslation[area]].setDirty(false);
    mSummaryCache[mTranslation[area]].clear();
    mTranslation.erase(area);
    PAFFS_DBG_S(PAFFS_TRACE_ASCACHE, "Deleted cache entry of area %" PRId16 "", area);
    return Result::ok;
}

// For Garbage collection to consider cached AS-Areas before others
bool
SummaryCache::isCached(AreaPos area)
{
    return mTranslation.find(area) != mTranslation.end();
}

// For Garbage collection to consider committed AS-Areas before others
bool
SummaryCache::wasASWritten(AreaPos area)
{
    if (mTranslation.find(area) == mTranslation.end())
    {
        if (dev->areaMgmt.getStatus(area) == AreaStatus::empty)
            return false;
        // If it is not empty, and not in Cache, it has to be containing Data and is not active.
        // It has to have an AS written.
        return true;
    }
    return mSummaryCache[mTranslation[area]].isAreaSummaryWritten();
}

// For Garbage collection that has deleted the AS too
void
SummaryCache::resetASWritten(AreaPos area)
{
    if (mTranslation.find(area) == mTranslation.end())
    {
        PAFFS_DBG(PAFFS_TRACE_ASCACHE,
                  "Tried to reset AS-Record of non-cached Area %" PRId16 ". This is probably not a bug.",
                  area);
        return;
    }
    mSummaryCache[mTranslation[area]].setAreaSummaryWritten(false);
}

Result
SummaryCache::loadAreaSummaries()
{
    // Assumes unused Summary Cache
    for (AreaPos i = 0; i < areaSummaryCacheSize; i++)
    {
        mSummaryCache[i].clear();
    }

    PAFFS_DBG_S(PAFFS_TRACE_VERBOSE, "cleared summary Cache");
    SuperIndex index;
    memset(&index, 0, sizeof(SuperIndex));
    index.summaries[0] = mSummaryCache[0].exposeSummary();
    index.summaries[1] = mSummaryCache[1].exposeSummary();

    PAFFS_DBG_S(PAFFS_TRACE_VERBOSE, "Inited SuperIndex");

    Result r = dev->superblock.readSuperIndex(&index);
    if (r != Result::ok)
    {
        PAFFS_DBG_S(PAFFS_TRACE_ERROR, "failed to load Area Summaries!");
        return r;
    }
    dev->areaMgmt.setUsedAreas(index.usedAreas);
    PAFFS_DBG_S(PAFFS_TRACE_VERBOSE, "read superIndex successfully");

    for (uint8_t i = 0; i < 2; i++)
    {
        if (index.areaSummaryPositions[i] > 0)
        {
            mTranslation[index.areaSummaryPositions[i]] = i;
            mSummaryCache[i].setArea(index.areaSummaryPositions[i]);
            mSummaryCache[i].setDirtyPages(countDirtyPages(i));
            mSummaryCache[i].setLoadedFromSuperPage();

            PAFFS_DBG_S(PAFFS_TRACE_ASCACHE,
                        "Checking for an AS at area %" PTYPE_AREAPOS " (phys. %" PTYPE_AREAPOS ", "
                        "abs. page %" PTYPE_PAGEABS ")",
                        index.areaSummaryPositions[i],
                        dev->areaMgmt.getPos(index.areaSummaryPositions[i]),
                        getPageNumber(combineAddress(index.areaSummaryPositions[i], dataPagesPerArea),
                                      *dev));
            uint8_t* summary = dev->driver.getPageBuffer();
            r = dev->driver.readPage(
                    getPageNumber(combineAddress(index.areaSummaryPositions[i], dataPagesPerArea), *dev),
                    summary,
                    dataBytesPerPage);
            if (r != Result::ok && r != Result::biterrorCorrected)
            {
                PAFFS_DBG(PAFFS_TRACE_ERROR,
                          "Could not check if AS was already written on "
                          "area %" PTYPE_AREAPOS,
                          index.areaSummaryPositions[i]);
                return r;
            }
            for (uint16_t t = 0; t < dataBytesPerPage; t++)
            {
                if (summary[t] != 0xFF)
                {
                    mSummaryCache[i].setAreaSummaryWritten();
                    break;
                }
            }
            if (dev->areaMgmt.getStatus(index.areaSummaryPositions[i]) == AreaStatus::active)
            {
                dev->areaMgmt.setActiveArea(dev->areaMgmt.getType(index.areaSummaryPositions[i]),
                                            index.areaSummaryPositions[i]);
            }
            PAFFS_DBG_S(PAFFS_TRACE_VERBOSE,
                        "Loaded area summary %" PRId16 " on %" PRId16 "",
                        index.areaSummaryPositions[i],
                        dev->areaMgmt.getPos(index.areaSummaryPositions[i]));
        }
    }

    return Result::ok;
}

Result
SummaryCache::commitAreaSummaries(bool createNew)
{
    // commit all Areas except two

    Result r;
    /** TODO: Maybe commit closed Areas?
     *  - No, Most of the time a page is much bigger than a summary
     */
    while (mTranslation.size() > 2)
    {
        r = freeNextBestSummaryCacheEntry(true);
        if (r != Result::ok)
        {
            PAFFS_DBG(PAFFS_TRACE_ERROR,
                      "Could not free a cached AreaSummary.\n"
                      "\tThis is ignored, because we have to unmount.");
            break;
        }
    }

    uint8_t pos = 0;

    SuperIndex index;
    memset(&index, 0, sizeof(SuperIndex));
    // Rest of members are inited in superblock.cpp

    bool someDirty = false;

    // write the open/uncommitted AS'es to Superindex
    for (std::pair<AreaPos, uint16_t> cacheElem : mTranslation)
    {
        // Clean Areas are not committed unless they are active
        if (pos >= 2)
        {
            PAFFS_DBG(PAFFS_TRACE_ERROR,
                      "Too many uncommitted AreaSummaries.\n"
                      "\tskipping lossy, because we have to unmount.");
            break;
        }
        if (!mSummaryCache[cacheElem.second].isDirty()
            && mSummaryCache[cacheElem.second].isAreaSummaryWritten())
            continue;

        someDirty |= mSummaryCache[cacheElem.second].isDirty()
                     && !mSummaryCache[cacheElem.second].isLoadedFromSuperPage();

        index.areaSummaryPositions[pos] = cacheElem.first;
        index.summaries[pos++] = mSummaryCache[cacheElem.second].exposeSummary();
    }

    r = dev->superblock.commitSuperIndex(&index, someDirty, createNew);
    if (r != Result::ok)
    {
        PAFFS_DBG(PAFFS_TRACE_ERROR, "Could not commit superindex");
        return r;
    }

    dev->journal.addEvent(journalEntry::Success(JournalEntry::Topic::summaryCache));

    for (std::pair<AreaPos, uint16_t> cacheElem : mTranslation)
    {
        mSummaryCache[cacheElem.second].setDirty(false);
        mSummaryCache[cacheElem.second].clear();
    }
    mTranslation.clear();

    return Result::ok;
}

JournalEntry::Topic
SummaryCache::getTopic()
{
    return JournalEntry::Topic::summaryCache;
}

void
SummaryCache::processEntry(JournalEntry& entry)
{
    if (entry.topic != getTopic())
    {
        PAFFS_DBG(PAFFS_TRACE_BUG, "Got wrong entry to process!");
        return;
    }
    auto e = static_cast<const journalEntry::SummaryCache*>(&entry);
    switch (e->subtype)
    {
    case journalEntry::SummaryCache::Subtype::commit:
    case journalEntry::SummaryCache::Subtype::remove:
        PAFFS_DBG_S(PAFFS_TRACE_ASCACHE,
                    "Deleting cache "
                    "entry of area %" PRId16 "",
                    mSummaryCache[e->area].getArea());
        mSummaryCache[e->area].setDirty(false);
        mTranslation.erase(mSummaryCache[e->area].getArea());
        mSummaryCache[e->area].clear();
        break;
    case journalEntry::SummaryCache::Subtype::setStatus:
    {
        // TODO activate some failsafe that checks for invalid writes during this setPages
        auto s = static_cast<const journalEntry::summaryCache::SetStatus*>(&entry);
        setPageStatus(s->area, s->page, s->status);
        break;
    }
    }
}

void
SummaryCache::processUncheckpointedEntry(JournalEntry& entry)
{
    if (entry.topic != getTopic())
    {
        PAFFS_DBG(PAFFS_TRACE_BUG, "Got wrong entry to process!");
        return;
    }
    const journalEntry::SummaryCache* e = static_cast<const journalEntry::SummaryCache*>(&entry);
    switch (e->subtype)
    {
    case journalEntry::SummaryCache::Subtype::commit:
    case journalEntry::SummaryCache::Subtype::remove:
        // TODO: Is it Ok if nothing happens here?
        // B.c. we are overwriting 'used' pages
        break;
    case journalEntry::SummaryCache::Subtype::setStatus:
    {
        // TODO activate some failsafe that checks for invalid writes during this setPages
        auto s = static_cast<const journalEntry::summaryCache::SetStatus*>(&entry);
        if (s->status == SummaryEntry::used)
        {
            setPageStatus(s->area, s->page, SummaryEntry::dirty);
        }
        if (s->status == SummaryEntry::dirty)
        {
            setPageStatus(s->area, s->page, SummaryEntry::used);
        }
        break;
    }
    }
}

Result
SummaryCache::loadUnbufferedArea(AreaPos area, bool urgent)
{
    Result r = Result::ok;
    int nextEntry = findNextFreeCacheEntry();
    if (nextEntry < 0)
    {
        r = freeNextBestSummaryCacheEntry(urgent);
        if (!urgent && r == Result::notFound)
        {
            PAFFS_DBG_S(PAFFS_TRACE_ASCACHE,
                        "Nonurgent Cacheclean did not return free space, activating read-only");
            return Result::nospace;
        }
        if (r != Result::ok)
        {
            PAFFS_DBG_S(PAFFS_TRACE_ERROR,
                        "Urgent Cacheclean did not return free space, expect errors");
            return r;
        }
        nextEntry = findNextFreeCacheEntry();
        if (nextEntry < 0)
        {
            PAFFS_DBG(PAFFS_TRACE_BUG, "freeNextBestSummaryCacheEntry did not free space!");
            return Result::bug;
        }
    }
    mTranslation[area] = nextEntry;
    mSummaryCache[nextEntry].setArea(area);

    r = readAreasummary(area, *mSummaryCache[mTranslation[area]].exposeSummary(), true);
    if (r == Result::ok || r == Result::biterrorCorrected)
    {
        mSummaryCache[nextEntry].setAreaSummaryWritten();
        mSummaryCache[nextEntry].setDirty(
                r == Result::biterrorCorrected);  // Rewrites corrected Bit upon next commit
        PAFFS_DBG_S(PAFFS_TRACE_ASCACHE, "Loaded existing AreaSummary of %" PRId16 " to cache", area);
        mSummaryCache[mTranslation[area]].setDirtyPages(countDirtyPages(mTranslation[area]));
    }
    else if (r == Result::notFound)
    {
        PAFFS_DBG_S(PAFFS_TRACE_ASCACHE, "Loaded new AreaSummary for %" PRId16 "", area);
    }
    else
    {
        return r;
    }

    PAFFS_DBG_S(PAFFS_TRACE_ASCACHE, "Created cache entry for area %" PRId16 "", area);
    return Result::ok;
}

Result
SummaryCache::freeNextBestSummaryCacheEntry(bool urgent)
{
    int fav = -1;

    // Look for unchanged cache entries, the easiest way
    for (int i = 0; i < areaSummaryCacheSize; i++)
    {
        if (mSummaryCache[i].isUsed())
        {
            if (!(mSummaryCache[i].isDirty() || mSummaryCache[i].isLoadedFromSuperPage())
                || dev->areaMgmt.getStatus(mSummaryCache[i].getArea()) == AreaStatus::empty)
            {
                if (mSummaryCache[i].isDirty()
                    && dev->areaMgmt.getStatus(mSummaryCache[i].getArea()) == AreaStatus::empty)
                {
                    // Dirty, but it was not properly deleted?
                    PAFFS_DBG(PAFFS_TRACE_BUG,
                              "Area %" PTYPE_AREAPOS " is dirty, but was "
                              "not set to an status (Type %s)",
                              mSummaryCache[i].getArea(),
                              areaNames[dev->areaMgmt.getType(mSummaryCache[i].getArea())]);
                }
                PAFFS_DBG_S(PAFFS_TRACE_ASCACHE,
                            "Deleted non-dirty cache entry "
                            "of area %" PTYPE_AREAPOS,
                            mSummaryCache[i].getArea());
                mTranslation.erase(mSummaryCache[i].getArea());
                mSummaryCache[i].clear();
                fav = i;
            }
        }
        else
        {
            PAFFS_DBG_S(PAFFS_TRACE_ASCACHE, "freeNextBestCache ignored empty pos %" PRId16 "", i);
        }
    }
    if (fav > -1)
    {
        return Result::ok;
    }

    // Look for the least probable Area to be used that has no committed AS
    PageOffs maxDirtyPages = 0;
    for (uint16_t i = 0; i < areaSummaryCacheSize; i++)
    {
        if (mSummaryCache[i].isUsed() && !mSummaryCache[i].isAreaSummaryWritten()
            && dev->areaMgmt.getStatus(mSummaryCache[i].getArea()) != AreaStatus::active)
        {
            PageOffs tmp = countUnusedPages(i);
            if (tmp >= maxDirtyPages)
            {
                fav = i;
                maxDirtyPages = tmp;
            }
        }
    }
    if (fav > -1)
    {
        return commitAndEraseElem(fav);
    }

    if (!urgent)
    {
        return Result::notFound;
    }

    PAFFS_DBG_S(PAFFS_TRACE_ASCACHE,
                "freeNextBestCache found no uncommitted Area, activating Garbage collection");
    Result r = dev->areaMgmt.gc.collectGarbage(AreaType::unset);

    if (r == Result::ok)
    {
        if (mTranslation.size() < areaSummaryCacheSize)
        {
            // GC freed something
            return Result::ok;
        }
    }
    // GC may have relocated an Area, deleting the committed AS
    // Look for the least probable Area to be used that has no committed AS
    maxDirtyPages = 0;
    for (int i = 0; i < areaSummaryCacheSize; i++)
    {
        if (mSummaryCache[i].isUsed() && !mSummaryCache[i].isAreaSummaryWritten()
            && dev->areaMgmt.getStatus(mSummaryCache[i].getArea()) != AreaStatus::active)
        {
            PageOffs tmp = countUnusedPages(i);
            if (tmp >= maxDirtyPages)
            {
                fav = i;
                maxDirtyPages = tmp;
            }
        }
    }
    if (fav > -1)
    {
        return commitAndEraseElem(fav);
    }

    PAFFS_DBG_S(PAFFS_TRACE_ASCACHE, "Garbage collection could not relocate any Areas");
    // Ok, just swap Area-positions, clearing AS
    r = commitAreaSummaryHard(fav);
    if (r != Result::ok)
    {
        PAFFS_DBG(PAFFS_TRACE_ERROR, "Could not free any AS cache elem!");
        return r;
    }

    if (traceMask & PAFFS_TRACE_VERIFY_AS)
    {
        // check for bugs in usage of Garbage collection
        uint8_t activeAreas = 0;
        for (AreaPos i = 0; i < areasNo; i++)
        {
            if (dev->areaMgmt.getStatus(i) == AreaStatus::active
                && (dev->areaMgmt.getType(i) == AreaType::data
                    || dev->areaMgmt.getType(i) == AreaType::index))
            {
                activeAreas++;
            }
        }
        if (activeAreas > 2)
        {
            PAFFS_DBG(PAFFS_TRACE_BUG, "More than two active Areas after gc! (%" PRIu8 ")", activeAreas);
            return Result::bug;
        }
    }

    if (fav < 0)
    {
        PAFFS_DBG_S(PAFFS_TRACE_ASCACHE, "Garbage collection could not free any Areas");
        // Ok, just swap Area-positions, clearing AS
        r = commitAreaSummaryHard(fav);
        if (r != Result::ok)
        {
            PAFFS_DBG(PAFFS_TRACE_ERROR, "Could not free any AS cache elem!");
            return r;
        }
    }

    if (fav > -1)
    {
        return commitAndEraseElem(fav);
    }

    PAFFS_DBG(PAFFS_TRACE_ERROR, "No area was found to clear!");
    return Result::notFound;
}

PageOffs
SummaryCache::countDirtyPages(uint16_t position)
{
    uint32_t dirty = 0;
    for (uint32_t i = 0; i < dataPagesPerArea; i++)
    {
        if (mSummaryCache[position].getStatus(i) == SummaryEntry::dirty)
        {
            dirty++;
        }
    }
    return dirty;
}

PageOffs
SummaryCache::countUsedPages(uint16_t position)
{
    uint32_t used = 0;
    for (uint32_t i = 0; i < dataPagesPerArea; i++)
    {
        if (mSummaryCache[position].getStatus(i) == SummaryEntry::used)
        {
            used++;
        }
    }
    return used;
}

PageOffs
SummaryCache::countUnusedPages(uint16_t position)
{
    return dataPagesPerArea - countUsedPages(position);
}

Result
SummaryCache::commitAndEraseElem(uint16_t position)
{
    // Commit AS to Area OOB
    Result r = writeAreasummary(mSummaryCache[position]);
    if (r != Result::ok)
    {
        PAFFS_DBG(PAFFS_TRACE_ERROR, "Could not write AreaSummary");
        return r;
    }
    PAFFS_DBG_S(PAFFS_TRACE_ASCACHE,
                "Committed and deleted cache "
                "entry of area %" PRId16 "",
                mSummaryCache[position].getArea());
    mTranslation.erase(mSummaryCache[position].getArea());
    mSummaryCache[position].clear();
    return Result::ok;
}

Result
SummaryCache::writeAreasummary(AreaSummaryElem& elem)
{
    if (elem.isAreaSummaryWritten())
    {
        PAFFS_DBG(PAFFS_TRACE_BUG, "Tried to commit elem with existing AS Commit!");
        return Result::bug;
    }
    uint8_t* summary;
    if(areaSummaryIsPacked)
    {
        BitList<dataPagesPerArea> packedSummary;
        if(packedSummary.byteUsage != static_cast<size_t>(areaSummarySizePacked - 1))
        {
            PAFFS_DBG(PAFFS_TRACE_BUG, "Calculated Bytes for areaSummary differ!"
                    "(BitList: %zu, autocalc: %" PRIu16 ")",
                    packedSummary.byteUsage, areaSummarySizePacked - 1);
            return Result::bug;
        }
        // TODO: Check if areaOOB is clean, and maybe Verify written data
        PAFFS_DBG_S(
                PAFFS_TRACE_ASCACHE, "Committing AreaSummary to Area %" PRId16 "", elem.getArea());

        for (uint16_t j = 0; j < dataPagesPerArea; j++)
        {
            if (elem.getStatus(j) != SummaryEntry::dirty)
            {
                packedSummary.setBit(j);
            }
        }
        summary = packedSummary.expose();
    }else
    {
        summary = elem.exposeSummary()->expose();
    }

    PageAbs page =
            getPageNumber(combineAddress(elem.getArea(), dataPagesPerArea), *dev);
    Result r;
    uint16_t btw;
    if(areaSummaryIsPacked)
    {
        btw = areaSummarySizePacked;
    }else
    {
        btw = areaSummarySizeUnpacked;
    }

    if (traceMask & PAFFS_TRACE_VERIFY_AS)
    {
        uint8_t* readbuf = dev->driver.getPageBuffer();
        r = dev->driver.readPage(page, readbuf, totalBytesPerPage);
        for(uint16_t i = 0; i < totalBytesPerPage; i++)
        {
            if (static_cast<uint8_t>(readbuf[i]) != 0xFF)
            {
                PAFFS_DBG(PAFFS_TRACE_BUG,
                          "Area %" PTYPE_AREAPOS,
                          elem.getArea());
                return Result::bug;
            }
        }
    }
    uint8_t* writebuf = dev->driver.getPageBuffer();
    writebuf[0] = 0;
    memcpy(&writebuf[sizeof(uint8_t)], summary, btw - 1);
    r = dev->driver.writePage(page, writebuf, btw);
    if (r != Result::ok)
    {
        return r;
    }
    dev->journal.addEvent(journalEntry::summaryCache::Commit(elem.getArea()));
    elem.setAreaSummaryWritten();
    elem.setDirty(false);
    return Result::ok;
}

Result
SummaryCache::readAreasummary(AreaPos area, TwoBitList<dataPagesPerArea>& elem, bool complete)
{
    bool bitErrorWasCorrected = false;
    PageAbs basePage = getPageNumber(combineAddress(area, dataPagesPerArea), *dev);
    Result r;
    uint16_t btr;
    if(areaSummaryIsPacked)
    {
        btr = areaSummarySizePacked;
    }else
    {
        btr = areaSummarySizeUnpacked;
    }
    uint8_t* readbuf = dev->driver.getPageBuffer();
    r = dev->driver.readPage(basePage, readbuf, btr);
    if (r != Result::ok)
    {
        if (r == Result::biterrorCorrected)
        {
            bitErrorWasCorrected = true;
            PAFFS_DBG(PAFFS_TRACE_INFO,
                      "Corrected biterror, triggering dirty areaSummary for rewrite.");
        }
        else
        {
            return r;
        }
    }

    if (readbuf[0] != 0)
    {
        // Magic marker not here, so no AS present
        PAFFS_DBG_S(PAFFS_TRACE_ASCACHE, "And just found an unset AS.");
        return Result::notFound;
    }

    if(areaSummaryIsPacked)
    {
        uint8_t summary[areaSummarySizePacked - 1];
        memcpy(summary, &readbuf[1], areaSummarySizePacked - 1);
        for (uint16_t i = 0; i < dataPagesPerArea; i++)
        {
            //Extract every Bit of this bitlist
            if (BitList<areaSummarySizePacked>::getBit(i, summary))
            {
                if (complete)
                {
                    Addr tmp = combineAddress(area, i);
                    r = dev->driver.readPage(getPageNumber(tmp, *dev), readbuf, totalBytesPerPage);
                    if (r != Result::ok)
                    {
                        if (r == Result::biterrorCorrected)
                        {
                            bitErrorWasCorrected = true;
                            PAFFS_DBG(PAFFS_TRACE_INFO,
                                      "Corrected biterror, triggering dirty areaSummary for "
                                      "rewrite by Garbage collection.\n\t(Hopefully it runs before an "
                                      "additional bitflip happens)");
                        }
                        else
                        {
                            return r;
                        }
                    }
                    bool containsData = false;
                    for (uint16_t byte = 0; byte < totalBytesPerPage; byte++)
                    {
                        if (readbuf[byte] != 0xFF)
                        {
                            containsData = true;
                            break;
                        }
                    }
                    if (containsData)
                    {
                        AreaSummaryElem::setStatus(i, SummaryEntry::used, elem);
                    }
                    else
                    {
                        AreaSummaryElem::setStatus(i, SummaryEntry::free, elem);
                    }
                }
                else
                {
                    // This is just a guess b/c we are in incomplete mode.
                    AreaSummaryElem::setStatus(i, SummaryEntry::used, elem);
                }
            }
            else
            {
                AreaSummaryElem::setStatus(i, SummaryEntry::dirty, elem);
            }
        }
    }else
    {
        memcpy(elem.expose(), &readbuf[1], areaSummarySizeUnpacked-1);
    }

    if (bitErrorWasCorrected)
    {
        return Result::biterrorCorrected;
    }
    return Result::ok;
}

}  // namespace paffs
