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
#include "journalEntry.hpp"
#include "journal.hpp"
#include "summaryCache.hpp"
#include "area.hpp"

namespace paffs
{

//Forward declaration
class PageAddressCache;

/**
 * The PageStateMachine implements a Logic for when written Pages get applied after an action,
 * or when they get invalidated after a sudden powerloss.
 * Every write of a new page gets logged in a list, including the addres of the replaced page.
 * If everything went ok, all old pages get invalidated.
 */
template <uint16_t maxPages, uint16_t maxPositions, JournalEntry::Topic topic>
class PageStateMachine
{
    static_assert(maxPositions == 0 || maxPositions == maxPages,
                  "maxPositions may be zero or equal to the number of pages!");
    Journal& mJournal;
    SummaryCache& mSummaryCache;
    PageAddressCache* mPac;

    Addr newPageList[maxPages    ];
    Addr oldPageList[maxPages    ];
    PageAbs position[maxPositions];
    uint16_t pageListHWM;
    bool withPosition = maxPages == maxPositions;
    enum class JournalState : uint8_t
    {
        ok,
        invalid,
        recover,
    } journalState;
public:
    PageStateMachine(Journal& journal, SummaryCache& summaryCache);

    PageStateMachine(Journal& journal, SummaryCache& summaryCache, PageAddressCache* pac);

    void
    clear();

    uint16_t
    getMinSpaceLeft();

    Result
    replacePage(Addr neu, Addr old);

    Result
    replacePage(Addr neu, Addr old, PageAbs pos);

    Result
    invalidateOldPages();

    Result
    processEntry(const journalEntry::Max& entry);
    /**
     * @return true, if commit was successful
     * \warn this strongly relies on PAC having been set to the correct Inode beforehand!
     */
    bool
    signalEndOfLog();
};
}

