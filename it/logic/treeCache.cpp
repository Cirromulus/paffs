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
 * - 2017, Fabian Greif (DLR RY-AVS)
 */
// ----------------------------------------------------------------------------

#include <iostream>

#include "commonTest.hpp"

class TreeTest : public InitFs
{
};

TEST_F(TreeTest, Sizes)
{
    EXPECT_LE(sizeof(paffs::TreeNode), paffs::dataBytesPerPage);
    EXPECT_LE(paffs::leafOrder * sizeof(paffs::Inode), sizeof(paffs::TreeNode));
    EXPECT_LE(paffs::branchOrder * sizeof(paffs::Addr), sizeof(paffs::TreeNode));
}

TEST_F(TreeTest, handleMoreThanCacheLimit)
{
    // Double cache size
    paffs::Device* d = fs.getDevice(0);
    paffs::Result r;

    // insert
    for (unsigned int i = 1; i < paffs::treeNodeCacheSize * 2; i++)
    {
        paffs::Inode test;
        memset(&test, 0, sizeof(paffs::Inode));
        test.no = i;
        r = d->tree.insertInode(test);
        ASSERT_EQ(paffs::Result::ok, r);
        if (r != paffs::Result::ok)
            std::cerr << paffs::err_msg(r) << std::endl;
    }
    // find
    for (unsigned int i = 1; i < paffs::treeNodeCacheSize * 2; i++)
    {
        paffs::Inode test;
        memset(&test, 0, sizeof(paffs::Inode));
        r = d->tree.getInode(i, test);
        ASSERT_EQ(paffs::Result::ok, r);
        if (r != paffs::Result::ok)
            std::cerr << paffs::err_msg(r) << std::endl;
        ASSERT_EQ(test.no, i);
    }
}

TEST_F(TreeTest, coalesceTree)
{
    paffs::Device* d = fs.getDevice(0);
    paffs::Result r;
    const unsigned numberOfNodes = paffs::leafOrder * paffs::branchOrder + 1;

    //This stops the log from filling
    d->journal.disable();

    // insert
    for (unsigned int i = 1; i <= numberOfNodes; i++)
    {
        paffs::Inode test;
        memset(&test, 0, sizeof(paffs::Inode));
        test.no = i;
        r = d->tree.insertInode(test);
        if (r != paffs::Result::ok)
            std::cerr << paffs::err_msg(r) << std::endl;
        ASSERT_EQ(paffs::Result::ok, r);
    }

    // delete reverse
    for (unsigned int i = numberOfNodes; i > 0; i--)
    {
        r = d->tree.deleteInode(i);
        if (r != paffs::Result::ok)
            std::cerr << paffs::err_msg(r) << std::endl;
        ASSERT_EQ(paffs::Result::ok, r);
    }

    // insert
    for (unsigned int i = 1; i <= numberOfNodes; i++)
    {
        paffs::Inode test;
        memset(&test, 0, sizeof(paffs::Inode));
        test.no = i;
        r = d->tree.insertInode(test);
        if (r != paffs::Result::ok)
            std::cerr << paffs::err_msg(r) << std::endl;
        ASSERT_EQ(paffs::Result::ok, r);
    }

    // delete forward
    for (unsigned int i = 1; i <= numberOfNodes; i++)
    {
        r = d->tree.deleteInode(i);
        if (r != paffs::Result::ok)
            std::cerr << paffs::err_msg(r) << std::endl;
        ASSERT_EQ(paffs::Result::ok, r);
    }
}

TEST_F(TreeTest, redistributeTree)
{
    paffs::Device* d = fs.getDevice(0);
    paffs::Result r;
    const unsigned numberOfNodes = paffs::leafOrder * (paffs::branchOrder + 1);

    // insert odd numbers
    for (unsigned int i = 1; i <= numberOfNodes; i++)
    {
        if(i & 0b1)
        {
            continue;
        }
        paffs::Inode test;
        memset(&test, 0, sizeof(paffs::Inode));
        test.no = i;
        r = d->tree.insertInode(test);
        if (r != paffs::Result::ok)
            std::cerr << paffs::err_msg(r) << std::endl;
        ASSERT_EQ(paffs::Result::ok, r);
    }
    //insert even numbers to fill the nodes to a maximum (not just halves)
    for (unsigned int i = 1; i <= numberOfNodes; i++)
    {
        if((i & 0b1) == 0)
        {
            continue;
        }
        paffs::Inode test;
        memset(&test, 0, sizeof(paffs::Inode));
        test.no = i;
        r = d->tree.insertInode(test);
        if (r != paffs::Result::ok)
            std::cerr << paffs::err_msg(r) << std::endl;
        ASSERT_EQ(paffs::Result::ok, r);
    }

    // delete
    for (unsigned int i = numberOfNodes; i > 0; i--)
    {
        r = d->tree.deleteInode(i);
        if (r != paffs::Result::ok)
            std::cerr << paffs::err_msg(r) << std::endl;
        ASSERT_EQ(paffs::Result::ok, r);
    }
}
