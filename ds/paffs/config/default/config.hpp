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
	static constexpr uint16_t dataBytesPerPage  = 512;
	static constexpr uint8_t  oobBytesPerPage = dataBytesPerPage / 32;
	static constexpr uint16_t pagesPerBlock = 64;
	static constexpr uint16_t blocks = 16;
	static constexpr uint8_t  blocksPerArea = 2;

	//automatically calculated
	static constexpr uint16_t totalBytesPerPage = dataBytesPerPage + oobBytesPerPage;
	static constexpr uint16_t areasNo = blocks / blocksPerArea;
	static constexpr uint16_t totalPagesPerArea = blocksPerArea * pagesPerBlock;
	static constexpr uint16_t dataPagesPerArea = totalPagesPerArea - 1;

	//Cache sizes
	static constexpr uint8_t  treeNodeCacheSize = 5;		//max. 1,5 * dataBytesPerPage(TreeNode) Bytes per Entry
	static constexpr uint8_t  areaSummaryCacheSize = 4;		//Currently  2 Bit per dataPagesPerArea
	static constexpr uint16_t areaSummarySize = 1 + dataPagesPerArea / 8 + 1;
	static constexpr uint8_t  maxNumberOfDevices = 2;
}
