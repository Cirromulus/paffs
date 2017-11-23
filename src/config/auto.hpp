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

namespace paffs{
constexpr int32_t ceil(float num)
{
    return (static_cast<float>(static_cast<int32_t>(num)) == num)
        ? static_cast<int32_t>(num)
        : static_cast<int32_t>(num) + ((num > 0) ? 1 : 0);
}

static_assert(blocksPerArea >= 2, "At least 2 blocks per area are required");
static_assert(blocksTotal >= 8, "At least 8 Blocks are needed to function properly");
static_assert(mramSize % 512 == 0, "Mram Size should be a multiple of 512 for mram Viewer");
static_assert(treeNodeCacheSize >= 2, "At least two tree Nodes have to be cacheable");
static_assert(areaSummaryCacheSize >= 3, "At least three areas have to be cacheable");
static_assert(maxNumberOfInodes >= 2, "At least two inodes may be open simultaneously");
static_assert(maxNumberOfFiles >= 1, "At least one file may be open simultaneously");

//automatically calculated
static constexpr uint16_t totalBytesPerPage = dataBytesPerPage + oobBytesPerPage;
static constexpr uint16_t areasNo = blocksTotal / blocksPerArea;
static constexpr uint16_t totalPagesPerArea = blocksPerArea * pagesPerBlock;
//An Area Summary consists of one 'zero byte' and following one bit per data page
//Calculate the maximum number of pages needed to fit an area summary
static constexpr uint16_t oobPagesPerArea = ceil((totalPagesPerArea / 8.) / dataBytesPerPage);
static constexpr uint16_t dataPagesPerArea = totalPagesPerArea - oobPagesPerArea;
//The actual summary size is calculated with _data_PagesPerArea
static constexpr uint16_t areaSummarySize = 1 + ceil(dataPagesPerArea / 8.);

static constexpr uint16_t superChainElems = jumpPadNo + 2;

static constexpr uint16_t addrsPerPage = dataBytesPerPage / sizeof(Addr);
static constexpr uint16_t minFreeAreas = 1;

static constexpr uint16_t journalTopicLogSize = 500;
}
