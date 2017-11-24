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

#include "superblock.hpp"
#include "area.hpp"
#include "dataIO.hpp"
#include "device.hpp"
#include "driver/driver.hpp"
#include "bitlist.hpp"
#include <stdlib.h>

namespace paffs
{
Result
SuperIndex::deserializeFromBuffer(Device* dev, const char* buf)
{
    uint16_t pointer = 0;
    uint8_t pagebuf[dataBytesPerPage];
    memcpy(&logPrev, &buf[pointer], sizeof(AreaPos));
    pointer += sizeof(AreaPos);
    memcpy(&rootNode, &buf[pointer], sizeof(Addr));
    pointer += sizeof(Addr);
    memcpy(&usedAreas, &buf[pointer], sizeof(AreaPos));
    pointer += sizeof(AreaPos);
    memcpy(areaMap, &buf[pointer], areasNo * sizeof(Area));
    pointer += areasNo * sizeof(Area);
    memcpy(activeAreas, &buf[pointer], AreaType::no * sizeof(AreaPos));
    pointer += AreaType::no * sizeof(AreaPos);
    memcpy(&overallDeletions, &buf[pointer], sizeof(uint64_t));
    pointer += sizeof(uint64_t);

    memcpy(areaSummaryPositions, &buf[pointer], 2 * sizeof(AreaPos));
    pointer += 2 * sizeof(AreaPos);

    unsigned char asCount = 0;
    for (unsigned int i = 0; i < 2; i++)
    {
        if (areaSummaryPositions[i] <= 0)
        {
            continue;
        }
        if (areaSummaryPositions[i] > areasNo)
        {
            PAFFS_DBG(PAFFS_TRACE_ERROR,
                      "Entry asPosition[%u] is unplausible! "
                      "(was %" PRIu32 ", should > %" PRIu32,
                      i,
                      areaSummaryPositions[i],
                      areasNo);
            return Result::fail;
        }
        asCount++;
        // Unpack AreaSummary
        for (unsigned int j = 0; j < dataPagesPerArea; j++)
        {
            if (BitList<dataPagesPerArea>::getBit(j, &buf[pointer]))
            {
                // TODO: Normally, we would check in the OOB for a Checksum or so, which is present
                // all the time
                Addr tmp = combineAddress(areaMap[areaSummaryPositions[i]].position, j);
                Result r = dev->driver.readPage(
                        getPageNumberFromDirect(tmp), pagebuf, dataBytesPerPage);
                if (r != Result::ok)
                {
                    if (r == Result::biterrorCorrected)
                    {
                        // TODO trigger SB rewrite. AS may be invalid at this point.
                        PAFFS_DBG(PAFFS_TRACE_ALWAYS, "Corrected biterror, but we do not yet write "
                                                      "corrected version back to flash.");
                        return Result::ok;
                    }
                    return r;
                }
                bool contains_data = false;
                for (unsigned int byte = 0; byte < dataBytesPerPage; byte++)
                {
                    if (pagebuf[byte] != 0xFF)
                    {
                        contains_data = true;
                        break;
                    }
                }
                if (contains_data)
                {
                    areaSummary[i][j] = SummaryEntry::used;
                }
                else
                {
                    areaSummary[i][j] = SummaryEntry::free;
                }
            }
            else
            {
                areaSummary[i][j] = SummaryEntry::dirty;
            }
        }
        pointer += BitList<dataPagesPerArea>::getByteUsage();
    }

    if (pointer != getNeededBytes(asCount))
    {
        PAFFS_DBG(PAFFS_TRACE_BUG,
                  "Read bytes (%u) differs from calculated (%u)!",
                  pointer,
                  getNeededBytes(asCount));
        return Result::bug;
    }
    return Result::ok;
}

Result
SuperIndex::serializeToBuffer(char* buf)
{
    if (areaMap == nullptr)
    {
        PAFFS_DBG(PAFFS_TRACE_BUG, "AreaMap not set!");
        return Result::bug;
    }
    if (activeAreas == nullptr)
    {
        PAFFS_DBG(PAFFS_TRACE_BUG, "ActiveArea not set!");
        return Result::bug;
    }
    unsigned int pointer = 0;
    memcpy(&buf[pointer], &logPrev, sizeof(AreaPos));
    pointer += sizeof(AreaPos);
    memcpy(&buf[pointer], &rootNode, sizeof(Addr));
    pointer += sizeof(Addr);
    memcpy(&buf[pointer], &usedAreas, sizeof(AreaPos));
    pointer += sizeof(AreaPos);
    memcpy(&buf[pointer], areaMap, areasNo * sizeof(Area));
    pointer += areasNo * sizeof(Area);
    memcpy(&buf[pointer], activeAreas, AreaType::no * sizeof(AreaPos));
    pointer += AreaType::no * sizeof(AreaPos);
    memcpy(&buf[pointer], &overallDeletions, sizeof(uint64_t));
    pointer += sizeof(uint64_t);

    memcpy(&buf[pointer], areaSummaryPositions, 2 * sizeof(AreaPos));
    pointer += 2 * sizeof(AreaPos);

    // Collect area summaries and pack them
    for (unsigned int i = 0; i < 2; i++)
    {
        if (areaSummaryPositions[i] <= 0)
            continue;
        for (unsigned int j = 0; j < dataPagesPerArea; j++)
        {
            if(areaSummary[i][j] != SummaryEntry::dirty)
            {
                BitList<dataPagesPerArea>::setBit(j, &buf[pointer]);
            }
        }
        pointer += BitList<dataPagesPerArea>::getByteUsage();
    }

    PAFFS_DBG_S(PAFFS_TRACE_SUPERBLOCK, "%u bytes have been written to Buffer", pointer);
    if (pointer != getNeededBytes())
    {
        PAFFS_DBG(PAFFS_TRACE_BUG,
                  "Written bytes (%u) differ from calculated (%u)!",
                  pointer,
                  getNeededBytes());
        return Result::bug;
    }
    return Result::ok;
}

void
SuperIndex::print()
{
    printf("No:\t\t%" PRIu32 "\n", no);
    printf("Rootnode addr.: \t%u:%u\n", extractLogicalArea(rootNode), extractPageOffs(rootNode));
    printf("Used Areas: %" PRIu32 "\n", usedAreas);
    printf("areaMap:\n");
    for (AreaPos i = 0; i < areasNo && i < 128; i++)
    {
        printf("\t%02" PRIu32 "->%02" PRIu32, i, areaMap[i].position);
        printf(" %10s", areaNames[areaMap[i].type]);
        bool found = false;
        for (unsigned int asOffs = 0; asOffs < 2; asOffs++)
        {
            if (areaSummaryPositions[asOffs] != 0 && i == areaSummaryPositions[asOffs])
            {
                found = true;
                PageOffs free = 0, used = 0, dirty = 0;
                for (unsigned int j = 0; j < dataPagesPerArea; j++)
                {
                    if (areaSummary[asOffs][j] == SummaryEntry::free)
                    {
                        free++;
                    }
                    if (areaSummary[asOffs][j] == SummaryEntry::used)
                    {
                        used++;
                    }
                    if (areaSummary[asOffs][j] == SummaryEntry::dirty)
                    {
                        dirty++;
                    }
                }
                printf("\tFree/Used/Dirty Pages: %u/%u/%u", free, used, dirty);
                asOffs++;
            }
        }
        if (!found)
        {
            // printf("\tSummary not present.\n");
        }
        // printf("\t----------------\n");
        printf("\n");
    }
    printf("activeAreas:\n");
    for (uint16_t i = 0; i < AreaType::no; i++)
    {
        printf("%10s: %" PRIu32 "\n", areaNames[i], activeAreas[i]);
    }
    printf("Overall deletions: %" PRIu64 "\n", overallDeletions);
}

JournalEntry::Topic
Superblock::getTopic()
{
    return JournalEntry::Topic::superblock;
}

void
Superblock::processEntry(JournalEntry& entry)
{
    if (entry.topic != getTopic())
    {
        PAFFS_DBG(PAFFS_TRACE_BUG, "Got wrong entry to process!");
        return;
    }
    auto e = static_cast<const journalEntry::Superblock*>(&entry);
    switch (e->type)
    {
    case journalEntry::Superblock::Type::rootnode:
    {
        auto r = static_cast<const journalEntry::superblock::Rootnode*>(&entry);
        registerRootnode(r->rootnode);
        break;
    }
    case journalEntry::Superblock::Type::areaMap:
    {
        auto a = static_cast<const journalEntry::superblock::AreaMap*>(&entry);
        switch (a->operation)
        {
        case journalEntry::superblock::AreaMap::Operation::type:
            device->areaMgmt.setType(
                    a->offs,
                    static_cast<const journalEntry::superblock::areaMap::Type*>(&entry)->type);
            break;
        case journalEntry::superblock::AreaMap::Operation::status:
            device->areaMgmt.setStatus(
                    a->offs,
                    static_cast<const journalEntry::superblock::areaMap::Status*>(&entry)->status);
            break;
        case journalEntry::superblock::AreaMap::Operation::increaseErasecount:
            device->areaMgmt.increaseErasecount(a->offs);
            break;
        case journalEntry::superblock::AreaMap::Operation::position:
            device->areaMgmt.setPos(
                    a->offs,
                    static_cast<const journalEntry::superblock::areaMap::Position*>(&entry)
                            ->position);
            break;
        case journalEntry::superblock::AreaMap::Operation::swap:
            device->areaMgmt.swapAreaPosition(
                    a->offs,
                    static_cast<const journalEntry::superblock::areaMap::Swap*>(&entry)->b);
            break;
        }
        break;
    }
    case journalEntry::Superblock::Type::activeArea:
        device->areaMgmt.setActiveArea(
                static_cast<const journalEntry::superblock::ActiveArea*>(&entry)->type,
                static_cast<const journalEntry::superblock::ActiveArea*>(&entry)->area);
        break;
    case journalEntry::Superblock::Type::usedAreas:
        device->areaMgmt.setUsedAreas(
                static_cast<const journalEntry::superblock::UsedAreas*>(&entry)->usedAreas);
        break;
    }
}

void
Superblock::processUncheckpointedEntry(JournalEntry& entry)
{
    if (entry.topic != getTopic())
    {
        PAFFS_DBG(PAFFS_TRACE_BUG, "Got wrong entry to process!");
        return;
    }
    // TODO: If swap occurs, check if it was successful.
    if (static_cast<journalEntry::Superblock*>(&entry)->type
        == journalEntry::Superblock::Type::areaMap)
    {
        processEntry(entry);
    }
}

Result
Superblock::registerRootnode(Addr addr)
{
    if (addr == 0)
        PAFFS_DBG(PAFFS_TRACE_BUG, "BUG: Tried to set Rootnode to 0");
    rootnode_addr = addr;
    rootnode_dirty = true;
    device->journal.addEvent(journalEntry::superblock::Rootnode(addr));
    return Result::ok;
}

Addr
Superblock::getRootnodeAddr()
{
    if (rootnode_addr == 0)
    {
        PAFFS_DBG(PAFFS_TRACE_ERROR, "rootnode_address is 0! Maybe device not mounted?");
        return 0;
    }

    return rootnode_addr;
}

Result
Superblock::readSuperIndex(SuperIndex* index)
{
    AreaPos logPrev[superChainElems];
    PAFFS_DBG_S(PAFFS_TRACE_SUPERBLOCK, "Reading SuperIndex.");

    index->areaMap = device->areaMgmt.getMap();
    index->activeAreas = device->areaMgmt.getActiveAreas();

    Result r = getPathToMostRecentSuperIndex(pathToSuperIndexDirect, superChainIndexes, logPrev);
    if (r != Result::ok)
    {
        PAFFS_DBG(PAFFS_TRACE_ERROR, "could not get addr of most recent Superindex");
        return r;
    }

    // Index of last chain elem (SuperEntry) must not be empty (not found)
    if (superChainIndexes[jumpPadNo + 1] == emptySerial)
    {
        PAFFS_DBG(PAFFS_TRACE_ERROR,
                  "Determined Address of last SuperIndex, but its SerialNo was empty!");
        PAFFS_DBG_S(PAFFS_TRACE_ERROR,
                    "Area %" PRIu32 ", page %" PRIu32,
                    extractLogicalArea(pathToSuperIndexDirect[jumpPadNo + 1]),
                    extractPageOffs(pathToSuperIndexDirect[jumpPadNo + 1]));
        return Result::bug;
    }

    AnchorEntry e;
    r = readAnchorEntry(pathToSuperIndexDirect[0], &e);
    if (r != Result::ok)
    {
        PAFFS_DBG(PAFFS_TRACE_ERROR,
                  "Could not read Anchor Entry at %" PRIu32 ":%" PRIu32,
                  extractLogicalArea(pathToSuperIndexDirect[0]),
                  extractPageOffs(pathToSuperIndexDirect[0]));
        return r;
    }
    if (memcmp(&stdParam, &e.param, sizeof(Param)) != 0)
    {
        PAFFS_DBG(PAFFS_TRACE_ERROR, "Device parameter differ with own settings!");
        return Result::fail;
    }
    else
    {
        PAFFS_DBG_S(PAFFS_TRACE_VERBOSE, "Formatting infos are matching with our own");
    }
    if (e.fsVersion != version)
    {
        PAFFS_DBG(PAFFS_TRACE_ERROR, "FS Version differs with our own!");
        return Result::fail;
    }

    Addr addr = pathToSuperIndexDirect[superChainElems - 1];

    PAFFS_DBG_S(PAFFS_TRACE_SUPERBLOCK,
                "Found Super Index at %u:%u\n",
                extractLogicalArea(addr),
                extractPageOffs(addr));

    r = readSuperPageIndex(addr, index, true);
    if (r != Result::ok)
    {
        PAFFS_DBG(PAFFS_TRACE_ERROR, "Could not read Super Index!");
        return r;
    }
    PAFFS_DBG_S(PAFFS_TRACE_VERBOSE, "Read of SuperPage successful");

    if (traceMask & PAFFS_TRACE_SUPERBLOCK)
    {
        printf("Read Super Index:\n");
        index->print();
    }

    if (index->areaMap[0].position != 0)
    {
        PAFFS_DBG_S(PAFFS_TRACE_ERROR,
                    "Position of area 0 may never be different than 0 "
                    "(was %" PRIu32 ")",
                    index->areaMap[0].position);
        return Result::fail;
    }

    if (index->areaMap[0].type != AreaType::superblock)
    {
        PAFFS_DBG_S(PAFFS_TRACE_ERROR,
                    "Type of area 0 may never be different than superblock "
                    "(was %d)",
                    static_cast<int>(index->areaMap[0].type));
        return Result::fail;
    }

    if (index->areaSummaryPositions[0] != 0 && index->areaMap[index->areaSummaryPositions[0]].type != AreaType::data
        && index->areaMap[index->areaSummaryPositions[0]].type != AreaType::index)
    {
        PAFFS_DBG_S(PAFFS_TRACE_ERROR,
                    "An superblock-cached Area may never be Type != data or index "
                    "(Area %d is %s)",
                    index->areaSummaryPositions[0],
                    areaNames[index->areaMap[index->areaSummaryPositions[0]].type]);
        return Result::fail;
    }

    if (index->areaSummaryPositions[1] != 0 && index->areaMap[index->areaSummaryPositions[1]].type != AreaType::data
        && index->areaMap[index->areaSummaryPositions[1]].type != AreaType::index)
    {
        PAFFS_DBG_S(PAFFS_TRACE_ERROR,
                    "An superblock-cached Area may never be Type != data or index "
                    "(Area %d is %s)",
                    index->areaSummaryPositions[1],
                    areaNames[index->areaMap[index->areaSummaryPositions[1]].type]);
        return Result::fail;
    }

    for (unsigned int i = 0; i < areasNo; i++)
    {
        if (index->areaMap[i].position > areasNo)
        {
            PAFFS_DBG_S(PAFFS_TRACE_ERROR,
                        "Position of area %u unplausible! (%" PRIu32 ")",
                        i,
                        index->areaMap[i].position);
            return Result::fail;
        }
        if (index->areaMap[i].type >= AreaType::no)
        {
            PAFFS_DBG_S(PAFFS_TRACE_ERROR,
                        "Type of area %u unplausible! (%u)",
                        i,
                        static_cast<unsigned int>(index->areaMap[i].type));
            return Result::fail;
        }
        if (index->areaMap[i].status > AreaStatus::empty)
        {
            PAFFS_DBG_S(PAFFS_TRACE_ERROR,
                        "Status of area %u unplausible! (%u)",
                        i,
                        static_cast<unsigned int>(index->areaMap[i].status));
            return Result::fail;
        }
    }

    if (index->areaMap[extractLogicalArea(index->rootNode)].type != AreaType::index)
    {
        PAFFS_DBG_S(PAFFS_TRACE_ERROR,
                    "Rootnode address does not point to index area"
                    " (Area %u, page %u)",
                    extractLogicalArea(index->rootNode),
                    extractPageOffs(index->rootNode));
        return Result::fail;
    }

    // Anchor entry is ignored, should never change
    if (logPrev[0] != 0)
    {
        PAFFS_DBG_S(PAFFS_TRACE_ERROR,
                    "Anchor Area stated it would have been "
                    "moved to log. %" PRIu32 ", which is not allowed.",
                    logPrev[0]);
        return Result::fail;
    }
    // Reverse order, because the changes were committed from
    // SuperIndex (last) to Anchor (first entry)
    for (int i = superChainElems - 1; i >= 0; i--)
    {
        if (logPrev[i] != 0)
        {
            PAFFS_DBG_S(PAFFS_TRACE_SUPERBLOCK,
                        "Chain Area %d (phys. %" PRIu32 ") changed its location to log. %" PRIu32,
                        i,
                        extractLogicalArea(pathToSuperIndexDirect[i]),
                        logPrev[i]);
            AreaPos directNew = extractLogicalArea(pathToSuperIndexDirect[i]);
            AreaPos logNew = 0;
            // This is O(n) with AreasNo
            for (AreaPos a = 0; a < areasNo; a++)
            {
                if (directNew == index->areaMap[a].position)
                {
                    logNew = a;
                    break;
                }
            }
            index->areaMap[logPrev[i]].status = AreaStatus::empty;
            // Type will be set to unset when deletion happens
            index->areaMap[logNew].status = AreaStatus::active;
            index->areaMap[logNew].type = AreaType::superblock;
        }
    }

    rootnode_addr = index->rootNode;
    rootnode_dirty = false;

    device->areaMgmt.setUsedAreas(index->usedAreas);
    device->areaMgmt.setOverallDeletions(index->overallDeletions);

    return Result::ok;
}

Result
Superblock::commitSuperIndex(SuperIndex* newIndex, bool asDirty, bool createNew)
{
    Result r;
    if (createNew)
    {
        PAFFS_DBG_S(PAFFS_TRACE_SUPERBLOCK, "Creating new superchain");
        memset(superChainIndexes, 0, superChainElems * sizeof(SerialNo));
        r = fillPathWithFirstSuperblockAreas(pathToSuperIndexDirect);
        if (r != Result::ok)
        {
            PAFFS_DBG(PAFFS_TRACE_ERROR, "Could not init superchain");
            return r;
        }
    }

    if (!asDirty && !rootnode_dirty)
    {
        PAFFS_DBG_S(PAFFS_TRACE_SUPERBLOCK,
                    "Skipping write of superIndex "
                    "because nothing is dirty");
        return Result::ok;
    }

    // Get index of last chain elem (SuperEntry) and increase
    newIndex->no = superChainIndexes[jumpPadNo + 1] + 1;
    newIndex->rootNode = rootnode_addr;
    newIndex->areaMap = device->areaMgmt.getMap();
    newIndex->usedAreas = device->areaMgmt.getUsedAreas();
    newIndex->activeAreas = device->areaMgmt.getActiveAreas();
    newIndex->overallDeletions = device->areaMgmt.getOverallDeletions();

    if (traceMask & PAFFS_TRACE_VERBOSE)
    {
        printf("write Super Index:\n");
        newIndex->print();
    }

    Addr logicalPath[superChainElems];
    r = resolveDirectToLogicalPath(pathToSuperIndexDirect, logicalPath);
    if (r != Result::ok)
    {
        PAFFS_DBG(PAFFS_TRACE_ERROR, "Could not resolve direct to logical path!");
        return r;
    }

    AreaPos directAreas[superChainElems];
    for (int i = 0; i < superChainElems; i++)
    {
        directAreas[i] = extractLogicalArea(pathToSuperIndexDirect[i]);
    }
    AreaPos lastArea = directAreas[jumpPadNo + 1];

    r = insertNewSuperIndex(logicalPath[jumpPadNo + 1], &directAreas[jumpPadNo + 1], newIndex);
    if (r != Result::ok)
    {
        PAFFS_DBG(PAFFS_TRACE_ERROR, "Could not insert new super Page!");
        return r;
    }

    if (!testmode)
    {
        if (!createNew && lastArea == directAreas[jumpPadNo + 1])
        {
            PAFFS_DBG_S(PAFFS_TRACE_SUPERBLOCK,
                        "Committing superindex "
                        "at phys. area %" PRIu32 " was enough!",
                        lastArea);
            rootnode_dirty = false;
            return Result::ok;
        }
    }
    else
    {
        PAFFS_DBG_S(PAFFS_TRACE_SUPERBLOCK, "comitting jumpPad anyway because of test setting!");
    }

    for (int i = jumpPadNo; i > 0; i--)
    {
        JumpPadEntry e = {superChainIndexes[i] + 1, 0, directAreas[i + 1]};
        lastArea = directAreas[i];
        r = insertNewJumpPadEntry(logicalPath[i], &directAreas[i], &e);
        if (r != Result::ok)
        {
            PAFFS_DBG(PAFFS_TRACE_ERROR, "Could not insert new JumpPadEntry (Chain %d)!", i);
            return r;
        }
        if (!createNew && lastArea == directAreas[i])
        {
            PAFFS_DBG_S(PAFFS_TRACE_SUPERBLOCK,
                        "Committing jumpPad no. %d "
                        "at phys. area %" PRIu32 "was enough!",
                        i,
                        lastArea);
            rootnode_dirty = false;
            return Result::ok;
        }
    }
    AnchorEntry a;
    a.no = superChainIndexes[0] + 1;
    a.logPrev = 0;
    a.jumpPadArea = directAreas[1];
    a.param = stdParam;
    a.fsVersion = version;

    lastArea = directAreas[1];
    r = insertNewAnchorEntry(logicalPath[0], &directAreas[0], &a);
    if (r != Result::ok)
    {
        PAFFS_DBG(PAFFS_TRACE_ERROR, "Could not insert new AnchorEntry!");
        return r;
    }
    if (lastArea != directAreas[1])
    {
        PAFFS_DBG(PAFFS_TRACE_BUG,
                  "Anchor entry (%" PRIu32 ") may never "
                  "change its previous area (%" PRIu32 ")!",
                  directAreas[1],
                  lastArea);
        return Result::bug;
    }

    device->journal.addEvent(journalEntry::Success(getTopic()));
    rootnode_dirty = false;
    return Result::ok;
}

void
Superblock::setTestmode(bool t)
{
    testmode = t;
}
/**
 * @brief Ugh, O(n) with areaCount
 */
Result
Superblock::resolveDirectToLogicalPath(Addr directPath[superChainElems],
                                       Addr outPath[superChainElems])
{
    AreaPos p = 0;
    int d = 0;
    for (AreaPos i = 0; i < areasNo; i++)
    {
        p = device->areaMgmt.getPos(i);
        for (d = 0; d < superChainElems; d++)
        {
            if (p == extractLogicalArea(directPath[d]))
                outPath[d] = combineAddress(i, extractPageOffs(directPath[d]));
        }
    }
    return Result::ok;
}

Result
Superblock::fillPathWithFirstSuperblockAreas(Addr directPath[superChainElems])
{
    int foundElems = 0;
    for (AreaPos i = 0; i < areasNo && foundElems <= superChainElems; i++)
    {
        if (device->areaMgmt.getType(i) == AreaType::superblock)
        {
            directPath[foundElems++] = combineAddress(device->areaMgmt.getPos(i), 0);
            PAFFS_DBG_S(
                    PAFFS_TRACE_SUPERBLOCK, "Found new superblock area for chain %d", foundElems);
        }
    }
    if (foundElems != superChainElems)
    {
        PAFFS_DBG(PAFFS_TRACE_ERROR,
                  "Could not find enough superBlocks path! got %d, should %u",
                  foundElems,
                  superChainElems);
        return Result::fail;
    }
    return Result::ok;
}

Result
Superblock::findFirstFreeEntryInArea(AreaPos area, PageOffs* outPos, unsigned int requiredPages)
{
    PageOffs pageOffs[blocksPerArea];
    Result r;
    for (int block = 0; block < blocksPerArea; block++)
    {
        r = findFirstFreeEntryInBlock(area, block, &pageOffs[block], requiredPages);
        if (r == Result::notFound)
        {
            if (block + 1 == blocksPerArea)
            {
                // We are last entry, no matter what previous blocks contain or not, this is full
                return Result::notFound;
            }
            // If this block is full and not on the last position,
            // the next block has to be empty.
            r = findFirstFreeEntryInBlock(area, block + 1, &pageOffs[block + 1], requiredPages);
            *outPos = (block + 1) * pagesPerBlock + pageOffs[block + 1];
            return r;
        }
        else if (r != Result::ok)
        {
            PAFFS_DBG(PAFFS_TRACE_ERROR, "Could not find free Entry in phys. Area %" PRIu32, area);
            return r;
        }
        else
        {
            if (pageOffs[block] != 0)
            {
                // This block contains Entries, but is not full.
                // It has to be the most recent block
                *outPos = block * pagesPerBlock + pageOffs[block];
                return r;
            }
        }
    }
    // Every Block is empty, so return first page in first block!
    *outPos = 0;
    return Result::ok;
}

// out_pos shall point to the first free page
Result
Superblock::findFirstFreeEntryInBlock(AreaPos area,
                                      uint8_t block,
                                      PageOffs* outPos,
                                      unsigned int requiredPages)
{
    unsigned int inARow = 0;
    PageOffs pageOffs = pagesPerBlock * (area * blocksPerArea + block);
    for (unsigned int i = 0; i < pagesPerBlock; i++)
    {
        PageAbs page = i + pageOffs;
        SerialNo no;
        Result r = device->driver.readPage(page, &no, sizeof(SerialNo));
        // Ignore corrected bits b.c. This function is used to write new Entry
        if (r != Result::ok && r != Result::biterrorCorrected)
        {
            return r;
        }
        if (no != emptySerial)
        {
            if (inARow != 0)
            {
                *outPos = 0;
                inARow = 0;
            }
            continue;
        }
        // Unprogrammed, therefore empty

        if (inARow == 0)
        {
            *outPos = i;  // We shall point to the first free page in this row
        }

        if (++inARow == requiredPages)
        {
            return Result::ok;
        }
    }
    return Result::notFound;
}

Result
Superblock::getPathToMostRecentSuperIndex(Addr path[superChainElems],
                                          SerialNo indexes[superChainElems],
                                          AreaPos logPrev[superChainElems])
{
    AreaPos areaPath[superChainElems + 1] = {0};
    Result r;
    for (int i = 0; i < superChainElems; i++)
    {
        r = readMostRecentEntryInArea(
                areaPath[i], &path[i], &indexes[i], &areaPath[i + 1], &logPrev[i]);
        if (r != Result::ok)
        {
            PAFFS_DBG(PAFFS_TRACE_ERROR,
                      "Could not find a Superpage in Area %" PRIu32,
                      extractLogicalArea(path[i]));
            return r;
        }
        if (i > 0 && extractLogicalArea(path[i]) == 0)
        {
            PAFFS_DBG(PAFFS_TRACE_ERROR, "A non-anchor chain elem is located in Area 0!");
            return Result::fail;
        }
        PAFFS_DBG_S(PAFFS_TRACE_SUPERBLOCK,
                    "Found Chain Elem %d at phys. area "
                    "%" PRIu32 " with index %" PRIu32,
                    i,
                    extractLogicalArea(path[i]),
                    indexes[i]);
        if (i < superChainElems - 1)
        {
            PAFFS_DBG_S(
                    PAFFS_TRACE_SUPERBLOCK, "\tpointing to phys. area %" PRIu32, areaPath[i + 1]);
        }
        if (logPrev[i] != 0)
        {
            PAFFS_DBG_S(PAFFS_TRACE_SUPERBLOCK,
                        "The previously used log. area was %" PRIu32,
                        logPrev[i]);
        }
    }

    return Result::ok;
}

Result
Superblock::readMostRecentEntryInArea(
        AreaPos area, Addr* out_pos, SerialNo* outIndex, AreaPos* next, AreaPos* logPrev)
{
    for (int i = 0; i < blocksPerArea; i++)
    {
        PageOffs pos = 0;
        Result r = readMostRecentEntryInBlock(area, i, &pos, outIndex, next, logPrev);
        if (r == Result::ok)
        {
            PAFFS_DBG_S(PAFFS_TRACE_SUPERBLOCK,
                        "Found most recent entry in "
                        "phys. area %" PRIu32 " block %d (abs page %" PRIu32 ")",
                        area,
                        i,
                        pos);
            *out_pos = combineAddress(pos / totalPagesPerArea, pos % totalPagesPerArea);
            return Result::ok;
        }
        if (r != Result::notFound)
        {
            return r;
        }
    }
    return Result::notFound;
}

Result
Superblock::readMostRecentEntryInBlock(AreaPos area,
                                       uint8_t block,
                                       PageOffs* outPos,
                                       SerialNo* outIndex,
                                       AreaPos* next,
                                       AreaPos* logPrev)
{
    SerialNo* maximum = outIndex;
    *maximum = 0;
    *outPos = 0;
    bool overflow = false;
    PageOffs page_offs = pagesPerBlock * (block + area * blocksPerArea);
    for (unsigned int i = 0; i < pagesPerBlock; i++)
    {
        PageAbs page = i + page_offs;
        char buf[sizeof(SerialNo) + sizeof(AreaPos) + sizeof(AreaPos)];
        Result r = device->driver.readPage(
                page, buf, sizeof(SerialNo) + sizeof(AreaPos) + sizeof(AreaPos));
        if (r != Result::ok)
        {
            if (r == Result::biterrorCorrected)
            {
                // TODO trigger SB rewrite. AS may be invalid at this point.
                PAFFS_DBG(PAFFS_TRACE_ALWAYS, "Corrected biterror, but we do not yet write "
                                              "corrected version back to flash.");
            }
            else
            {
                return r;
            }
        }
        SerialNo* no = reinterpret_cast<SerialNo*>(buf);
        if (*no == emptySerial)
        {
            // Unprogrammed, therefore empty
            if (*maximum != 0 || overflow)
            {
                return Result::ok;
            }
            return Result::notFound;
        }

        if (*no > *maximum || *no == 0)
        {  //==0 if overflow occured
            overflow = *no == 0;
            *outPos = i + page_offs;
            *maximum = *no;
            memcpy(logPrev, &buf[sizeof(SerialNo)], sizeof(AreaPos));
            memcpy(next, &buf[sizeof(SerialNo) + sizeof(SerialNo)], sizeof(AreaPos));
        }
    }

    return Result::ok;
}

/**
 * This assumes that the area of the Anchor entry does not change.
 */
Result
Superblock::insertNewAnchorEntry(Addr logPrev, AreaPos* directArea, AnchorEntry* entry)
{
    if (device->areaMgmt.getPos(extractLogicalArea(logPrev)) != *directArea)
    {
        PAFFS_DBG(PAFFS_TRACE_BUG,
                  "Logical (log: %d->%d) and direct Address (%d) differ!",
                  extractLogicalArea(logPrev),
                  device->areaMgmt.getPos(extractLogicalArea(logPrev)),
                  *directArea);
        return Result::bug;
    }

    if (device->areaMgmt.getType(extractLogicalArea(logPrev)) != AreaType::superblock)
    {
        PAFFS_DBG(PAFFS_TRACE_BUG, "Tried to write superIndex outside of superblock Area");
        return Result::bug;
    }
    if (sizeof(AnchorEntry) > dataBytesPerPage)
    {
        PAFFS_DBG(PAFFS_TRACE_ERROR,
                  "Anchor entry (%zu) is bigger than page (%d)!",
                  sizeof(AnchorEntry),
                  dataBytesPerPage);
        return Result::fail;
    }
    entry->logPrev = 0;  // In Anchor entry, this is always zero
    PageOffs page;
    Result r = findFirstFreeEntryInArea(*directArea, &page, 1);
    if (r == Result::notFound)
    {
        PAFFS_DBG_S(PAFFS_TRACE_SUPERBLOCK, "Cycled Anchor Area");
        // just start at first page again, we do not look for other areas as Anchor is always at 0
        page = 0;
    }
    else if (r != Result::ok)
    {
        PAFFS_DBG(PAFFS_TRACE_ERROR, "Could not insert new Anchor Entry!");
        return r;
    }

    r = handleBlockOverflow(*directArea * totalPagesPerArea + page, logPrev, &entry->no);
    if (r != Result::ok)
    {
        PAFFS_DBG(PAFFS_TRACE_ERROR, "Could not handle Block overflow!");
        return r;
    }
    PAFFS_DBG_S(PAFFS_TRACE_SUPERBLOCK,
                "Writing Anchor to phys. Area %" PRIu32 ", "
                "page %" PRIu32 " pointing to area %" PRIu32,
                *directArea,
                page,
                entry->jumpPadArea);
    return device->driver.writePage(
            *directArea * totalPagesPerArea + page, entry, sizeof(AnchorEntry));
}

Result
Superblock::readAnchorEntry(Addr addr, AnchorEntry* entry)
{
    if (sizeof(AnchorEntry) > dataBytesPerPage)
    {
        PAFFS_DBG(PAFFS_TRACE_BUG,
                  "AnchorEntry bigger than dataBytes per Page! (%zu, %u)",
                  sizeof(AnchorEntry),
                  dataBytesPerPage);
        return Result::nimpl;
    }
    if (traceMask & PAFFS_TRACE_SUPERBLOCK && extractLogicalArea(addr) != 0)
    {
        PAFFS_DBG(PAFFS_TRACE_BUG,
                  "Read Anchor entry at phys. area %" PRIu32 ", "
                  "but must only be in area 0!",
                  extractLogicalArea(addr));
        return Result::bug;
    }
    PAFFS_DBG_S(PAFFS_TRACE_SUPERBLOCK,
                "Reading Anchor entry at phys. area %" PRIu32 " page %" PRIu32,
                extractLogicalArea(addr),
                extractPageOffs(addr));
    // No check of areaType because we may not have an AreaMap
    Result r = device->driver.readPage(getPageNumberFromDirect(addr), entry, sizeof(AnchorEntry));

    if (r == Result::biterrorCorrected)
    {
        // TODO trigger SB rewrite. AS may be invalid at this point.
        PAFFS_DBG(PAFFS_TRACE_ALWAYS,
                  "Corrected biterror, but we do not yet write corrected version back to flash.");
        return Result::ok;
    }
    return r;
}

Result
Superblock::insertNewJumpPadEntry(Addr logPrev, AreaPos* directArea, JumpPadEntry* entry)
{
    if (device->areaMgmt.getPos(extractLogicalArea(logPrev)) != *directArea)
    {
        PAFFS_DBG(PAFFS_TRACE_BUG,
                  "Logical (log: %d->%d) and direct Address (%d) differ!",
                  extractLogicalArea(logPrev),
                  device->areaMgmt.getPos(extractLogicalArea(logPrev)),
                  *directArea);
        return Result::bug;
    }
    if (*directArea == 0)
    {
        PAFFS_DBG(PAFFS_TRACE_BUG, "Tried to write not-anchor chain Elem to area 0!");
        return Result::bug;
    }
    if (device->areaMgmt.getType(extractLogicalArea(logPrev)) != AreaType::superblock)
    {
        PAFFS_DBG(PAFFS_TRACE_BUG, "Tried to write superIndex outside of superblock Area");
        return Result::bug;
    }
    if (sizeof(AnchorEntry) > dataBytesPerPage)
    {
        PAFFS_DBG(PAFFS_TRACE_ERROR,
                  "Anchor entry (%zu) is bigger than page (%d)!",
                  sizeof(AnchorEntry),
                  dataBytesPerPage);
        return Result::fail;
    }
    PageOffs page;
    Result r = findFirstFreeEntryInArea(*directArea, &page, 1);
    entry->logPrev = 0;
    if (r == Result::notFound)
    {
        AreaPos p = findBestNextFreeArea(extractLogicalArea(logPrev));
        if (p != extractLogicalArea(logPrev))
        {
            entry->logPrev = extractLogicalArea(logPrev);
            PAFFS_DBG_S(PAFFS_TRACE_SUPERBLOCK,
                        "Moving JumpPad area from "
                        "log. %" PRIu32 " to log. %" PRIu32,
                        entry->logPrev,
                        p);
            *directArea = device->areaMgmt.getPos(p);
        }
        else
        {
            PAFFS_DBG_S(PAFFS_TRACE_SUPERBLOCK, "Warning: reusing JumpPad area.");
        }
        page = 0;
    }
    else if (r != Result::ok)
    {
        PAFFS_DBG(PAFFS_TRACE_ERROR, "Could not insert new JumpPad!");
        return r;
    }

    r = handleBlockOverflow(*directArea * totalPagesPerArea + page, logPrev, &entry->no);
    if (r != Result::ok)
    {
        PAFFS_DBG(PAFFS_TRACE_ERROR, "Could not handle Block overflow!");
        return r;
    }
    PAFFS_DBG_S(PAFFS_TRACE_SUPERBLOCK,
                "Writing jumpPad to phys. Area %" PRIu32 ", "
                "page %" PRIu32 " pointing to area %" PRIu32,
                *directArea,
                page,
                entry->nextArea);
    return device->driver.writePage(
            *directArea * totalPagesPerArea + page, entry, sizeof(JumpPadEntry));
}

Result
Superblock::insertNewSuperIndex(Addr logPrev, AreaPos* directArea, SuperIndex* entry)
{
    if (device->areaMgmt.getPos(extractLogicalArea(logPrev)) != *directArea)
    {
        PAFFS_DBG(PAFFS_TRACE_BUG,
                  "Logical (log: %d->%d) and direct Address (%d) differ!",
                  extractLogicalArea(logPrev),
                  device->areaMgmt.getPos(extractLogicalArea(logPrev)),
                  *directArea);
        return Result::bug;
    }

    if (device->areaMgmt.getType(extractLogicalArea(logPrev)) != AreaType::superblock)
    {
        PAFFS_DBG(PAFFS_TRACE_BUG, "Tried to write superIndex outside of superblock Area");
        return Result::bug;
    }
    if (sizeof(AnchorEntry) > dataBytesPerPage)
    {
        PAFFS_DBG(PAFFS_TRACE_ERROR,
                  "Anchor entry (%zu) is bigger than page (%d)!",
                  sizeof(AnchorEntry),
                  dataBytesPerPage);
        return Result::fail;
    }
    PageOffs page;
    unsigned int neededSummaries = 0;
    for (unsigned int i = 0; i < 2; i++)
    {
        if (entry->areaSummaryPositions[i] > 0)
        {
            neededSummaries++;
        }
    }

    // Every page needs its serial Number
    unsigned int neededBytes = SuperIndex::getNeededBytes(neededSummaries);
    unsigned int neededPages = neededBytes / (dataBytesPerPage - sizeof(SerialNo)) + 1;

    Result r = findFirstFreeEntryInArea(*directArea, &page, neededPages);
    entry->logPrev = 0;
    if (r == Result::notFound)
    {
        AreaPos p = findBestNextFreeArea(extractLogicalArea(logPrev));
        if (p != extractLogicalArea(logPrev))
        {
            entry->logPrev = extractLogicalArea(logPrev);
            PAFFS_DBG_S(PAFFS_TRACE_SUPERBLOCK,
                        "Moving Superindex area from "
                        "log. %" PRIu32 " to log. %" PRIu32,
                        entry->logPrev,
                        p);
            *directArea = device->areaMgmt.getPos(p);
        }
        else
        {
            PAFFS_DBG_S(PAFFS_TRACE_SUPERBLOCK, "Warning: reusing SuperIndex area.");
        }
        page = 0;
    }
    else if (r != Result::ok)
    {
        PAFFS_DBG(PAFFS_TRACE_ERROR, "Could not insert new superIndex!");
        return r;
    }

    r = handleBlockOverflow(*directArea * totalPagesPerArea + page, logPrev, &entry->no);
    if (r != Result::ok)
    {
        PAFFS_DBG(PAFFS_TRACE_ERROR, "Could not handle Block overflow!");
        return r;
    }

    PAFFS_DBG_S(PAFFS_TRACE_SUPERBLOCK,
                "Writing superIndex to phys. Area %" PRIu32 ", page %" PRIu32,
                *directArea,
                page);
    return writeSuperPageIndex(*directArea * totalPagesPerArea + page, entry);
}

// warn: Make sure that free space is sufficient!
Result
Superblock::writeSuperPageIndex(PageAbs pageStart, SuperIndex* entry)
{
    if (device->readOnly)
    {
        PAFFS_DBG(PAFFS_TRACE_BUG, "Tried writing SuperPage in readOnly mode!");
        return Result::bug;
    }
    unsigned int neededBytes = entry->getNeededBytes();
    // note: Serial number is inserted on the first bytes for every page later on.
    // Every page needs its serial Number
    unsigned int neededPages = neededBytes / (dataBytesPerPage - sizeof(SerialNo)) + 1;
    PAFFS_DBG_S(PAFFS_TRACE_SUPERBLOCK,
                "Minimum Pages needed to write SuperIndex: %d (%d bytes)",
                neededPages,
                neededBytes);

    char buf[neededBytes];
    memset(buf, 0, neededBytes);
    Result r;
    r = entry->serializeToBuffer(buf);
    if (r != Result::ok)
    {
        PAFFS_DBG(PAFFS_TRACE_ERROR, "Could not serialize Superpage to buffer!");
        return r;
    }

    unsigned int pointer = 0;
    char pagebuf[dataBytesPerPage];
    for (unsigned page = 0; page < neededPages; page++)
    {
        unsigned int btw = (pointer + dataBytesPerPage - sizeof(SerialNo)) < neededBytes
                                    ? dataBytesPerPage - sizeof(SerialNo)
                                    : neededBytes - pointer;
        // This inserts the serial number at the first Bytes in every page
        memcpy(pagebuf, &entry->no, sizeof(SerialNo));
        memcpy(&pagebuf[sizeof(SerialNo)], &buf[pointer], btw);
        r = device->driver.writePage(pageStart + page, pagebuf, btw + sizeof(SerialNo));
        if (r != Result::ok)
            return r;
        pointer += btw;
    }
    return Result::ok;
}

Result
Superblock::readSuperPageIndex(Addr addr, SuperIndex* entry, bool withAreaMap)
{
    Result r;
    if (!withAreaMap)
    {
        r = device->driver.readPage(
                getPageNumberFromDirect(addr), entry, sizeof(SerialNo) + sizeof(Addr));
        if (r == Result::biterrorCorrected)
        {
            // TODO trigger SB rewrite. AS may be invalid at this point.
            PAFFS_DBG(PAFFS_TRACE_ALWAYS, "Corrected biterror, but we do not yet write corrected "
                                          "version back to flash.");
            return Result::ok;
        }
        return r;
    }
    if (entry->areaMap == NULL)
        return Result::invalidInput;

    if (extractPageOffs(addr) > totalPagesPerArea)
    {
        PAFFS_DBG(PAFFS_TRACE_BUG,
                  "Read SuperPage at page %" PRIu32 " of area %" PRIu32 ", "
                  "but an area is only %" PRIu32 " pages wide!",
                  extractPageOffs(addr),
                  extractLogicalArea(addr),
                  totalPagesPerArea);
    }

    PAFFS_DBG_S(PAFFS_TRACE_SUPERBLOCK,
                "Reading SuperIndex at phys. area %" PRIu32 " page %" PRIu32,
                extractLogicalArea(addr),
                extractPageOffs(addr));
    // TODO: Just read the appropiate number of area Summaries
    // when dynamic ASses are allowed.

    // note: Serial number is inserted on the first bytes for every page later on.
    unsigned int neededBytes = SuperIndex::getNeededBytes(2);
    unsigned int neededPages = neededBytes / (dataBytesPerPage - sizeof(SerialNo)) + 1;
    PAFFS_DBG_S(PAFFS_TRACE_SUPERBLOCK,
                "Maximum Pages needed to read SuperIndex: %d (%d bytes, 2 AS'es)",
                neededPages,
                neededBytes);

    char buf[neededBytes];
    memset(buf, 0, neededBytes);
    uint32_t pointer = 0;
    PageAbs pageBase = getPageNumberFromDirect(addr);
    entry->no = emptySerial;
    char* pagebuf = device->driver.getPageBuffer();
    SerialNo localSerialTmp;
    for (unsigned int page = 0; page < neededPages; page++)
    {
        unsigned int btr = pointer + dataBytesPerPage - sizeof(SerialNo) < neededBytes
                                   ? dataBytesPerPage - sizeof(SerialNo)
                                   : neededBytes - pointer;
        r = device->driver.readPage(pageBase + page, pagebuf, btr + sizeof(SerialNo));
        if (r != Result::ok)
        {
            if (r == Result::biterrorCorrected)
            {
                // TODO trigger SB rewrite. AS may be invalid at this point.
                PAFFS_DBG(PAFFS_TRACE_ALWAYS, "Corrected biterror, but we do not yet write "
                                              "corrected version back to flash.");
                return Result::ok;
            }
            return r;
        }

        memcpy(&localSerialTmp, pagebuf, sizeof(SerialNo));
        if (localSerialTmp == emptySerial)
        {
            PAFFS_DBG(PAFFS_TRACE_ERROR,
                      "Got empty SerialNo during SuperPage read! "
                      "PageOffs: %" PRIu64 ", page: %u",
                      pageBase,
                      page);
            return Result::bug;
        }
        if (entry->no != emptySerial && localSerialTmp != entry->no)
        {
            PAFFS_DBG(PAFFS_TRACE_ERROR,
                      "Got different Serials during SuperPage read! "
                      "Was: %" PRIu32 ", should %" PRIu32,
                      localSerialTmp,
                      entry->no);
            return Result::bug;
        }
        if (entry->no == emptySerial)
        {
            entry->no = localSerialTmp;
        }

        memcpy(&buf[pointer], &pagebuf[sizeof(SerialNo)], btr);
        pointer += btr;
    }
    // buffer ready
    PAFFS_DBG_S(PAFFS_TRACE_WRITE, "SuperIndex Buffer was filled with %" PRIu32 " Bytes.", pointer);

    r = entry->deserializeFromBuffer(device, buf);
    if (r != Result::ok)
    {
        PAFFS_DBG(PAFFS_TRACE_ERROR, "Could not deserialize Superpage from buffer");
        return r;
    }
    return Result::ok;
}

Result
Superblock::handleBlockOverflow(PageAbs newPage, Addr logPrev, SerialNo* serial)
{
    BlockAbs newblock = newPage / pagesPerBlock;
    if (newblock != getBlockNumber(logPrev, *device))
    {
        // reset serial no if we start a new block
        PAFFS_DBG_S(PAFFS_TRACE_SUPERBLOCK,
                    "Deleting phys. Area %d, block %d"
                    " (abs: %d, new abs on %d) for chain Entry",
                    extractLogicalArea(logPrev),
                    extractPageOffs(logPrev) / pagesPerBlock,
                    getBlockNumber(logPrev, *device),
                    newblock);
        *serial = 0;
        Result r = deleteSuperBlock(extractLogicalArea(logPrev),
                                    extractPageOffs(logPrev) / pagesPerBlock);
        if (r != Result::ok)
        {
            PAFFS_DBG(PAFFS_TRACE_ERROR,
                      "Could not delete block of chain Entry! BlockAbs: %" PRIu32,
                      getBlockNumber(logPrev, *device));
            return r;
        }
    }
    return Result::ok;
}

Result
Superblock::deleteSuperBlock(AreaPos area, uint8_t block)
{
    if (device->areaMgmt.getType(area) != AreaType::superblock)
    {
        PAFFS_DBG(PAFFS_TRACE_BUG, "Tried to delete Block outside of SUPERBLOCK area");
        return Result::bug;
    }
    // blocks are deleted sequentially, erasecount is for whole area erases
    if (block == blocksPerArea)
    {
        device->areaMgmt.increaseErasecount(area);
        device->areaMgmt.setStatus(area, AreaStatus::empty);
        device->areaMgmt.setType(area, AreaType::unset);
        device->areaMgmt.decreaseUsedAreas();
        PAFFS_DBG_S(PAFFS_TRACE_AREA,
                    "Info: FREED Superblock Area %u at pos. %u.",
                    area,
                    device->areaMgmt.getPos(area));
    }

    BlockAbs block_offs = device->areaMgmt.getPos(area) * blocksPerArea;
    return device->driver.eraseBlock(block_offs + block);
}

AreaPos
Superblock::findBestNextFreeArea(AreaPos logPrev)
{
    PAFFS_DBG_S(
            PAFFS_TRACE_SUPERBLOCK, "log. Area %" PRIu32 " is full, finding new one...", logPrev);
    for (AreaPos i = 1; i < areasNo; i++)
    {
        if (device->areaMgmt.getStatus(i) == AreaStatus::empty)
        {
            // Following changes to areaMap may not be persistent if SuperIndex was already written
            device->areaMgmt.setStatus(i, AreaStatus::active);
            device->areaMgmt.setType(i, AreaType::superblock);
            /**
             * The area will be empty after the next handleBlockOverflow
             * This allows other SuperIndex areas to switch to this one if flushed in same commit.
             * This should be OK given the better performance in low space environments
             * and that the replacing Area will be a higher order and
             * thus less frequently written to.
             */
            device->areaMgmt.setStatus(logPrev, AreaStatus::empty);
            // Unset is postponed till actual deletion

            PAFFS_DBG_S(PAFFS_TRACE_SUPERBLOCK, "Found log. %" PRIu32, i);
            return i;
        }
    }
    PAFFS_DBG(PAFFS_TRACE_ERROR,
              "Warning: Using same area (log. %" PRIu32 ") for new cycle!",
              logPrev);
    return logPrev;
}
}
