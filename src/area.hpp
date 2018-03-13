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
#include "garbage_collection.hpp"
#include "journalTopic.hpp"

namespace paffs
{
extern const char* areaNames[];
extern const char* areaStatusNames[];

// Helper functions
/**
 * Translates indirect Addr to physical page number in respect to the Area mapping
 */
PageAbs
getPageNumber(const Addr addr, Device& dev);
/**
 * Translates direct Addr to physical page number ignoring AreaMap
 */
PageAbs
getPageNumberFromDirect(const Addr addr);
/**
 * Returns the absolute page number from *logical* address
 */
BlockAbs
getBlockNumber(const Addr addr, Device& dev);
/**
 * Translates direct Addr to physical Block number ignoring AreaMap
 */
BlockAbs
getBlockNumberFromDirect(const Addr addr);
/**
 * combines two values to one type
 */
Addr
combineAddress(const AreaPos logicalArea, const PageOffs page);
unsigned int
extractLogicalArea(const Addr addr);
unsigned int
extractPageOffs(const Addr addr);

class AreaManagement : public JournalTopic
{

    Device* dev;

    bool mUnfinishedTransaction = false;
    journalEntry::areaMgmt::Max mLastOp;

    enum class ExternOp
    {
        none,
        setType,
        setStatus,
        setActiveArea,
        changeUsedAreas,
        increaseErasecount,
        resetASWritten,
        deleteSummary,
    } mLastExternOp;

public:
    GarbageCollection gc;
    AreaManagement(Device* mdev) : dev(mdev), gc(mdev)
    {
        resetState();
    };

    /**
     * May call garbage collection
     * May return an empty or active area
     * Returns same area if there is still writable space left
     *\warn modifies active area if area was inited
     */
    unsigned int
    findWritableArea(AreaType areaType);

    Result
    findFirstFreePage(PageOffs& page, AreaPos area);

    Result
    manageActiveAreaFull(AreaType areaType);

    void
    initAreaAs(AreaPos area, AreaType type);
    Result
    closeArea(AreaPos area);
    void
    retireArea(AreaPos area);
    /**
     * \param noJournalLogging is active if called from deleteArea.
     */
    Result
    deleteAreaContents(AreaPos area, bool noJournalLogging = false);
    Result
    deleteArea(AreaPos area);

    JournalEntry::Topic
    getTopic() override;
    void
    resetState() override;
    bool
    isInterestedIn(const journalEntry::Max& entry) override;
    Result
    processEntry(const journalEntry::Max& entry, JournalEntryPosition) override;
    void
    signalEndOfLog() override;
};

}  // namespace paffs
