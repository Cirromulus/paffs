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
	static constexpr uint8_t  blocksPerArea = 2;

	//automatically calculated
	static constexpr uint16_t totalBytesPerPage = dataBytesPerPage + oobBytesPerPage;
	static constexpr uint16_t areasNo = blocks / blocksPerArea;
	static constexpr uint16_t totalPagesPerArea = blocksPerArea * pagesPerBlock;
	static constexpr uint16_t dataPagesPerArea = 1 - totalPagesPerArea;

	//Cache sizes
	static constexpr uint8_t  treeNodeCacheSize = 5;		//max. 1,5 * dataBytesPerPage(TreeNode) Bytes per Entry
	static constexpr uint8_t  areaSummaryCacheSize = 4;		//Currently  2 Bit per dataPagesPerArea
	static constexpr uint16_t areaSummarySize = 1 + dataPagesPerArea / 8 + 1;
	static constexpr uint8_t  maxNumberOfDevices = 2;
}