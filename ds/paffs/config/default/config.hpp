/*
 * standard.hpp
 *
 *  Created on: Feb 14, 2017
 *      Author: user
 */
#include <stdint.h>
#include "../../../../simu/types.hpp"
#pragma once

namespace paffs{
	//Flash config
	static constexpr uint16_t dataBytesPerPage  = PAGE_DATA;
	static constexpr uint8_t  oobBytesPerPage = PAGE_AUX;
	static constexpr uint16_t pagesPerBlock = BLOCK_SIZE;
	static constexpr uint16_t blocks = PLANE_SIZE*CELL_SIZE;
	static constexpr uint8_t  blocksPerArea = 2;
	static constexpr uint8_t  jumpPadNo = 1;

	//Cache sizes
	static constexpr uint8_t  treeNodeCacheSize = 5;		//max. 1,5 * dataBytesPerPage(TreeNode) Bytes per Entry
	static constexpr uint8_t  areaSummaryCacheSize = 8;		//Currently  2 Bit per dataPagesPerArea
	static constexpr uint8_t  maxNumberOfDevices = 1;
}

#include "../auto.hpp"
