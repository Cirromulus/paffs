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

#include "bitlist.hpp"
#include "commonTypes.hpp"
#include "journalPageStatemachine.hpp"

#include <functional>

namespace paffs
{
typedef uint32_t PageNo;

struct AddrListCacheElem
{
    Addr cache[addrsPerPage];
    uint16_t positionInParent;
    bool dirty : 1;
    bool active : 1;
    inline
    AddrListCacheElem() : positionInParent(0), dirty(false), active(false){};
    void
    setAddr(PageNo pos, Addr addr);
    Addr
    getAddr(PageNo pos);
};

class PageAddressCache : public JournalTopic
{
    //The three caches for each indirection layer
    AddrListCacheElem tripl[3];
    AddrListCacheElem doubl[2];  // name clash with double
    AddrListCacheElem singl;
    Device& device;
    Inode* mInodePtr;
    bool isInodeDirty = false;

    PageStateMachine<maxPagesPerWrite, 0, JournalEntry::Topic::pac> statemachine;
    Inode mJournalInodeCopy;
    bool processedForeignSuccessElement;

public:
    PageAddressCache(Device& mdev);
    void
    clear();
    Result
    setTargetInode(Inode& node);
    InodeNo
    getTargetInode();
    Result
    getPage(PageNo page, Addr* addr);
    Result
    setPage(PageNo page, Addr addr);
    Result
    setValid();
    Result
    commit();
    bool
    isDirty();

    JournalEntry::Topic
    getTopic() override;
    void
    resetState() override;
    bool
    isInterestedIn(const journalEntry::Max& entry) override;
    Result
    processEntry(const journalEntry::Max& entry, JournalEntryPosition position) override;
    void
    signalEndOfLog() override;
    Result
    setJournallingInode(InodeNo no);

private:
    uint16_t
    getCacheID(AddrListCacheElem* elem);
    void
    informJournal(uint16_t cacheID, const PageNo pos, const Addr newAddr);
    /**
     * @param addrPos is the position of the wanted page in end list
     */
    Result
    loadPath(Addr& anchor,
             PageNo pageOffs,
             AddrListCacheElem* start,
             uint8_t depth,
             PageNo& addrPos);

    Result
    commitPath(Addr& anchor, AddrListCacheElem* path, uint8_t depth);
    Result
    commitElem(AddrListCacheElem& parent, AddrListCacheElem& elem);
    Result
    loadCacheElem(Addr from, AddrListCacheElem& elem);
    Result
    writeCacheElem(Addr& source, AddrListCacheElem& elem);

    Result
    readAddrList(Addr from, Addr list[addrsPerPage]);
    /**
     * @param to should contain former position of page
     */
    Result
    writeAddrList(Addr& source, Addr list[addrsPerPage]);
    bool
    isAddrListPlausible(Addr* addrList, size_t elems);
};
}
