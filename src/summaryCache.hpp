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

class AreaSummaryElem{
	unsigned char entry[dataPagesPerArea / 4 + 1];	//assumes an Odd number of dataPagesPerArea
	unsigned char statusBits; //dirty < asWritten < loadedFromSuperIndex < used
	AreaPos area;
	PageOffs dirtyPages;
	void setUsed(bool used = true);
public:
	AreaSummaryElem();
	~AreaSummaryElem();
	void clear();
	SummaryEntry getStatus(PageOffs page);
	void setStatus(PageOffs page, SummaryEntry value);
	bool isDirty();
	void setDirty(bool dirty = true);
	bool isAsWritten();
	void setAsWritten(bool written = true);
	/**
	 * @brief used to determine, if AS
	 * did not change since loaded from SuperPage
	 */
	bool isLoadedFromSuperPage();
	void setLoadedFromSuperPage(bool loaded = true);
	bool isUsed();
	PageOffs getDirtyPages();
	void setDirtyPages(PageOffs pages);
	void setArea(AreaPos areaPos);
	AreaPos getArea();
};

class SummaryCache{

	//excess byte is for dirty- and wasASWritten marker
	AreaSummaryElem summaryCache[areaSummaryCacheSize];

	std::unordered_map<AreaPos, uint16_t> translation;	//from area number to array offset
	Device* dev;
public:

	SummaryCache(Device* mdev);

	//Same as setPageStatus(area, page, state)
	Result setPageStatus(Addr addr, SummaryEntry state);

	Result setPageStatus(AreaPos area, PageOffs page, SummaryEntry state);

	//Same as getPageStatus(area, page, Result)
	SummaryEntry getPageStatus(Addr addr, Result *result);

	SummaryEntry getPageStatus(AreaPos area, PageOffs page, Result *result);

	Result setSummaryStatus(AreaPos area, SummaryEntry* summary);

	Result getSummaryStatus(AreaPos area, SummaryEntry* summary, bool complete=true);

	//Does not check if pages are dirty or free
	Result getEstimatedSummaryStatus(AreaPos area, SummaryEntry* summary);

	//for retired or unused Areas
	Result deleteSummary(AreaPos area);

	//For Garbage collection to consider cached AS-Areas before others
	bool isCached(AreaPos area);
	//For Garbage collection to consider committed AS-Areas before others
	bool wasASWritten(AreaPos area);

	//For Garbage collection that has deleted the AS too
	void resetASWritten(AreaPos area);

	/**
	 * Loads all unclosed AreaSummaries in RAM upon Mount
	 * Complete wipe of all previous Information
	 * @warning High Stack usage scaling with dataPagesPerArea
	 */
	Result loadAreaSummaries();

	/**
	 * @param createNew if set, a new path will be set instead of
	 * looking for an old one
	 */
	Result commitAreaSummaries(bool createNew = false);

private:
	/**
	 * @Brief uses garbageCollection-buffer to swap a whole Area,
	 * committing its new AS.
	 * @warn Decreases wear-off efficiency if used regularly.
	 */
	Result commitASHard(int &clearedAreaCachePosition);

	SummaryEntry getPackedStatus(uint16_t position, PageOffs page);

	void setPackedStatus(uint16_t position, PageOffs page, SummaryEntry value);

	void unpackStatusArray(uint16_t position, SummaryEntry* arr);

	void packStatusArray(uint16_t position, SummaryEntry* arr);

	int findNextFreeCacheEntry();

	Result loadUnbufferedArea(AreaPos area, bool urgent);

	Result freeNextBestSummaryCacheEntry(bool urgent);

	PageOffs countDirtyPages(uint16_t position);

	PageOffs countUsedPages(uint16_t position);

	PageOffs countUnusedPages(uint16_t position);

	Result commitAndEraseElem(uint16_t position);
	/**
	 * @warn Area needs to be in translation array
	 */
	Result readAreasummary(AreaPos area, SummaryEntry* out_summary, bool complete);

	Result writeAreasummary(uint16_t pos);

};


}  // namespace paffs
