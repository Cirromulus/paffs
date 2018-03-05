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

#include "pageAddressCache.hpp"
#include "dataIO.hpp"
#include "device.hpp"
#include "driver/driver.hpp"
#include "journalEntry.hpp"
#include "journalPageStatemachine_impl.hpp"
#include <cmath>
#include <inttypes.h>

namespace paffs
{
void
AddrListCacheElem::setAddr(PageNo pos, Addr addr)
{
    if (pos >= addrsPerPage)
    {
        PAFFS_DBG(PAFFS_TRACE_BUG,
                  "Tried to set pos %" PRIu32 " to addr,"
                  " allowed < %" PRIu16 "",
                  pos,
                  addrsPerPage);
    }
    if(cache[pos] == addr)
    {
        return;
    }
    cache[pos] = addr;
    dirty = true;
}
Addr
AddrListCacheElem::getAddr(PageNo pos)
{
    if (pos >= addrsPerPage)
    {
        PAFFS_DBG(PAFFS_TRACE_BUG, "Tried to get pos %" PRIu32 ","
                " allowed < %" PRIu16 "", pos, addrsPerPage);
    }
    return cache[pos];
}

PageAddressCache::PageAddressCache(Device& mdev) :
        device(mdev), mInodePtr(nullptr), statemachine(device.journal, device.sumCache){};

void
PageAddressCache::clear()
{
    for (AddrListCacheElem& elem : tripl)
    {
        elem.active = false;
    }
    for (AddrListCacheElem& elem : doubl)
    {
        elem.active = false;
    }
    singl.active = false;

    mInodePtr = nullptr;
    isInodeDirty = false;
}

Result
PageAddressCache::setTargetInode(Inode& node)
{
    if (&node == mInodePtr)
    {
        return Result::ok;
    }
    PAFFS_DBG_S(PAFFS_TRACE_PACACHE, "Set new target inode %" PTYPE_INODENO, node.no);
    if (isDirty())
    {
        PAFFS_DBG_S(PAFFS_TRACE_PACACHE, "Target Inode differs, committing old Inode");
        Result r = commit();
        if (r != Result::ok)
        {
            PAFFS_DBG(PAFFS_TRACE_ERROR, "Could not commit old Inode, aborting");
            return r;
        }
    }
    for (AddrListCacheElem& elem : tripl)
    {
        elem.active = false;
    }
    for (AddrListCacheElem& elem : doubl)
    {
        elem.active = false;
    }
    singl.active = false;
    mInodePtr = &node;

    //journal setInode is delayed until something is really changed

    return Result::ok;
}

InodeNo
PageAddressCache::getTargetInode()
{
    if(mInodePtr == nullptr)
    {
        PAFFS_DBG(PAFFS_TRACE_BUG, "Tried to get InodeNo without Inode!");
        return 0;
    }
    return mInodePtr->no;
}

Result
PageAddressCache::getPage(PageNo page, Addr* addr)
{
    if (mInodePtr == nullptr)
    {
        PAFFS_DBG(PAFFS_TRACE_BUG, "Tried to get Page of null inode");
        return Result::bug;
    }

    if (page < directAddrCount)
    {
        *addr = mInodePtr->direct[page];
        PAFFS_DBG_S(PAFFS_TRACE_PACACHE, "GetPage at %" PRIu32
                    " (direct, %" PTYPE_AREAPOS ":%" PTYPE_PAGEOFFS ")",
                    page, extractLogicalArea(*addr), extractPageOffs(*addr));
        return Result::ok;
    }
    page -= directAddrCount;
    Result r;

    if (page < addrsPerPage)
    {
        PAFFS_DBG_S(PAFFS_TRACE_VERBOSE | PAFFS_TRACE_PACACHE, "Accessing first indirection at %" PRIu32, page);
        if (!singl.active)
        {
            r = loadCacheElem(mInodePtr->indir, singl);
            if (r != Result::ok)
            {
                PAFFS_DBG(PAFFS_TRACE_ERROR, "Could not read first indirection!");
                return r;
            }
        }
        *addr = singl.getAddr(page);
        PAFFS_DBG_S(PAFFS_TRACE_PACACHE, "GetPage at %" PRIu32
                    " (first, %" PTYPE_AREAPOS ":%" PTYPE_PAGEOFFS ")",
                    page, extractLogicalArea(*addr), extractPageOffs(*addr));
        return Result::ok;
    }
    page -= addrsPerPage;
    PageNo addrPos;

    if (page < std::pow(addrsPerPage, 2))
    {
        PAFFS_DBG_S(PAFFS_TRACE_VERBOSE | PAFFS_TRACE_PACACHE, "Accessing second indirection at %" PRIu32, page);
        r = loadPath(mInodePtr->d_indir, page, doubl, 1, addrPos);
        if (r != Result::ok)
        {
            PAFFS_DBG(PAFFS_TRACE_ERROR, "Could not load second indirection!");
            return r;
        }
        *addr = doubl[1].getAddr(addrPos);
        PAFFS_DBG_S( PAFFS_TRACE_PACACHE, "GetPage at %" PRIu32
                    " (second, %" PTYPE_AREAPOS ":%" PTYPE_PAGEOFFS ")",
                    page, extractLogicalArea(*addr), extractPageOffs(*addr));
        return Result::ok;
    }
    page -= std::pow(addrsPerPage, 2);

    if (page < std::pow(addrsPerPage, 3))
    {
        PAFFS_DBG_S(PAFFS_TRACE_VERBOSE | PAFFS_TRACE_PACACHE, "Accessing third indirection at %" PRIu32, page);
        r = loadPath(mInodePtr->t_indir, page, tripl, 2, addrPos);
        if (r != Result::ok)
        {
            PAFFS_DBG(PAFFS_TRACE_ERROR, "Could not load third indirection!");
            return r;
        }
        *addr = tripl[2].getAddr(addrPos);
        PAFFS_DBG_S(PAFFS_TRACE_PACACHE, "GetPage at %" PRIu32
                    " (third, %" PTYPE_AREAPOS ":%" PTYPE_PAGEOFFS ")",
                    page, extractLogicalArea(*addr), extractPageOffs(*addr));
        return Result::ok;
    }

    PAFFS_DBG(PAFFS_TRACE_ERROR,
              "Get Page bigger than allowed! (was --, should <--)");
    // TODO: Actual calculation of values
    return Result::tooBig;
}

Result
PageAddressCache::setPage(PageNo page, Addr addr)
{
    if (mInodePtr == nullptr)
    {
        PAFFS_DBG(PAFFS_TRACE_BUG, "Tried to get Page of null inode");
        return Result::bug;
    }
    if (device.readOnly)
    {
        PAFFS_DBG(PAFFS_TRACE_BUG, "Tried setting PageAddress in readOnly mode!");
        return Result::bug;
    }

    if (traceMask & PAFFS_TRACE_VERBOSE)
    {
        PAFFS_DBG_S(PAFFS_TRACE_PACACHE,
                    "SetPage of %" PTYPE_INODENO " to %" PTYPE_AREAPOS ":%" PTYPE_PAGEOFFS " at %" PRIu32,
                    mInodePtr->no,
                    extractLogicalArea(addr),
                    extractPageOffs(addr),
                    page);
    }

    PageNo relPage = page;

    if (relPage < directAddrCount)
    {
        if(mInodePtr->direct[relPage] == addr)
        {
            return Result::ok;
        }
        //the event gets delayed until we are sure we don't have to commit
        //or else during replay we commit before the log commits (so we would overwrite the last element)
        device.journal.addEvent(journalEntry::pac::SetAddress(mInodePtr->no, page, addr));
        mInodePtr->direct[relPage] = addr;
        isInodeDirty = true;
        return Result::ok;
    }
    relPage -= directAddrCount;
    Result r;

    if(statemachine.getMinSpaceLeft() < 6)
    {
        r = commit();
        if(r != Result::ok)
        {
            PAFFS_DBG(PAFFS_TRACE_ERROR, "Could not commit PAC for statemachine cache!");
            return r;
        }
    }

    if (relPage < addrsPerPage)
    {
        // First Indirection
        if (!singl.active)
        {
            r = loadCacheElem(mInodePtr->indir, singl);
            if (r != Result::ok)
            {
                PAFFS_DBG(PAFFS_TRACE_ERROR, "Could not read first indirection!");
                return r;
            }
        }
        if(singl.getAddr(relPage) == addr)
        {
            return Result::ok;
        }
        device.journal.addEvent(journalEntry::pac::SetAddress(mInodePtr->no, page, addr));
        singl.setAddr(relPage, addr);
        isInodeDirty = true;
        return Result::ok;
    }
    relPage -= addrsPerPage;
    PageNo addrPos;

    if (relPage < std::pow(addrsPerPage, 2))
    {
        r = loadPath(mInodePtr->d_indir, relPage, doubl, 1, addrPos);
        if (r != Result::ok)
        {
            PAFFS_DBG(PAFFS_TRACE_ERROR, "Could not load second indirection!");
            return r;
        }
        if(doubl[1].getAddr(addrPos) == addr)
        {
            return Result::ok;
        }
        device.journal.addEvent(journalEntry::pac::SetAddress(mInodePtr->no, page, addr));
        doubl[1].setAddr(addrPos, addr);
        isInodeDirty = true;
        return Result::ok;
    }
    relPage -= std::pow(addrsPerPage, 2);

    if (relPage < std::pow(addrsPerPage, 3))
    {
        r = loadPath(mInodePtr->t_indir, relPage, tripl, 2, addrPos);
        if (r != Result::ok)
        {
            PAFFS_DBG(PAFFS_TRACE_ERROR, "Could not load third indirection!");
            return r;
        }
        if(tripl[2].getAddr(addrPos) == addr)
        {
            return Result::ok;
        }
        device.journal.addEvent(journalEntry::pac::SetAddress(mInodePtr->no, page, addr));
        tripl[2].setAddr(addrPos, addr);
        isInodeDirty = true;
        return Result::ok;
    }

    PAFFS_DBG(PAFFS_TRACE_ERROR,
              "Get Page bigger than allowed! (was --, should <--)");
    // TODO: Actual calculation of values
    return Result::tooBig;
}

Result
PageAddressCache::setValid()
{
    //We only want to invalidate old Pages which may have been written.
    //It is not allowed for PAC to revert new pages after DATAIO assumes the new Data having set.
    //TODO: Only log success if we actually wrote something
    device.journal.addEvent(journalEntry::pagestate::Success(getTopic()));
    return statemachine.invalidateOldPages();
}

Result
PageAddressCache::commit()
{
    if (mInodePtr == nullptr)
    {
        PAFFS_DBG(PAFFS_TRACE_BUG, "Tried to commit null inode");
        return Result::bug;
    }

    if(!isInodeDirty)
    {
        return Result::ok;
    }

    Result r;
    r = commitPath(mInodePtr->indir, &singl, 0);
    if (r != Result::ok)
    {
        PAFFS_DBG(PAFFS_TRACE_ERROR, "Could not commit first indirection!");
        return r;
    }

    r = commitPath(mInodePtr->d_indir, doubl, 1);
    if (r != Result::ok)
    {
        PAFFS_DBG(PAFFS_TRACE_ERROR, "Could not commit second indirection!");
        return r;
    }

    r = commitPath(mInodePtr->t_indir, tripl, 2);
    if (r != Result::ok)
    {
        PAFFS_DBG(PAFFS_TRACE_ERROR, "Could not commit third indirection!");
        return r;
    }

    if (isDirty())
    {
        PAFFS_DBG(PAFFS_TRACE_ERROR, "Committed all indirections, but something still dirty!");
        return Result::bug;
    }

    //todo: doubled code, use tree update as a signal

    //this would not be necessary if we got the update as a signal
    device.journal.addEvent(journalEntry::pac::UpdateAddressList(*mInodePtr));
    r = device.tree.updateExistingInode(*mInodePtr);

    if(traceMask & PAFFS_TRACE_PACACHE && traceMask & PAFFS_TRACE_VERBOSE)
    {
        printf("Resulting Addresslist of Inode %" PTYPE_INODENO "\n", mInodePtr->no);
        for(uint8_t i = 0; i < 13; i++)
        {
            //intentionally over size of 11, because we print indirects too
            printf("%" PRIu8 "\t%" PTYPE_AREAPOS ":%" PTYPE_PAGEOFFS "\n",
                   i, extractLogicalArea(mInodePtr->direct[i]), extractPageOffs(mInodePtr->direct[i]));
        }
    }

    statemachine.invalidateOldPages();
    device.journal.addEvent(journalEntry::Checkpoint(getTopic()));
    isInodeDirty = false;
    return r;
}

JournalEntry::Topic
PageAddressCache::getTopic()
{
    return JournalEntry::Topic::pac;
}

Result
PageAddressCache::setJournallingInode(InodeNo no)
{
    Result r = device.tree.getInode(no, mJournalInodeCopy);
    if(r != Result::ok)
    {
        return r;
    }
    return setTargetInode(mJournalInodeCopy);
}

Result
PageAddressCache::processEntry(const journalEntry::Max& entry, JournalEntryPosition)
{
    if (entry.base.topic == getTopic())
    {   //normal operations
        switch (entry.pac.operation)
        {
        case journalEntry::PAC::Operation::setAddress:
        {
            if(mInodePtr == nullptr || entry.pac_.setAddress.inodeNo != mInodePtr->no)
            {
                setJournallingInode(entry.pac_.setAddress.inodeNo);
            }
            return setPage(entry.pac_.setAddress.page, entry.pac_.setAddress.addr);
        }
        case journalEntry::PAC::Operation::updateAddresslist:
        {
            if(mInodePtr == nullptr || mInodePtr != &mJournalInodeCopy)
            {
                return Result::bug;
            }
            //Get newest metadata of Inode from tree
            Inode tmp;
            device.tree.getInode(mInodePtr->no, tmp);
            //This intentionally reads over the boundaries of direct array into the indirections
            memcpy(&tmp.direct, &entry.pac_.updateAddressList.inode.direct, (11+3) * sizeof(Addr));
            mJournalInodeCopy = tmp;
            Result r = device.tree.updateExistingInode(mJournalInodeCopy);
            if(r != Result::ok)
            {
                return r;
            }
            journalEntry::Max success;
            success.pagestate_.success = journalEntry::pagestate::Success(getTopic());
            return statemachine.processEntry(success);
        }
        }
        return Result::bug;
    }
    else if(entry.base.topic == JournalEntry::Topic::pagestate &&
            entry.pagestate.target == getTopic())
    {   //statemachine operations
        return statemachine.processEntry(entry);
    }
    else
    {
        PAFFS_DBG(PAFFS_TRACE_BUG, "Got wrong entry to process!");
        return Result::invalidInput;
    }
}

void
PageAddressCache::signalEndOfLog()
{
    if(statemachine.signalEndOfLog() == JournalState::recover)
    {
        //TODO: We may want to find out which cache elements are clean to suppress double versions
    }
    if(mInodePtr != nullptr)
    {
        //This refreshes the Inode we will commit to Index.
        //During replay, changes to the same node are only done to index, not the PAC version
        //TODO: Link them somehow
        Inode tmp;
        device.tree.getInode(mInodePtr->no, tmp);
        //This intentionally reads over the boundaries of direct array into the indirections
        memcpy(&tmp.direct, &mJournalInodeCopy.direct, (11+3) * sizeof(Addr));
        mJournalInodeCopy = tmp;
        commit();
    }
}

bool
PageAddressCache::isDirty()
{
    if (singl.dirty)
    {
        return true;
    }
    if (doubl[0].dirty || doubl[1].dirty)
    {
        return true;
    }
    if (tripl[0].dirty || tripl[1].dirty || tripl[2].dirty)
    {
        return true;
    }
    return false;
}

uint16_t
PageAddressCache::getCacheID(AddrListCacheElem* elem)
{
    if (elem == &tripl[2])
    {
        return 6;
    }
    if (elem == &tripl[1])
    {
        return 5;
    }
    if (elem == &tripl[0])
    {
        return 4;
    }
    if (elem == &doubl[1])
    {
        return 3;
    }
    if (elem == &doubl[0])
    {
        return 2;
    }
    if (elem == &singl)
    {
        return 1;
    }
    return 0;
}

void
PageAddressCache::informJournal(uint16_t cacheID, PageNo pos, Addr newAddr)
{
    // TODO: Inform Journal
    (void)cacheID;
    (void)pos;
    (void)newAddr;
}

Result
PageAddressCache::loadPath(Addr& anchor,
                           PageNo pageOffs,
                           AddrListCacheElem* start,
                           uint8_t depth,
                           PageNo& addrPos)
{
    if(depth >= 3)
    {
        PAFFS_DBG(PAFFS_TRACE_BUG, "Tried calculating a depth bigger than 2!");
        return Result::bug;
    }
    Result r;
    PageNo path[3] = {0};
    for (uint8_t i = 0; i <= depth; i++)
    {
        path[depth - i] =
                static_cast<unsigned int>(pageOffs / std::pow(addrsPerPage, i)) % addrsPerPage;
        if (path[depth - i] >= addrsPerPage)
        {
            PAFFS_DBG(PAFFS_TRACE_BUG,
                      "Miscalculated path for page %" PRIu32 " (was %" PRIu32 ", should < %" PRIu16 ")",
                      pageOffs,
                      path[depth - i],
                      addrsPerPage);
            return Result::bug;
        }
    }
    PAFFS_DBG_S(PAFFS_TRACE_PACACHE,
                "Resulting path: %" PRIu32 ":%" PRIu32 ":%" PRIu32,
                path[0],
                path[1],
                path[2]);

    if (!start[0].active)
    {
        r = loadCacheElem(anchor, start[0]);
        if (r != Result::ok)
        {
            PAFFS_DBG(PAFFS_TRACE_ERROR, "Could not read elem 0 in second indirection!");
            return r;
        }
    }

    for (unsigned int i = 1; i <= depth; i++)
    {
        if (start[i].active && start[i].positionInParent != path[i - 1])
        {
            // We would override existing CacheElem
            r = commitElem(start[i - 1], start[i]);
            if (r != Result::ok)
            {
                PAFFS_DBG(PAFFS_TRACE_ERROR,
                          "Could not commit elem depth %" PRId16 ""
                          " at %" PRIu32 ":%" PRIu32 ":%" PRIu32,
                          i,
                          path[0],
                          path[1],
                          path[2]);
                return r;
            }
        }

        if (!start[i].active || start[i].positionInParent != path[i - 1])
        {
            // Load if it was inactive or has just been committed
            r = loadCacheElem(start[i - 1].getAddr(path[i - 1]), start[i]);
            if (r != Result::ok)
            {
                PAFFS_DBG(PAFFS_TRACE_ERROR,
                          "Could not read elem depth %" PRId16 ""
                          " at %" PRIu32 ":%" PRIu32 ":%" PRIu32,
                          i,
                          path[0],
                          path[1],
                          path[2]);
                return r;
            }
            start[i].positionInParent = path[i - 1];
        }
    }
    addrPos = path[depth];
    return Result::ok;
}

Result
PageAddressCache::commitPath(Addr& anchor, AddrListCacheElem* path, unsigned char depth)
{
    //Journal set here
    Result r;
    for (uint16_t i = depth; i > 0; i--)
    {
        if (path[i].dirty)
        {
            r = commitElem(path[i - 1], path[i]);
            if (r != Result::ok)
            {
                PAFFS_DBG(PAFFS_TRACE_ERROR, "Could not commit Elem in depth %" PRIu16 "!", i);
                return r;
            }
        }
    }
    if (!path[0].dirty)
    {
        return Result::ok;
    }

    bool validEntries = false;
    for (uint32_t i = 0; i < addrsPerPage; i++)
    {
        if (path[0].getAddr(i) != 0)
        {
            validEntries = true;
            break;
        }
    }
    if (!validEntries)
    {
        PAFFS_DBG_S(PAFFS_TRACE_PACACHE, "Deleting CacheElem referenced by anchor");
        // invalidate old page.
        r = device.sumCache.setPageStatus(
                extractLogicalArea(anchor), extractPageOffs(anchor), SummaryEntry::dirty);
        if (r != Result::ok)
        {
            PAFFS_DBG(PAFFS_TRACE_ERROR, "Could not commit invalidate old addresspage!");
            return r;
        }
        anchor = 0;
    }
    else
    {
        PAFFS_DBG_S(PAFFS_TRACE_PACACHE, "writing CacheElem referenced by anchor");
        r = writeCacheElem(anchor, path[0]);
        if (r != Result::ok)
        {
            PAFFS_DBG(PAFFS_TRACE_ERROR, "Could not commit Elem in depth 0!");
            return r;
        }
    }
    path[0].dirty = false;
    return Result::ok;
}

Result
PageAddressCache::commitElem(AddrListCacheElem& parent, AddrListCacheElem& elem)
{
    if (!elem.active)
    {
        PAFFS_DBG(PAFFS_TRACE_BUG, "Tried to commit inactive Cache Elem");
        return Result::bug;
    }
    if (!elem.dirty)
    {
        return Result::ok;
    }
    bool validEntries = false;
    for (unsigned int i = 0; i < addrsPerPage; i++)
    {
        if (elem.getAddr(i) != 0)
        {
            validEntries = true;
            break;
        }
    }
    Result r;
    if (!validEntries)
    {
        PAFFS_DBG_S(PAFFS_TRACE_PACACHE,
                    "Deleting CacheElem referenced by "
                    "parent:%" PRIu16,
                    elem.positionInParent);
        // invalidate old page.
        r = device.sumCache.setPageStatus(extractLogicalArea(parent.cache[elem.positionInParent]),
                                       extractPageOffs(parent.cache[elem.positionInParent]),
                                       SummaryEntry::dirty);
        if (r != Result::ok)
        {
            PAFFS_DBG(PAFFS_TRACE_ERROR, "Could not commit invalidate old addresspage!");
            return r;
        }
        parent.cache[elem.positionInParent] = 0;
        elem.dirty = false;
        elem.active = false;
    }
    else
    {
        PAFFS_DBG_S(PAFFS_TRACE_PACACHE,
                    "Committing CacheElem referenced by "
                    "parent:%" PRIu16,
                    elem.positionInParent);
        r = writeCacheElem(parent.cache[elem.positionInParent], elem);
        if (r != Result::ok)
        {
            PAFFS_DBG(PAFFS_TRACE_ERROR, "Could not commit Cache Elem!");
            return r;
        }
    }
    parent.dirty = true;
    return Result::ok;
}

Result
PageAddressCache::loadCacheElem(Addr from, AddrListCacheElem& elem)
{
    Result r = readAddrList(from, elem.cache);
    if (r == Result::biterrorCorrected)
    {
        PAFFS_DBG(PAFFS_TRACE_INFO, "Corrected biterror in AddrList");
        elem.dirty = true;
        elem.active = true;
        return Result::ok;
    }
    else if (r != Result::ok)
    {
        PAFFS_DBG(PAFFS_TRACE_ERROR, "Could not load Cache Elem");
        return r;
    }
    elem.active = true;
    elem.dirty = false;
    return Result::ok;
}

Result
PageAddressCache::writeCacheElem(Addr& source, AddrListCacheElem& elem)
{
    Result r = writeAddrList(source, elem.cache);
    if (r != Result::ok)
    {
        PAFFS_DBG(PAFFS_TRACE_ERROR, "Could not write Cache Elem");
        return r;
    }
    elem.dirty = false;
    return Result::ok;
}

Result
PageAddressCache::readAddrList(Addr from, Addr list[addrsPerPage])
{
    if (from == 0)
    {
        // This data was not used yet
        PAFFS_DBG_S(PAFFS_TRACE_PACACHE, "load empty CacheElem (new)");
        memset(list, 0, addrsPerPage * sizeof(Addr));
        return Result::ok;
    }
    if (device.areaMgmt.getType(extractLogicalArea(from)) != AreaType::index)
    {
        PAFFS_DBG(PAFFS_TRACE_BUG,
                  "READ ADDR LIST operation of invalid area at %" PRId16 ":%" PRId16 "",
                  extractLogicalArea(from),
                  extractPageOffs(from));
        return Result::bug;
    }
    PAFFS_DBG_S(PAFFS_TRACE_PACACHE,
                "loadCacheElem from %" PTYPE_AREAPOS ":%" PTYPE_PAGEOFFS,
                extractLogicalArea(from),
                extractPageOffs(from));
    Result res = device.driver.readPage(getPageNumber(from, device), list, addrsPerPage * sizeof(Addr));
    if (res != Result::ok)
    {
        PAFFS_DBG(PAFFS_TRACE_ERROR,
                  "Could not load existing addresses"
                  " of first indirection layer");
        return res;
    }
    if (traceMask & PAFFS_TRACE_VERIFY_ALL)
    {
        if (!isAddrListPlausible(list, addrsPerPage))
        {
            PAFFS_DBG(PAFFS_TRACE_ERROR, "Read of addrList inplausible!");
            return Result::fail;
        }
    }
    return res;
}

Result
PageAddressCache::writeAddrList(Addr& source, Addr list[addrsPerPage])
{
    Result r = device.lasterr;
    device.lasterr = Result::ok;
    device.areaMgmt.findWritableArea(AreaType::index);
    if (device.lasterr != Result::ok)
    {
        // TODO: Reset former pagestatus, so that FS will be in a safe state
        return device.lasterr;
    }
    if (device.areaMgmt.getActiveArea(AreaType::index) == 0)
    {
        PAFFS_DBG(PAFFS_TRACE_BUG, "findWritableArea returned 0");
        return Result::bug;
    }
    device.lasterr = r;

    PageOffs firstFreePage = 0;
    if (device.areaMgmt.findFirstFreePage(firstFreePage, device.areaMgmt.getActiveArea(AreaType::index))
        == Result::noSpace)
    {
        PAFFS_DBG(PAFFS_TRACE_BUG,
                  "BUG: findWritableArea returned full area (%" PRId16 ").",
                  device.areaMgmt.getActiveArea(AreaType::index));
        return device.lasterr = Result::bug;
    }
    Addr to = combineAddress(device.areaMgmt.getActiveArea(AreaType::index), firstFreePage);

    // Mark Page as used
    r = statemachine.replacePage(to, source);
    if (r != Result::ok)
    {
        PAFFS_DBG(PAFFS_TRACE_ERROR, "Could not mark Page as used!");
        return r;
    }

    r = device.driver.writePage(
            getPageNumber(to, device), reinterpret_cast<char*>(list), addrsPerPage * sizeof(Addr));
    if (r != Result::ok)
    {
        // TODO: Revert Changes to PageStatus
        return r;
    }

    source = to;

    r = device.areaMgmt.manageActiveAreaFull(AreaType::index);
    if (r != Result::ok)
    {
        return r;
    }

    return Result::ok;
}

bool
PageAddressCache::isAddrListPlausible(Addr* addrList, size_t elems)
{
    for (uint32_t i = 0; i < elems; i++)
    {
        if (extractPageOffs(addrList[i]) == 0)
        {
            continue;
        }

        if (extractPageOffs(addrList[i]) > dataPagesPerArea)
        {
            PAFFS_DBG(PAFFS_TRACE_BUG,
                      "PageList elem %" PRIu32 " Page is higher than possible "
                      "(was %" PTYPE_PAGEOFFS ", should < %" PTYPE_PAGEOFFS ")",
                      i,
                      extractPageOffs(addrList[i]),
                      dataPagesPerArea);
            return false;
        }
        if (extractLogicalArea(addrList[i]) == 0 && extractPageOffs(addrList[i]) != 0)
        {
            PAFFS_DBG(PAFFS_TRACE_BUG,
                      "PageList elem %" PRIu32 " Area is 0, "
                      "but Page is not (%" PTYPE_PAGEOFFS ")",
                      i,
                      extractPageOffs(addrList[i]));
            return false;
        }
    }
    return true;
}
}
