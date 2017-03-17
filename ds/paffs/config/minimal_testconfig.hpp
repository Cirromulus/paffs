/*
 * standard.hpp
 *
 *  Created on: Feb 14, 2017
 *      Author: user
 */
#include <stdint.h>
#pragma once

namespace paffs{
	static constexpr uint16_t dataBytesPerPage = 512;
	static constexpr uint16_t dataPagesPerArea = 127;
	static constexpr uint8_t  treeNodeCacheSize = 4;		//max. 1,5 * dataBytesPerPage(TreeNode) Bytes per Entry
	static constexpr uint8_t  areaSummaryCacheSize = 4;		//Currently  2 Bit per dataPagesPerArea
	static constexpr uint16_t areaSummarySize = 1 + dataPagesPerArea / 8 + 1;
}
