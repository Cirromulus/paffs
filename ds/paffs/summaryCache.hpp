/*
 * summaryCache.hpp
 *
 *  Created on: 27 Jan 2017
 *      Author: Pascal Pieper
 */

#pragma once
#include "paffs.hpp"
namespace paffs {

class SummaryCache{
	SummaryEntry summaryCache[areaSummaryCacheSize][dataPagesPerArea];
	//FIXME Translation needs to be as big as there are Areas. This is bad.
	//TODO: Use Linked List or HashMap.
	//Translates from areaPosition to summaryCachePosition
	int16_t translation[areaSummaryCacheSize] = {-1,-1,-1,-1,-1,-1,-1,-1};
	Device* dev;

public:

	SummaryCache(Device* dev) : dev(dev){};

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
