/*
 * summaryCache.hpp
 *
 *  Created on: 27 Jan 2017
 *      Author: Pascal Pieper
 */

#pragma once
#include "commonTypes.hpp"
#include <unordered_map>

#ifndef TEST_FRIENDS
#define TEST_FRIENDS
#endif

//#define STR_HELPER(x) #x
//#define STR(x) STR_HELPER(x)
//#pragma message "TEST_FRIENDS: " STR(TEST_FRIENDS)

namespace paffs {

class SummaryCache{
	TEST_FRIENDS;
	unsigned char summaryCache[areaSummaryCacheSize][dataPagesPerArea / 4 + 2]; //excess byte is for dirty marker

	std::unordered_map<AreaPos, uint16_t> translation;	//from area number to array offset
	Device* dev;

public:

	SummaryCache(Device* mdev);

	Result setPageStatus(AreaPos area, uint8_t page, SummaryEntry state);

	SummaryEntry getPageStatus(AreaPos area, uint8_t page, Result *result);

	Result setSummaryStatus(AreaPos area, SummaryEntry* summary);

	Result getSummaryStatus(AreaPos area, SummaryEntry* summary);

	//for retired or unused Areas
	Result deleteSummary(AreaPos area);

	//Loads all unclosed AreaSummaries in RAM upon Mount
	Result loadAreaSummaries();

	Result commitAreaSummaries();

private:
	SummaryEntry getPackedStatus(uint16_t position, uint16_t page);

	void setPackedStatus(uint16_t position, uint16_t page, SummaryEntry value);

	void unpackStatusArray(uint16_t position, SummaryEntry* arr);

	void packStatusArray(uint16_t position, SummaryEntry* arr);

	bool isDirty(uint16_t position);

	void setDirty(uint16_t position, bool value=true);

	int findNextFreeCacheEntry();

	Result loadUnbufferedArea(AreaPos area);

	Result freeNextBestSummaryCacheEntry();

	Result readAreasummary(AreaPos area, SummaryEntry* out_summary, bool complete);

	Result writeAreasummary(AreaPos area, SummaryEntry* summary);

};


}  // namespace paffs
