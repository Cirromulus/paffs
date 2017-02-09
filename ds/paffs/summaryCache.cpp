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
//FIXME Translation needs to be as big as there are Areas. This is bad.
//TODO: Use Linked List or comparable.
int8_t translation[AREASUMMARYCACHESIZE] = {-1,-1,-1,-1,-1,-1,-1,-1};

int findNextFreeCacheEntry(){
	for(int i = 0; i < AREASUMMARYCACHESIZE; i++){
		if(summaryCache[i] == 0)
			return i;
	}
	return -1;
}

Result setPageStatus(Dev* dev, AreaPos area, uint8_t page, SummaryEntry state){
	if(AREASUMMARYCACHESIZE < area){
		PAFFS_DBG(PAFFS_TRACE_BUG, "Areasummarycache is too small for current implementation");
		return Result::nimpl;
	}
	if(translation[area] <= -1){
		translation[area] = findNextFreeCacheEntry();
		if(translation[area] < 0){
			PAFFS_DBG_S(PAFFS_TRACE_ERROR, "Could not find free Cache Entry for summaryCache, "
					"and flush is not supported yet");
			return Result::nimpl;
		}
		summaryCache[translation[area]] = (SummaryEntry*) malloc(dev->param->data_pages_per_area*sizeof(SummaryEntry));
		memset(summaryCache[translation[area]], 0, dev->param->data_pages_per_area*sizeof(SummaryEntry));
		PAFFS_DBG_S(PAFFS_TRACE_CACHE, "Created cache entry for area %d", area);
	}
	if(page > dev->param->data_pages_per_area){
		PAFFS_DBG(PAFFS_TRACE_BUG, "Tried to access page out of bounds! (was: %d, should: < %d", page, dev->param->data_pages_per_area);
	}
	summaryCache[translation[area]][page] = state;
	return Result::ok;
}

SummaryEntry getPageStatus(Dev* dev, AreaPos area, uint8_t page, Result *result){
	if(AREASUMMARYCACHESIZE < area){
		PAFFS_DBG(PAFFS_TRACE_BUG, "Areasummarycache is too small for current implementation");
		*result = Result::nimpl;
		return SummaryEntry::dirty;
	}
	if(translation[area] <= -1){
		translation[area] = findNextFreeCacheEntry();
		if(translation[area] < 0){
			PAFFS_DBG_S(PAFFS_TRACE_ERROR, "Could not find free Cache Entry for summaryCache, "
					"and flush is not supported yet");
			*result = Result::nimpl;
			return SummaryEntry::dirty;
		}
		summaryCache[translation[area]] = (SummaryEntry*) malloc(dev->param->data_pages_per_area*sizeof(SummaryEntry));
		memset(summaryCache[translation[area]], 0, dev->param->data_pages_per_area*sizeof(SummaryEntry));
		PAFFS_DBG_S(PAFFS_TRACE_CACHE, "Created cache entry for area %d", area);
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
	return summaryCache[translation[area]][page];
}

SummaryEntry* getSummaryStatus(Dev* dev, AreaPos area, Result *result){
	if(AREASUMMARYCACHESIZE < area){
		PAFFS_DBG(PAFFS_TRACE_BUG, "Areasummarycache is too small for current implementation");
		*result = Result::nimpl;
		return NULL;
	}
	if(translation[area] <= -1){
		translation[area] = findNextFreeCacheEntry();
		if(translation[area] < 0){
			PAFFS_DBG_S(PAFFS_TRACE_ERROR, "Could not find free Cache Entry for summaryCache, "
					"and flush is not supported yet");
			*result = Result::nimpl;
			return NULL;
		}
		summaryCache[translation[area]] = (SummaryEntry*) malloc(dev->param->data_pages_per_area*sizeof(SummaryEntry));
		memset(summaryCache[translation[area]], 0, dev->param->data_pages_per_area*sizeof(SummaryEntry));
		PAFFS_DBG_S(PAFFS_TRACE_CACHE, "Created cache entry for area %d", area);
	}
	*result = Result::ok;
	return summaryCache[translation[area]];
}

Result setSummaryStatus(Dev* dev, AreaPos area, SummaryEntry* summary){
	if(AREASUMMARYCACHESIZE < area){
		PAFFS_DBG(PAFFS_TRACE_BUG, "Areasummarycache is too small for current implementation");
		return Result::nimpl;
	}
	if(translation[area] <= -1){
		translation[area] = findNextFreeCacheEntry();
		if(translation[area] < 0){
			PAFFS_DBG_S(PAFFS_TRACE_ERROR, "Could not find free Cache Entry for summaryCache, "
					"and flush is not supported yet");
			return Result::nimpl;
		}
		summaryCache[translation[area]] = (SummaryEntry*) malloc(dev->param->data_pages_per_area*sizeof(SummaryEntry));
		memset(summaryCache[translation[area]], 0, dev->param->data_pages_per_area*sizeof(SummaryEntry));
		PAFFS_DBG_S(PAFFS_TRACE_CACHE, "Created cache entry for area %d", area);
	}
	memcpy(summaryCache[translation[area]], summary, dev->param->data_pages_per_area*sizeof(SummaryEntry));
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

	translation[index.asPositions[0]] = 0;
	translation[index.asPositions[1]] = 1;

	return Result::ok;
}

Result commitAreaSummaries(Dev* dev){
	//todo: commit all Areas except two of the emptiest


	char pos = 0;
	superIndex index = {0};
	index.areaMap = dev->areaMap;
	//write the two open AS'es to Superindex
	for (unsigned int i = 0; i < dev->param->areas_no; i++){
		if((dev->areaMap[i].type == AreaType::data || dev->areaMap[i].type == AreaType::index)
				&& dev->areaMap[i].status == AreaStatus::active){
			if(pos >= 2){
				PAFFS_DBG(PAFFS_TRACE_BUG, "More than one active Area! This is not handled.");
				return Result::bug;
			}
			index.areaSummary[pos++] = summaryCache[translation[i]];
		}
	}
	commitSuperIndex(dev, &index);

	return Result::nimpl;
}

Result removeAreaSummary(Dev* dev, AreaPos area){
	return Result::nimpl;
}

}  // namespace paffs
