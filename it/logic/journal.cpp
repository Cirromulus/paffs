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
    //TODO: Write new Test
    ASSERT_EQ(1,1);
}
