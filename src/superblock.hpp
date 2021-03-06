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

#pragma once

#include "commonTypes.hpp"
#include "journalTopic.hpp"
#include "bitlist.hpp"
#include <inttypes.h>
#include <stdint.h>

namespace paffs
{
/**
 * This only has to hold as many numbers as there are pages in superblock area
 * Values Zero and 0xFF... are reserved.
 * Zero to indicate overflow, 0xFF.. to indicate empty page
 */
typedef uint32_t SerialNo;
static constexpr SerialNo emptySerial = 0xFFFFFFFF;

/**
 * @file nearly all AreaPos values point to _physical_ areanumbers, not logical!
 */

struct AnchorEntry
{
    SerialNo no;
    AreaPos logPrev;      // This is unused in anchor entry
    AreaPos jumpPadArea;  // direct. this needs to be on third place
    Param param;
    uint8_t fsVersion;
};

struct JumpPadEntry
{
    SerialNo no;
    AreaPos logPrev;  // if != 0, the logical area prev is now free, while this current is not
                      // (obviously)
    AreaPos nextArea;  // direct.
};

struct SuperIndex
{
    SerialNo no;
    AreaPos logPrev;  // if != 0, the logical area prev is now free, while this current is not
                      // (obviously)
    //"public"
    Addr rootNode;
    AreaPos usedAreas;
    Area* areaMap;
    AreaPos* activeArea;
    uint64_t overallDeletions;
    //"internal"
    AreaPos areaSummaryPositions[2];
    //! may contain non-active areaSummaries.
    TwoBitList<dataPagesPerArea>* summaries[2];
    static constexpr uint16_t
    getNeededBytes(const uint16_t numberOfAreaSummaries)
    {
        // Serial Number Skipped because it is inserted later on
        return numberOfAreaSummaries <= 2 ?
               sizeof(AreaPos) +                 // LogPrev
               sizeof(Addr) +                    // rootNode
               sizeof(AreaPos) +                 // usedAreas
               sizeof(Area) * areasNo +          // AreaMap
               sizeof(AreaPos) * AreaType::no +  // ActiveAreas
               sizeof(uint64_t) +                // overallDeletions
               sizeof(AreaPos) * 2
               +  // Area Summary Positions
               TwoBitList<dataPagesPerArea>::byteUsage * numberOfAreaSummaries
               /* two entries for INDEX and DATA section*/
        : 0;
    }


    uint16_t
    getNeededBytes()
    {
        uint16_t neededSummaries = 0;
        for (uint16_t i = 0; i < 2; i++)
        {
            if (areaSummaryPositions[i] > 0)
                neededSummaries++;
        }
        return getNeededBytes(neededSummaries);
    }

    Result
    deserializeFromBuffer(const uint8_t* buf);

    Result
    serializeToBuffer(uint8_t* buf);

    bool
    isPlausible();

    void
    print();
};

class Superblock : public JournalTopic
{
    Device* device;

    Addr mRootnodeAddr = 0;
    bool mRootnodeDirty = 0;
    Area mMap[areasNo];
    AreaPos mActiveAreas[AreaType::no];
    AreaPos mUsedAreas;
    uint64_t mOverallDeletions;

    Addr pathToSuperIndexDirect[superChainElems];  // Direct Addresses
    SerialNo superChainIndexes[superChainElems];
    bool testmode = false;

    //This buffer is only used for reading/writing the superindex.
    //TODO: Use another pagebuf that is unused during superindex commit
    uint8_t mBuf[SuperIndex::getNeededBytes(2)];

public:
    Superblock(Device* mdev) : device(mdev)
    {
        clear();
    };

    JournalEntry::Topic
    getTopic() override;
    void
    resetState() override;
    /**
     * AreaMap contains the phys. Positions of the areas.
     * An area move instruction changes the position of older elements as well.
     * We need to have the latest Version of the Areamap before
     * other topics start processing the log.
     */
    void
    preScan(const journalEntry::Max& entry, JournalEntryPosition position) override;
    Result
    processEntry(const journalEntry::Max& entry, JournalEntryPosition position) override;

    void
    clear();

    Result
    registerRootnode(Addr addr);
    Addr
    getRootnodeAddr();
    AreaType
    getType(AreaPos area);
    AreaStatus
    getStatus(AreaPos area);
    uint32_t
    getErasecount(AreaPos area);
    AreaPos
    getPos(AreaPos area);

    void
    setType(AreaPos area, AreaType type);
    void
    setStatus(AreaPos area, AreaStatus status);
    void
    increaseErasecount(AreaPos area);
    void
    setPos(AreaPos area, AreaPos pos);

