/*
 * standard.hpp
 *
 *  Created on: Feb 14, 2017
 *      Author: user
 */

#pragma once

namespace paffs{

	static constexpr uint16_t dataBytesPerPage = 512;
	static constexpr uint16_t dataPagesPerArea = 127;
	static constexpr uint8_t treeNodeCacheSize = 5;		//672 Bytes per Entry
	static constexpr uint8_t areaSummaryCacheSize = 5;	//Currently  1 Byte per dataPagesPerArea

}