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

#include "device.hpp"
#include "journalPersistence.hpp"
#include <inttypes.h>

namespace paffs
{
uint16_t
JournalPersistence::getSizeFromJE(const JournalEntry& entry)
{
    switch (entry.topic)
    {
    case JournalEntry::Topic::invalid:
        return 0;
    case JournalEntry::Topic::checkpoint:
        return sizeof(journalEntry::Checkpoint);
    case JournalEntry::Topic::pagestate:
        switch (static_cast<const journalEntry::Pagestate*>(&entry)->type)
        {
        case journalEntry::Pagestate::Type::replacePage:
            return sizeof(journalEntry::pagestate::ReplacePage);
        case journalEntry::Pagestate::Type::replacePagePos:
            return sizeof(journalEntry::pagestate::ReplacePagePos);
        case journalEntry::Pagestate::Type::success:
            return sizeof(journalEntry::pagestate::Success);
        case journalEntry::Pagestate::Type::invalidateOldPages:
            return sizeof(journalEntry::pagestate::InvalidateOldPages);
        }
        break;
    case JournalEntry::Topic::superblock:
        switch (static_cast<const journalEntry::Superblock*>(&entry)->type)
        {
        case journalEntry::Superblock::Type::rootnode:
            return sizeof(journalEntry::superblock::Rootnode);
        case journalEntry::Superblock::Type::areaMap:
            switch (static_cast<const journalEntry::superblock::AreaMap*>(&entry)->operation)
            {
            case journalEntry::superblock::AreaMap::Operation::type:
                return sizeof(journalEntry::superblock::areaMap::Type);
            case journalEntry::superblock::AreaMap::Operation::status:
                return sizeof(journalEntry::superblock::areaMap::Status);
            case journalEntry::superblock::AreaMap::Operation::increaseErasecount:
                return sizeof(journalEntry::superblock::areaMap::IncreaseErasecount);
            case journalEntry::superblock::AreaMap::Operation::position:
                return sizeof(journalEntry::superblock::areaMap::Type);
            case journalEntry::superblock::AreaMap::Operation::swap:
                return sizeof(journalEntry::superblock::areaMap::Swap);
            }
            break;
        case journalEntry::Superblock::Type::activeArea:
            return sizeof(journalEntry::superblock::ActiveArea);
        case journalEntry::Superblock::Type::usedAreas:
            return sizeof(journalEntry::superblock::UsedAreas);
        }
        break;
    case JournalEntry::Topic::areaMgmt:
        switch (static_cast<const journalEntry::AreaMgmt*>(&entry)->operation)
        {
        case journalEntry::AreaMgmt::Operation::initAreaAs:
            return sizeof(journalEntry::areaMgmt::InitAreaAs);
        case journalEntry::AreaMgmt::Operation::closeArea:
            return sizeof(journalEntry::areaMgmt::CloseArea);
        case journalEntry::AreaMgmt::Operation::retireArea:
            return sizeof(journalEntry::areaMgmt::RetireArea);
        case journalEntry::AreaMgmt::Operation::deleteAreaContents:
            return sizeof(journalEntry::areaMgmt::DeleteAreaContents);
        case journalEntry::AreaMgmt::Operation::deleteArea:
            return sizeof(journalEntry::areaMgmt::DeleteArea);
        }
        break;
    case JournalEntry::Topic::garbage:
        switch (static_cast<const journalEntry::GarbageCollection*>(&entry)->operation)
        {
        case journalEntry::GarbageCollection::Operation::moveValidData:
            return sizeof(journalEntry::garbageCollection::MoveValidData);
        }
        break;
    case JournalEntry::Topic::summaryCache:
        switch (static_cast<const journalEntry::SummaryCache*>(&entry)->subtype)
        {
        case journalEntry::SummaryCache::Subtype::commit:
            return sizeof(journalEntry::summaryCache::Commit);
        case journalEntry::SummaryCache::Subtype::remove:
            return sizeof(journalEntry::summaryCache::Remove);
        case journalEntry::SummaryCache::Subtype::reset:
            return sizeof(journalEntry::summaryCache::ResetASWritten);
        case journalEntry::SummaryCache::Subtype::setStatus:
            return sizeof(journalEntry::summaryCache::SetStatus);
        case journalEntry::SummaryCache::Subtype::setStatusBlock:
            return sizeof(journalEntry::summaryCache::SetStatusBlock);
        }
        break;
    case JournalEntry::Topic::tree:
        switch (static_cast<const journalEntry::BTree*>(&entry)->op)
        {
        case journalEntry::BTree::Operation::insert:
            return sizeof(journalEntry::btree::Insert);
        case journalEntry::BTree::Operation::update:
            return sizeof(journalEntry::btree::Update);
        case journalEntry::BTree::Operation::remove:
            return sizeof(journalEntry::btree::Remove);
        }
        break;
    case JournalEntry::Topic::pac:
        switch (static_cast<const journalEntry::PAC*>(&entry)->operation)
        {
        case journalEntry::PAC::Operation::setAddress:
            return sizeof(journalEntry::pac::SetAddress);
        }
        break;
    case JournalEntry::Topic::dataIO:
        switch(static_cast<const journalEntry::DataIO*>(&entry)->operation)
        {
        case journalEntry::DataIO::Operation::newInodeSize:
            return sizeof(journalEntry::dataIO::NewInodeSize);
        }
        break;
    case JournalEntry::Topic::device:
        switch (static_cast<const journalEntry::Device*>(&entry)->action)
        {
            case journalEntry::Device::Action::mkObjInode:
                return sizeof(journalEntry::device::MkObjInode);
            case journalEntry::Device::Action::insertIntoDir:
                return sizeof(journalEntry::device::InsertIntoDir);
            case journalEntry::Device::Action::removeObj:
                return sizeof(journalEntry::device::RemoveObj);
        }
        break;
    }
    return 0;
}

uint16_t
JournalPersistence::getSizeFromMax(const journalEntry::Max& entry)
{
    return getSizeFromJE(entry.base);
}

//======= MRAM ========
Result
MramPersistence::rewind()
{
    curr = sizeof(PageAbs);
    return Result::ok;
}

Result
MramPersistence::seek(JournalEntryPosition& addr)
{
    curr = addr.mram.offs;
    return Result::ok;
}

JournalEntryPosition
MramPersistence::tell()
{
    return JournalEntryPosition(curr);
}

Result
MramPersistence::appendEntry(const JournalEntry& entry)
{
    if (curr + sizeof(journalEntry::Max) > mramSize)
    {
        return Result::noSpace;
    }
    uint16_t size = getSizeFromJE(entry);
    device->driver.writeMRAM(curr, &entry, size);
    PAFFS_DBG_S(PAFFS_TRACE_JOURNAL | PAFFS_TRACE_VERBOSE,
                "Wrote Entry to %" PRIu32 "-%" PRIu32,
                curr,
                curr + size);
    curr += size;
    device->driver.writeMRAM(0, &curr, sizeof(PageAbs));
    if(isLowMem())
    {
        return Result::lowMem;
    }
    return Result::ok;
}

bool
MramPersistence::isLowMem()
{
    if(mramSize == 0)
    {
        return false;
    }
    return mramSize - curr < reservedLogsize;
}

Result
MramPersistence::clear()
{
    curr = sizeof(PageAbs);
    device->driver.writeMRAM(0, &curr, sizeof(PageAbs));
    return Result::ok;
}

Result
MramPersistence::readNextElem(journalEntry::Max& entry)
{
    PageAbs hwm;
    device->driver.readMRAM(0, &hwm, sizeof(PageAbs));
    if (hwm == UINT32_MAX || curr >= hwm)
    {
        return Result::notFound;
    }

    device->driver.readMRAM(curr, &entry, sizeof(journalEntry::Max));
    uint16_t size = getSizeFromMax(entry);
    PAFFS_DBG_S(PAFFS_TRACE_JOUR_PERS,
                "Read entry at %" PRIu32 "-%" PRIu32,
                curr,
                curr + size);
    if (size == 0)
    {
        PAFFS_DBG(PAFFS_TRACE_ERROR, "Read unknown entry!");
        return Result::fail;
    }
    curr += size;
    return Result::ok;
}

//======= FLASH =======

// FIXME: no revert of changes of uncheckpointed entries if it is buffered.

Result
FlashPersistence::rewind()
{
    // TODO: ActiveArea has to be consistent even after a remount
    // TODO: Save AA in Superpage
    if (device->superblock.getActiveArea(AreaType::journal) == 0)
    {
        PAFFS_DBG(PAFFS_TRACE_ERROR, "Invalid journal Area (0)!");
        return Result::bug;
    }
    curr.addr = combineAddress(device->superblock.getActiveArea(AreaType::journal), 0);
    curr.offs = 0;
    return Result::ok;
}
Result
FlashPersistence::seek(JournalEntryPosition& addr)
{
    if (buf.dirty)
    {
        PAFFS_DBG(PAFFS_TRACE_ERROR, "Skipping a journal Commit in seek!");
        return Result::bug;
    }
    curr = addr.flash;
    Result r = loadCurrentPage();
    if (r != Result::ok)
    {
        PAFFS_DBG(PAFFS_TRACE_ERROR, "Could not load a page in journal!");
        return r;
    }
    return Result::ok;
}
JournalEntryPosition
FlashPersistence::tell()
{
    return JournalEntryPosition(curr);
}
Result
FlashPersistence::appendEntry(const JournalEntry& entry)
{
    // Keep in mind that a page cant be written twice when restart of logging after replay
    uint16_t size = getSizeFromJE(entry);
    Result r;
    if (curr.offs + size > dataBytesPerPage)
    {
        // Commit is needed
        r = commitBuf();
        if (r != Result::ok)
        {
            PAFFS_DBG(PAFFS_TRACE_ERROR, "Could not commit buffer");
            // Maybe try other?
            return r;
        }
        r = findNextPos();
        if (r != Result::ok)
        {
            PAFFS_DBG(PAFFS_TRACE_ERROR, "Could not find next Pos for JournalEntry");
            return r;
        }
        // just init current page b.c. we assume free pages after last page
        loadCurrentPage(false);
    }
    memcpy(&buf.data[curr.offs], static_cast<const void*>(&entry), size);
    curr.offs = size;

    if (entry.topic == JournalEntry::Topic::checkpoint)
    {
        // Flush buffer because we have a checkpoint
        r = commitBuf();
        if (r != Result::ok)
        {
            PAFFS_DBG(PAFFS_TRACE_ERROR, "Could not commit buffer");
            // Maybe try other?
            return r;
        }
        r = findNextPos(true);
        if (r != Result::ok)
        {
            PAFFS_DBG(PAFFS_TRACE_ERROR, "Could not find next Pos for JournalEntry");
            return r;
        }
        // just init current page b.c. we assume free pages after last page
        loadCurrentPage(false);
    }
    if(isLowMem())
    {
        return Result::lowMem;
    }
    return Result::ok;
}

bool
FlashPersistence::isLowMem()
{
    if(mramSize == 0)
    {
        return false;
    }
    return totalPagesPerArea * dataBytesPerPage - extractPageOffs(curr.addr) < reservedLogsize;
}

Result
FlashPersistence::clear()
{
    // TODO: Change Area with garbage collection, gc should notice usage upon mount and delete
    return Result::nimpl;
}

Result
FlashPersistence::readNextElem(journalEntry::Max& entry)
{
    if (buf.data[curr.offs] == 0)
    {
        // Reached end of buf
        Result r = findNextPos();
        if (r != Result::ok)
        {
            return Result::notFound;
        }
        loadCurrentPage();
    }
    if (buf.data[curr.offs] == 0)
    {
        return Result::notFound;
    }

    memcpy(static_cast<void*>(&entry),
           &buf.data[curr.offs],
           curr.offs + sizeof(journalEntry::Max) > dataPagesPerArea ? dataPagesPerArea - curr.offs
                                                                    : sizeof(journalEntry::Max));
    uint16_t size = getSizeFromMax(entry);
    PAFFS_DBG_S(PAFFS_TRACE_JOUR_PERS,
                "Read entry at %" PTYPE_AREAPOS ":%" PTYPE_PAGEOFFS " %" PRIu16 "-%" PRIu16,
                extractLogicalArea(curr.addr),
                extractPageOffs(curr.addr),
                curr.offs,
                curr.offs + size);
    if (size == 0)
    {
        PAFFS_DBG(PAFFS_TRACE_ERROR, "Did not recognize JournalEntry");
        return Result::fail;
    }
    curr.offs += size;
    return Result::ok;
}

Result
FlashPersistence::commitBuf()
{
    if (buf.readOnly)
    {
        PAFFS_DBG(PAFFS_TRACE_BUG, "Tried commiting a buffer that was already written!");
        return Result::bug;
    }
    if (!buf.dirty)
    {
        PAFFS_DBG(PAFFS_TRACE_BUG, "Tried commiting a buffer that was not modified!");
        return Result::bug;
    }

    Result r = device->driver.writePage(buf.page, buf.data, dataBytesPerPage);
    if (r != Result::ok)
    {
        PAFFS_DBG(PAFFS_TRACE_ERROR, "Could not write Page for Journal commit");
        return r;
    }
    buf.dirty = false;
    buf.readOnly = true;
    return Result::ok;
}

Result
FlashPersistence::findNextPos(bool forACheckpoint)
{
    if (buf.dirty)
    {
        PAFFS_DBG(PAFFS_TRACE_BUG, "Tried to switch to new Position without commiting old one");
        return Result::bug;
    }
    if (forACheckpoint)
    {
        // Check if we are in reserved space inside area
        // TODO: Commit everything if in safespace
    }
    if (extractPageOffs(curr.addr) == totalPagesPerArea)
    {
        // Ouch, we dont want to reach this
        PAFFS_DBG(PAFFS_TRACE_ERROR, "Could not find a free page in journal!");
        return Result::noSpace;
    }
    curr = JournalEntryPosition(
                   combineAddress(extractLogicalArea(curr.addr), extractPageOffs(curr.addr) + 1), 0)
                   .flash;
    return Result::ok;
}

Result
FlashPersistence::loadCurrentPage(bool readPage)
{
    if (buf.dirty)
    {
        PAFFS_DBG(PAFFS_TRACE_BUG, "loading page with dirty buf!");
        return Result::bug;
    }

    if (readPage)
    {
        Result r = device->driver.readPage(
                getPageNumber(curr.addr, *device), buf.data, dataBytesPerPage);
        if (r != Result::ok)
        {
            PAFFS_DBG(PAFFS_TRACE_ERROR, "Could not read a page for journal!");
            return r;
        }
    }
    else
    {
        memset(buf.data, 0, dataBytesPerPage);
    }
    buf.page = getPageNumber(curr.addr, *device);
    if (buf.data[0] == 0xFF)
    {
        buf.readOnly = false;
        memset(buf.data, 0, dataBytesPerPage);
    }
    else
    {
        buf.readOnly = true;
    }
    buf.dirty = false;
    return Result::ok;
}
};
