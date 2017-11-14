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

#include "commonTest.hpp"
#include <paffs/config.hpp>
#include <stdio.h>

using namespace paffs;

class JournalTest : public InitFs
{
};

TEST_F(JournalTest, WriteAndReadMRAM)
{
    Device* dev = fs.getDevice(0);

    dev->superblock.registerRootnode(1234);
    dev->superblock.registerRootnode(5678);
    dev->areaMgmt.setStatus(0, AreaStatus::active);
    dev->areaMgmt.setType(0, AreaType::superblock);
    dev->sumCache.setPageStatus(0, 0, SummaryEntry::used);

    dev->journal.checkpoint();

    dev->superblock.registerRootnode(9010);
    dev->sumCache.setPageStatus(0, 1, SummaryEntry::used);

    // whoops, power went out (No Checkpoint)
    dev->journal.processBuffer();

    ASSERT_EQ(dev->superblock.getRootnodeAddr(), journalEntry::superblock::Rootnode(5678).rootnode);
    Result r;
    ASSERT_EQ(dev->areaMgmt.getStatus(0), AreaStatus::active);
    ASSERT_EQ(dev->areaMgmt.getStatus(0), AreaStatus::active);
    ASSERT_EQ(dev->sumCache.getPageStatus(combineAddress(0, 0), &r), SummaryEntry::used);
    ASSERT_EQ(r, Result::ok);
    ASSERT_EQ(dev->sumCache.getPageStatus(combineAddress(0, 1), &r), SummaryEntry::dirty);
    ASSERT_EQ(r, Result::ok);

    // To clean up
    dev->sumCache.deleteSummary(0);
}
