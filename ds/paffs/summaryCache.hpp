/*
 * summaryCache.hpp
 *
 *  Created on: 27 Jan 2017
 *      Author: Pascal Pieper
 */

#pragma once
#include "commonTypes.hpp"
#include <unordered_map>

namespace paffs {

class SummaryCache{
	SummaryEntry summaryCache[areaSummaryCacheSize][dataPagesPerArea];

	std::unordered_map<AreaPos, uint16_t> translation;	//from area number to array offset
	Device* dev;

public:

	SummaryCache(Device* dev);

	Result setPageStatus(AreaPos area, uint8_t page, SummaryEntry state);

	SummaryEntry getPageStatus(AreaPos area, uint8_t page, Result *result);

	Result setSummaryStatus(AreaPos area, SummaryEntry* summary);

	SummaryEntry* getSummaryStatus(AreaPos area, Result *result);

	//for retired or unused Areas
	Result deleteSummary(AreaPos area);

	//Loads all unclosed AreaSummaries in RAM upon Mount
	Result loadAreaSummaries();

	Result commitAreaSummaries();

private:

	int findNextFreeCacheEntry();

	Result readAreasummary(AreaPos area, SummaryEntry* out_summary, bool complete);

	Result writeAreasummary(AreaPos area, SummaryEntry* summary);

};


}  // namespace paffs