    AreaPos
    getActiveArea(AreaType type);
    void
    setActiveArea(AreaType type, AreaPos pos);

    AreaPos
    getUsedAreas();
    void
    setUsedAreas(AreaPos num);
    void
    increaseUsedAreas();
    void
    decreaseUsedAreas();

    void
    swapAreaPosition(AreaPos a, AreaPos b);

    void
    setOverallDeletions(uint64_t& deletions);
    uint64_t
    getOverallDeletions();

    // returns PAFFS_NF if no superindex is in flash
    Result
    readSuperIndex(SuperIndex* index);
    Result
    commitSuperIndex(SuperIndex* newIndex, bool asDirty, bool createNew = false);
    void
    setTestmode(bool t);
private:
    /**
     * Worst case O(n) with area count
     */
    Result
    resolveDirectToLogicalPath(Addr directPath[superChainElems], Addr outPath[superChainElems]);
    /**
     * Is constant with flash size because
     * at formatting time all superblocks are located at start.
     */
    Result
    fillPathWithFirstSuperblockAreas(Addr directPath[superChainElems]);

    /**
     * This assumes that blocks are immediately deleted after starting
     * a new block inside area.
     * \return NF if last block is full, even if other blocks are free
     */
    Result
    findFirstFreeEntryInArea(AreaPos area, PageOffs* outPos, PageOffs requiredPages);

    Result
    findFirstFreeEntryInBlock(AreaPos area,
                              uint8_t block,
                              PageOffs* outPos,
                              PageOffs requiredPages);

    /**
     * First addr in path is anchor
     * @param path returns the *direct* addresses to each found Entry up to SuperEntry
     */
    Result
    getPathToMostRecentSuperIndex(Addr path[superChainElems],
                                  SerialNo indexes[superChainElems],
                                  AreaPos logPrev[superChainElems]);

    /**
     * @param area : *physical* Area in which to look
     * @param out_pos : offset in pages starting from area front where Entry was found
     * @param out_index : The index of the elem found
     * @param next : The area of the next chain elem as read from current
     *
     * Assumes, that if a block contains a valid Entry, no other block contains
     * another entry. This assumption is correct,
     * because the block has to be deleted, if the cursor jumps to next Block.
     */
    Result
    readMostRecentEntryInArea(
            AreaPos area, Addr* outPos, SerialNo* outIndex, AreaPos* next, AreaPos* logPrev);
    /**
     * @param area : *physical* Area in which to look
     * @param block: Which block to check
     * @param out_pos : offset in pages starting from area front where Entry was found
     * @param out_index : The index of the elem found
     */
    Result
    readMostRecentEntryInBlock(AreaPos area,
                               uint8_t block,
                               PageOffs* outPos,
                               SerialNo* outIndex,
                               AreaPos* next,
                               AreaPos* logPrev);

    /**
     * This assumes that the area of the Anchor entry does not change.
     * Entry->serialNo needs to be set to appropriate increased number.
     * Changes the serial to zero if a new block is used.
     * @param prev is a *logical* addr to the last valid entry
     * @param area may be changed if target was written to a new area
     */
    Result
    insertNewAnchorEntry(Addr prev, AreaPos* area, AnchorEntry* entry);
    Result
    readAnchorEntry(Addr addr, AnchorEntry* entry);

    /**
     * May call garbage collection for a new SB area.
     * Changes the serial to zero if a new block is used.
     * @param prev is a *logical* addr to the last valid entry
     * @param area may be changed if target was written to a new area
     */
    Result
    insertNewJumpPadEntry(Addr prev, AreaPos* area, JumpPadEntry* entry);
    // Result readJumpPadEntry(Addr addr, JumpPadEntry* entry);

    /**
     * May call garbage collection for a new SB area.
     * Changes the serial to zero if a new block is used.
     * @param prev is a *logical* addr to the last valid entry
     * @param area may be changed if target was written to a new area
     */
    Result
    insertNewSuperIndex(Addr prev, AreaPos* area, SuperIndex* entry);
    Result
    writeSuperPageIndex(PageAbs pageStart, SuperIndex* entry);
    Result
    readSuperPageIndex(Addr addr, SuperIndex* entry, bool withAreaMap);

    Result
    handleBlockOverflow(PageAbs newPage, Addr logPrev, SerialNo* serial);
    Result
    deleteSuperBlock(AreaPos area, uint8_t block);

    /**
     * This does not trigger GC, because this would change Area Map
     * @param logPrev: old log. area
     * @return newArea: new log. area
     */
    AreaPos
    findBestNextFreeArea(AreaPos logPrev);
};
}
