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
#include <superblock.hpp>

using namespace paffs;

TEST(Superblock, IsIndexSerializeFunctionValid)
{
    SuperIndex input, output;
    uint8_t buf[SuperIndex::getNeededBytes(2)];
    Area correctMap[areasNo];
    Area outputMap[areasNo];
    AreaPos correctActiveArea[AreaType::no];
    AreaPos outputActiveArea[AreaType::no];
    TwoBitList<dataPagesPerArea> correctSummaries[2];
    TwoBitList<dataPagesPerArea> outputSummaries[2];

    Result r;
    uint64_t deletions = 0;
    AreaPos usedAreas = 0;

    for(uint16_t i = 0; i < AreaType::no; i++)
    {
        correctActiveArea[i] = i;
        if(i == AreaType::data)
        {
            correctMap[i].status = AreaStatus::active;
        }
    }

    for(uint16_t i = 0; i < areasNo; i++)
    {
        correctMap[i].position = i;
        correctMap[i].erasecount = areasNo - i;
        correctMap[i].type = static_cast<AreaType>(i % AreaType::no);

        deletions += correctMap[i].erasecount;
        usedAreas += correctMap[i].status != AreaStatus::empty ? 1 : 0;
    }

    for(uint8_t i = 0; i < 2; i++)
    {
        for(uint16_t j = 0; j < dataPagesPerArea; j++)
        {
            correctSummaries[i].setValue(j, (j * (j + i)) & 0b100);
        }
    }

    input.logPrev = areasNo;
    input.rootNode = combineAddress(10,99);
    input.areaMap = correctMap;
    input.usedAreas = usedAreas;
    input.activeArea = correctActiveArea;
    input.overallDeletions = deletions;
    input.areaSummaryPositions[0] = correctActiveArea[AreaType::data];
    input.areaSummaryPositions[1] = correctActiveArea[AreaType::index];
    input.summaries[0] = &correctSummaries[0];
    input.summaries[1] = &correctSummaries[1];

    ASSERT_TRUE(input.isPlausible());

    output.areaMap = outputMap;
    output.activeArea = outputActiveArea;
    output.summaries[0] = &outputSummaries[0];
    output.summaries[1] = &outputSummaries[1];

    r = input.serializeToBuffer(buf);
    ASSERT_EQ(r, Result::ok);
    r = output.deserializeFromBuffer(buf);
    ASSERT_EQ(r, Result::ok);

    ASSERT_EQ(input.logPrev, output.logPrev);
    ASSERT_EQ(input.rootNode, output.rootNode);
    ASSERT_EQ(memcmp(correctMap, outputMap, sizeof(Area) * areasNo), 0);
    ASSERT_EQ(input.usedAreas, output.usedAreas);
    ASSERT_TRUE(ArraysMatch(correctActiveArea, outputActiveArea));;
    ASSERT_EQ(input.overallDeletions, output.overallDeletions);
    ASSERT_TRUE(ArraysMatch(input.areaSummaryPositions, output.areaSummaryPositions));
    ASSERT_EQ(correctSummaries[0], outputSummaries[0]);
    ASSERT_EQ(correctSummaries[1], outputSummaries[1]);

    ASSERT_TRUE(output.isPlausible());
}
