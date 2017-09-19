/*
 * standard.hpp
 *
 *  Created on: Feb 14, 2017
 *      Author: user
 */
#include <stdint.h>
#include <simu/config.hpp>
#pragma once

namespace paffs{
	//Flash config
	static constexpr uint16_t dataBytesPerPage  = PAGE_DATA;
	static constexpr uint8_t  oobBytesPerPage = PAGE_AUX;
	static constexpr uint16_t pagesPerBlock = BLOCK_SIZE;
	static constexpr uint16_t blocksTotal = PLANE_SIZE*CELL_SIZE;
	static constexpr uint8_t  blocksPerArea = 4;
	static constexpr uint8_t  jumpPadNo = 1;				//Should scale with max(0, log2(blocks / 16))

	//Cache sizes
	static constexpr uint8_t  treeNodeCacheSize = 5;		//max. 1,5 * dataBytesPerPage(TreeNode) Bytes per Entry
	static constexpr uint8_t  areaSummaryCacheSize = 8;		//Currently  2 Bit per dataPagesPerArea
	static constexpr uint8_t  maxNumberOfDevices = 1;
	static constexpr uint8_t  maxNumberOfInodes = 10;		//limits simultaneously open files/folders excluding duplicates
	static constexpr uint8_t  maxNumberOfFiles = 10;		//limits simultaneously open files including duplicates
};
