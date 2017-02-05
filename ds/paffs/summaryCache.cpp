/*
 * summaryCache.cpp
 *
 *  Created on: 27 Jan 2017
 *      Author: rooot
 */


#include "summaryCache.hpp"
#include "superblock.hpp"

namespace paffs {

//TODO: Reduce Memory usage by packing Sum.enties
SummaryEntry* summaryCache[AREASUMMARYCACHESIZE] = {NULL};

Result setPageStatus(Dev* dev, AreaPos area, uint8_t page, SummaryEntry state){
	if(AREASUMMARYCACHESIZE < area){
		PAFFS_DBG(PAFFS_TRACE_BUG, "Areasummarycache is too small for current implementation");
		return Result::nimpl;
	}
	if(summaryCache[area] == 0){
		summaryCache[area] = (SummaryEntry*) malloc(dev->param->data_pages_per_area*sizeof(SummaryEntry));
		memset(summaryCache[area], 0, dev->param->data_pages_per_area*sizeof(SummaryEntry));
		PAFFS_DBG(PAFFS_TRACE_CACHE, "Created cache entry for area %d", area);
	}
	if(page > dev->param->data_pages_per_area){
		PAFFS_DBG(PAFFS_TRACE_BUG, "Tried to access page out of bounds! (was: %d, should: < %d", page, dev->param->data_pages_per_area);
	}
	summaryCache[area][page] = state;
	return Result::ok;
}

SummaryEntry getPageStatus(Dev* dev, AreaPos area, uint8_t page, Result *result){
	if(AREASUMMARYCACHESIZE < area){
		PAFFS_DBG(PAFFS_TRACE_BUG, "Areasummarycache is too small for current implementation");
		*result = Result::nimpl;
		return SummaryEntry::dirty;
	}
	if(summaryCache[area] == 0){
		summaryCache[area] = (SummaryEntry*) malloc(dev->param->data_pages_per_area*sizeof(SummaryEntry));
		memset(summaryCache[area], 0, dev->param->data_pages_per_area*sizeof(SummaryEntry));
		PAFFS_DBG(PAFFS_TRACE_CACHE, "Created cache entry for area %d (Which is bad as we are in a GET function)", area);
	}
	if(page > dev->param->data_pages_per_area){
		PAFFS_DBG(PAFFS_TRACE_BUG, "Tried to access page out of bounds! (was: %d, should: < %d", page, dev->param->data_pages_per_area);
	}
/*
	if(trace_mask & PAFFS_TRACE_VERIFY_AS){
		for(unsigned int j = 0; j < dev->param->data_pages_per_area; j++){
			if(curr[j] > SummaryEntry::dirty)
				PAFFS_DBG(PAFFS_TRACE_BUG, "Summary of %u contains invalid Entries!", j);
		}
	}
*/

	*result = Result::ok;
	return summaryCache[area][page];
}

SummaryEntry* getSummaryStatus(Dev* dev, AreaPos area, Result *result){
	if(AREASUMMARYCACHESIZE < area){
		PAFFS_DBG(PAFFS_TRACE_BUG, "Areasummarycache is too small for current implementation");
		*result = Result::nimpl;
		return NULL;
	}
	if(summaryCache[area] == 0){
		summaryCache[area] = (SummaryEntry*) malloc(dev->param->data_pages_per_area*sizeof(SummaryEntry));
		memset(summaryCache[area], 0, dev->param->data_pages_per_area*sizeof(SummaryEntry));
		PAFFS_DBG(PAFFS_TRACE_CACHE, "Created cache entry for area %d (Which is bad as we are in a GET function)", area);
	}
	*result = Result::ok;
	return summaryCache[area];
}

Result setSummaryStatus(Dev* dev, AreaPos area, SummaryEntry* summary){
	if(AREASUMMARYCACHESIZE < area){
		PAFFS_DBG(PAFFS_TRACE_BUG, "Areasummarycache is too small for current implementation");
		return Result::nimpl;
	}
	if(summaryCache[area] == 0){
		summaryCache[area] = (SummaryEntry*) malloc(dev->param->data_pages_per_area*sizeof(SummaryEntry));
		memset(summaryCache[area], 0, dev->param->data_pages_per_area*sizeof(SummaryEntry));
		PAFFS_DBG(PAFFS_TRACE_CACHE, "Created cache entry for area %d", area);
	}
	memcpy(summaryCache[area], summary, dev->param->data_pages_per_area*sizeof(SummaryEntry));
	return Result::ok;
}

Result loadAreaSummaries(Dev* dev){
	for(AreaPos i = 0; i < 2; i++){
		summaryCache[i] = (SummaryEntry*) malloc(dev->param->data_pages_per_area*sizeof(SummaryEntry));
	}
	superIndex index = {0};
	index.areaMap = dev->areaMap;
	index.areaSummary[0] = summaryCache[0];
	index.areaSummary[1] = summaryCache[1];
	readSuperIndex(dev, &index);

	//todo: Now load index into cache

	return Result::nimpl;
}

Result commitAreaSummaries(Dev* dev){
	//commit all Areas except two of the emptiest

	//write the two open AS'es to Superindex
	superIndex index = {0};
	index.areaMap = dev->areaMap;
	index.areaSummary[0] = summaryCache[0];
	index.areaSummary[1] = summaryCache[1];
	commitSuperIndex(dev, &index);

	return Result::nimpl;
}

Result removeAreaSummary(Dev* dev, AreaPos area){
	return Result::nimpl;
}

}  // namespace paffs
