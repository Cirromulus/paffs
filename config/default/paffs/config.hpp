/*
 * Default configuration
 *
 *  Created on: Feb 14, 2017
 *      Author: Pascal Pieper
 */
#include <stdint.h>
#pragma once

namespace paffs{
	//Flash config
	static constexpr uint16_t dataBytesPerPage  = 512;
	static constexpr uint8_t  oobBytesPerPage = 16;
	static constexpr uint16_t pagesPerBlock = 64;
	static constexpr uint16_t blocksTotal = 16;
	static constexpr uint8_t  blocksPerArea = 2;
	static constexpr uint8_t  jumpPadNo = 1;				//Should scale with max(0, log2(blocks / 16))

	//MRam config
	static constexpr uint32_t mramSize = 4096 * 512;		//Should be a multiple of 512 for viewer

	//Cache sizes
	static constexpr uint8_t  treeNodeCacheSize = 5;		//max. 1,5 * dataBytesPerPage(TreeNode) Bytes per Entry
	static constexpr uint8_t  areaSummaryCacheSize = 8;		//Currently  2 Bit per dataPagesPerArea
	static constexpr uint8_t  maxNumberOfDevices = 1;
	static constexpr uint8_t  maxNumberOfInodes = 10;		//limits simultaneously open files/folders excluding duplicates
	static constexpr uint8_t  maxNumberOfFiles = 10;		//limits simultaneously open files including duplicates
}
