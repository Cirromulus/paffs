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
	//excess byte is for dirty- and wasASWritten marker
	unsigned char summaryCache[areaSummaryCacheSize][dataPagesPerArea / 4 + 2];

	std::unordered_map<AreaPos, uint16_t> translation;	//from area number to array offset
	Device* dev;

public:

	SummaryCache(Device* mdev);

	Result setPageStatus(AreaPos area, uint8_t page, SummaryEntry state);

	SummaryEntry getPageStatus(AreaPos area, uint8_t page, Result *result);

	Result setSummaryStatus(AreaPos area, SummaryEntry* summary);

	Result getSummaryStatus(AreaPos area, SummaryEntry* summary, bool complete=true);

	//Does not check if pages are dirty or free
	Result getEstimatedSummaryStatus(AreaPos area, SummaryEntry* summary);

	//for retired or unused Areas
	Result deleteSummary(AreaPos area);

	//For Garbage collection to consider committed AS-Areas before others
	bool wasASWritten(AreaPos area);

	//For Garbage collection that has deleted the AS too
	void resetASWritten(AreaPos area);

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

	bool wasASWrittenByCachePosition(uint16_t position);

	void setASWritten(uint16_t position, bool value=true);

	int findNextFreeCacheEntry();

	Result loadUnbufferedArea(AreaPos area);

	Result freeNextBestSummaryCacheEntry();

	uint32_t countDirtyPages(uint16_t position);

	Result readAreasummary(AreaPos area, SummaryEntry* out_summary, bool complete);

	Result writeAreasummary(AreaPos area, SummaryEntry* summary);

};


}  // namespace paffs
