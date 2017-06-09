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
	static constexpr unsigned int areaSummaryEntrySize = dataPagesPerArea / 4 + 2;
	//excess byte is for dirty- and wasASWritten marker
	unsigned char summaryCache[areaSummaryCacheSize][areaSummaryEntrySize];

	std::unordered_map<AreaPos, uint16_t> translation;	//from area number to array offset
	Device* dev;
public:

	SummaryCache(Device* mdev);

	Result setPageStatus(AreaPos area, PageOffs page, SummaryEntry state);

	SummaryEntry getPageStatus(AreaPos area, PageOffs page, Result *result);

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

	/**
	 * Loads all unclosed AreaSummaries in RAM upon Mount
	 *
	 * \warning High Stack usage scaling with dataPagesPerArea
	 */

	Result loadAreaSummaries();

	Result commitAreaSummaries(bool createNew = false);

private:
	SummaryEntry getPackedStatus(uint16_t position, PageOffs page);

	void setPackedStatus(uint16_t position, PageOffs page, SummaryEntry value);

	void unpackStatusArray(uint16_t position, SummaryEntry* arr);

	void packStatusArray(uint16_t position, SummaryEntry* arr);

	bool isDirty(uint16_t position);

	void setDirty(uint16_t position, bool value=true);

	bool wasASWrittenByCachePosition(uint16_t position);

	void setASWritten(uint16_t position, bool value=true);

	int findNextFreeCacheEntry();

	Result loadUnbufferedArea(AreaPos area, bool urgent);

	Result freeNextBestSummaryCacheEntry(bool urgent);

	PageOffs countDirtyPages(uint16_t position);

	/**
	 * @warn Area needs to be in translation array
	 */
	Result readAreasummary(AreaPos area, SummaryEntry* out_summary, bool complete);

	Result writeAreasummary(AreaPos area, SummaryEntry* summary);

};


}  // namespace paffs
