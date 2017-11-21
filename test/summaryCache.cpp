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

#include "commonTest.hpp"
#include <paffs.hpp>
#include <stdio.h>

using namespace testing;

TEST(SummaryCacheElem, packedStatusIntegrity)
{
    paffs::AreaSummaryElem asElem;

    // TODO: Test other features like no clear if dirty etc.
    asElem.setArea(0);
    for (unsigned i = 0; i < paffs::dataPagesPerArea; i++)
    {
        ASSERT_EQ(asElem.getStatus(i), paffs::SummaryEntry::free);
    }
    ASSERT_EQ(asElem.isDirty(), false);
    ASSERT_EQ(asElem.isAreaSummaryWritten(), false);
    ASSERT_EQ(asElem.isLoadedFromSuperPage(), false);

    asElem.setDirty();
    ASSERT_EQ(asElem.isDirty(), true);
    ASSERT_EQ(asElem.isAreaSummaryWritten(), false);
    ASSERT_EQ(asElem.isLoadedFromSuperPage(), false);

    asElem.setDirty(false);
    ASSERT_EQ(asElem.isDirty(), false);

    asElem.setAreaSummaryWritten();
    ASSERT_EQ(asElem.isDirty(), false);
    ASSERT_EQ(asElem.isAreaSummaryWritten(), true);
    ASSERT_EQ(asElem.isLoadedFromSuperPage(), false);

    asElem.setAreaSummaryWritten(false);
    ASSERT_EQ(asElem.isAreaSummaryWritten(), false);

    asElem.setLoadedFromSuperPage();
    ASSERT_EQ(asElem.isDirty(), false);
    ASSERT_EQ(asElem.isAreaSummaryWritten(), false);
    ASSERT_EQ(asElem.isLoadedFromSuperPage(), true);

    for (unsigned int i = 0; i < paffs::dataPagesPerArea; i++)
    {
        asElem.setStatus(i, paffs::SummaryEntry::error);
        for (unsigned int j = 0; j < paffs::dataPagesPerArea; j++)
        {
            if (j == i)
            {
                ASSERT_EQ(asElem.getStatus(j), paffs::SummaryEntry::error);
                continue;
            }
            ASSERT_EQ(asElem.getStatus(j), paffs::SummaryEntry::free);
        }
        asElem.setStatus(i, paffs::SummaryEntry::free);
    }

    // It should set itself Dirty when a setStatus occurs.
    ASSERT_EQ(asElem.isDirty(), true);

    // It should reset |loadedFromSuperPage| when a setStatus occurs.
    ASSERT_EQ(asElem.isLoadedFromSuperPage(), false);
    asElem.setDirty(false);
}
