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

#include <iostream>

#include "commonTest.hpp"
#include <bitlist.hpp>

static constexpr unsigned length = 100;

TEST(Bitlist, SetAndGetBits)
{
    paffs::BitList<length> bitlist;
    // bitlist.printStatus();
    for (unsigned i = 0; i < length; i++)
    {
        ASSERT_EQ(bitlist.getBit(i), false);
    }
    ASSERT_EQ(bitlist.isSetSomewhere(), false);
    bitlist.setBit(length - 1);
    ASSERT_EQ(bitlist.isSetSomewhere(), true);

    for (unsigned i = 0; i < length; i++)
    {
        bitlist.setBit(i);
    }
    // bitlist.printStatus();
    for (unsigned i = 0; i < length; i++)
    {
        ASSERT_EQ(bitlist.getBit(i), true);
    }
    for (unsigned i = 0; i < length; i += 2)
    {
        bitlist.resetBit(i);
    }
    // bitlist.printStatus();
    for (unsigned i = 0; i < length; i++)
    {
        ASSERT_EQ(bitlist.getBit(i), i % 2 != 0);
    }

    paffs::BitList<length> bitlist2;
    memcpy(bitlist2.expose(), bitlist.expose(), bitlist.getByteUsage());

    for (unsigned i = 0; i < length; i++)
    {
        ASSERT_EQ(bitlist.getBit(i), i % 2 != 0);
    }
}


TEST(TwoBitlist, SetAndGetBits)
{
    paffs::TwoBitList<length> bitlist;
    for (unsigned i = 0; i < length; i++)
    {
        ASSERT_EQ(bitlist.getValue(i), 0u);
    }
    for (unsigned i = 0; i < length; i++)
    {
        bitlist.setValue(i, 0b11);
    }
    for (unsigned i = 0; i < length; i++)
    {
        ASSERT_EQ(bitlist.getValue(i), 0b11);
    }
    for (unsigned i = 0; i < length; i++)
    {
        bitlist.setValue(i, 0b10);
    }
    for (unsigned i = 0; i < length; i++)
    {
        ASSERT_EQ(bitlist.getValue(i), 0b10);
    }
    for (unsigned i = 0; i < length; i++)
    {
        bitlist.setValue(i, i % 4);
    }
    for (unsigned i = 0; i < length; i++)
    {
        ASSERT_EQ(bitlist.getValue(i), i % 4);
    }
}
