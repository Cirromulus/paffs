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

#include <stdint.h>
#include <simu/config.hpp>
#pragma once

namespace paffs{
	//Flash config
static constexpr uint16_t dataBytesPerPage = simu::pageDataSize;
static constexpr uint8_t  oobBytesPerPage = simu::pageAuxSize;
static constexpr uint16_t pagesPerBlock = simu::pagesPerBlock;
static constexpr uint16_t blocksTotal = simu::blocksPerPlane * simu::planesPerCell;
static constexpr uint8_t  blocksPerArea = 4;
static constexpr uint8_t  jumpPadNo = 1;				//Should scale with max(0, log2(blocks / 16))

//MRam config
static constexpr uint32_t mramSize = 4096 * 512;		//Should be a multiple of 512 for viewer

//Cache sizes
static constexpr uint8_t  treeNodeCacheSize = 5;		//max. 1,5 * dataBytesPerPage(TreeNode) Bytes per Entry
static constexpr uint8_t  areaSummaryCacheSize = 8;		//Currently  2 Bit per dataPagesPerArea
static constexpr uint8_t  maxNumberOfDevices = 1;
static constexpr uint8_t  maxNumberOfInodes = 10;		//limits simultaneously open files/folders excluding duplicates
static constexpr uint8_t  maxNumberOfFiles = 10;		//limits simultaneously open files including duplicates
static constexpr uint16_t maxPagesPerWrite     = 256;   //limits the size of a single write to a file or folder
};
