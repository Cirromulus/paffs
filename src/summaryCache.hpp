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
#include "journalTopic.hpp"
#include "bitlist.hpp"
#include <unordered_map>

namespace paffs
{
class AreaSummaryElem
{
    TwoBitList<dataPagesPerArea> mEntries;
    uint8_t mStatusBits;  // dirty < asWritten < loadedFromSuperIndex < used
    AreaPos mArea;
    PageOffs mDirtyPages;
    void
    setUsed(bool used = true);
public:
    AreaSummaryElem();
    ~AreaSummaryElem();
    void
    clear();
    SummaryEntry
    getStatus(PageOffs page);
    static SummaryEntry
    getStatus(PageOffs page, const TwoBitList<dataPagesPerArea>& list);
    void
    setStatus(PageOffs page, SummaryEntry value);
    static void
    setStatus(PageOffs page, SummaryEntry value, TwoBitList<dataPagesPerArea>& list);
    bool
    isDirty();
    void
    setDirty(bool dirty = true);
    bool
    isAreaSummaryWritten();
    void
    setAreaSummaryWritten(bool written = true);
    /**
     * \brief used to determine if AS did not change since loaded from SuperPage
     */
    bool
    isLoadedFromSuperPage();
    void
    setLoadedFromSuperPage(bool loaded = true);
    bool
    isUsed();
    PageOffs
    getDirtyPages();
    void
    setDirtyPages(PageOffs pages);
    void
    setArea(AreaPos areaPos);
    AreaPos
    getArea();
    TwoBitList<dataPagesPerArea>*
    exposeSummary();
};

class SummaryCache : public JournalTopic
{
    // excess byte is for dirty- and wasASWritten marker
    AreaSummaryElem mSummaryCache[areaSummaryCacheSize];

    std::unordered_map<AreaPos, uint16_t> mTranslation;  // from area number to array offset
    Device* dev;

    bool journalReplayMode = false;
    JournalEntryPosition firstUncommittedElem[areasNo];
public:
    SummaryCache(Device* mdev);
    ~SummaryCache();

    /**
     * Functionally same as setPageStatus(area, page, state)
     */
    Result
    setPageStatus(Addr addr, SummaryEntry state);

    Result
    setPageStatus(AreaPos area, PageOffs page, SummaryEntry state);

    /*
     * Functionally same as getPageStatus(area, page, Result)
     */
    SummaryEntry
    getPageStatus(Addr addr, Result& result);

    SummaryEntry
    getPageStatus(AreaPos area, PageOffs page, Result& result);

    Result
    setSummaryStatus(AreaPos area, SummaryEntry* summary);

    /**
     * This one does not copy into Cache
     * Because it is just for a one-shot of Garbage collection looking for the best area
     */
    Result
    getSummaryStatus(AreaPos area, SummaryEntry* summary);

    /*
     * Reads every Page for data instead of scanning just OOB
     */
    Result
    scanAreaForSummaryStatus(AreaPos area, SummaryEntry* summary);

    /*
     * \warn Only for retired or unused Areas
     */
    Result
    deleteSummary(AreaPos area);

    /**
     * Used by Garbage collection to consider cached AS-Areas before others
     */
    bool
    shouldClearArea(AreaPos area);

    /**
     * Used by Garbage collection to consider dirty AS-Areas before others
     */
    bool wasAreaSummaryWritten(AreaPos area);

    /**
     * Used by Garbage collection that can delete the AS too. Does nothing if area is not cached.
     */
    void
    resetASWritten(AreaPos area);

    /**
     * Loads all unclosed AreaSummaries in RAM upon Mount
     * Complete wipe of all previous Information
     * @warning High Stack usage scaling with dataPagesPerArea
     */
    Result
    loadAreaSummaries();

    /**
     * @param createNew if set, a new path will be set instead of
     * looking for an old one
     */
    Result
    commitAreaSummaries(bool createNew = false);
    void
    clear();
    JournalEntry::Topic
    getTopic() override;
    void
    resetState() override;
    void
    preScan(const journalEntry::Max& entry, JournalEntryPosition position) override;
    Result
    processEntry(const journalEntry::Max& entry, JournalEntryPosition position) override;
    void
    signalEndOfLog() override;
    void
    printStatus();


private:
    void
    enableSummaryElem(uint16_t pos, AreaPos area);
    void
    removeSummaryElem(AreaPos area);
    uint16_t
    getSummaryElemPos(AreaPos area);
    bool
    existsSummaryElem(AreaPos area);


    /**
     * @Brief uses garbageCollection-buffer to swap a whole Area,
     * committing its new AS.
     * @warn Decreases wear leveling if used regularly.
     * @param desperate is used if even non-dirty elems have to be written if they are not active.
     */
    Result
    commitAreaSummaryHard(int& clearedAreaCachePosition, bool desperate = false);

    SummaryEntry
    getPackedStatus(uint16_t position, PageOffs page);

    void
    setPackedStatus(uint16_t position, PageOffs page, SummaryEntry value);

    void
    unpackStatusArray(uint16_t position, SummaryEntry* arr);

    void
    packStatusArray(uint16_t position, SummaryEntry* arr);

    int
    findNextFreeCacheEntry();

    /**
     * \param urgent if not urgent, it stops before calling garbagecollection
     */
    Result
    loadUnbufferedArea(AreaPos area, bool urgent);

    Result
    freeNextBestSummaryCacheEntry(bool urgent);

    PageOffs
    countDirtyPages(uint16_t position);

    PageOffs
    countUsedPages(uint16_t position);

    PageOffs
    countUnusedPages(uint16_t position);

    Result
    commitAndEraseElem(uint16_t position);
    Result
    readAreasummary(AreaPos area, TwoBitList<dataPagesPerArea>& elem);

    Result
    writeAreasummary(AreaSummaryElem& elem);
};

}  // namespace paffs
