/*
 * standard.hpp
 *
 *  Created on: Feb 14, 2017
 *      Author: user
 */
#include <stdint.h>
#pragma once

namespace paffs{
	//Flash config
	static constexpr uint16_t dataBytesPerPage  = 4096;
	static constexpr uint8_t  oobBytesPerPage = 128;
	static constexpr uint16_t pagesPerBlock = 64;
	static constexpr uint16_t blocks = 4096;
	static constexpr uint8_t  blocksPerArea = 4;
	static constexpr uint8_t  jumpPadNo = 2;

	//Cache sizes
	static constexpr uint8_t  treeNodeCacheSize = 5;		//max. 1,5 * dataBytesPerPage(TreeNode) Bytes per Entry
	static constexpr uint8_t  areaSummaryCacheSize = 4;		//Currently  2 Bit per dataPagesPerArea
	static constexpr uint8_t  maxNumberOfDevices = 2;
}

#include "../auto.hpp"
