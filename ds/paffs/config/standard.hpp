/*
 * standard.hpp
 *
 *  Created on: Feb 14, 2017
 *      Author: user
 */
#include <stdint.h>
#pragma once

namespace paffs{
	static constexpr uint16_t dataBytesPerPage = 512; //1024;
	static constexpr uint16_t dataPagesPerArea = 127; //255;
	static constexpr uint8_t  treeNodeCacheSize = 5;		//672 Bytes per Entry
	static constexpr uint8_t  areaSummaryCacheSize = 4;	//Currently  2 Bit per dataPagesPerArea
	static constexpr uint16_t areaSummarySize = 1 + dataPagesPerArea / 8 + 1;
}
