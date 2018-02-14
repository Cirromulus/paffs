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
#include <stdio.h>

using namespace testing;
using namespace paffs;

class SmartInodePointer : public testing::Test
{
public:
    static constexpr unsigned int inodePoolSize = 10;
    paffs::InodePool<inodePoolSize> inodePool;

    SmartInodePtr
    getInode(InodeNo no)
    {
        SmartInodePtr ret;
        Result r = inodePool.requireNewInode(no, ret);
        EXPECT_EQ(r, Result::ok);
        return ret;
    }

    void
    checkPoolRefs(InodeNo no, unsigned int refs)
    {
        InodePoolBase::InodeMap::iterator it = inodePool.map.find(no);
        if (refs == 0)
        {
            ASSERT_EQ(it, inodePool.map.end());
            return;
        }
        ASSERT_NE(it, inodePool.map.end());
        ASSERT_EQ(it->second.second, refs);
    }

    virtual void
    SetUp(){};

    virtual void
    TearDown()
    {
        ASSERT_EQ(inodePool.pool.getUsage(), 0u);
    };

    virtual ~SmartInodePointer(){};
};

TEST_F(SmartInodePointer, validReferenceAfterCopyFunctionsAndScopeChanges)
{
    {
        SmartInodePtr a;

        inodePool.requireNewInode(0, a);
        ASSERT_EQ(inodePool.pool.getUsage(), 1u);
        checkPoolRefs(0, 1);
        SmartInodePtr b = a;  // copy Constructor
        checkPoolRefs(0, 2);
        {
            SmartInodePtr c;
            c = a;  // copy operator
            checkPoolRefs(0, 3);
            ASSERT_EQ(inodePool.pool.getUsage(), 1u);
            c = b;  // copy overwrite (same)
            checkPoolRefs(0, 3);
            ASSERT_EQ(inodePool.pool.getUsage(), 1u);
        }
        checkPoolRefs(0, 2);

        inodePool.requireNewInode(1, b);  // overwrite b
        ASSERT_EQ(inodePool.pool.getUsage(), 2u);
        checkPoolRefs(0, 1);
        checkPoolRefs(1, 1);
        b = a;  // copy overwrite (different)
        checkPoolRefs(0, 2);
        checkPoolRefs(1, 0);
    }
    ASSERT_EQ(inodePool.pool.getUsage(), 0u);
    SmartInodePtr z = getInode(0);
    ASSERT_EQ(inodePool.pool.getUsage(), 1u);
    checkPoolRefs(0, 1);
    z = getInode(1);
    checkPoolRefs(0, 0);
    checkPoolRefs(1, 1);
    z = getInode(2);
    checkPoolRefs(0, 0);
    checkPoolRefs(1, 0);
    checkPoolRefs(2, 1);
    Result r = inodePool.getExistingInode(2, z);
    ASSERT_EQ(r, paffs::Result::ok);
    checkPoolRefs(0, 0);
    checkPoolRefs(1, 0);
    checkPoolRefs(2, 1);
}

TEST_F(SmartInodePointer, shouldReportErrorIfInodePoolIsEmpty)
{
    SmartInodePtr ar[inodePoolSize + 1];
    for (unsigned int i = 0; i < inodePoolSize; i++)
    {
        ar[i] = getInode(i);
    }
    Result r = inodePool.requireNewInode(inodePoolSize + 1, ar[inodePoolSize]);
    ASSERT_EQ(r, Result::noSpace);
}
