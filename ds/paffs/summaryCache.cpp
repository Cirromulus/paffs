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
SummaryEntry* summaryCache[AREASUMMARYCACHESIZE];
AreaPos summaryPosition[AREASUMMARYCACHESIZE];

Result setPageStatus(Dev* dev, AreaPos area, uint8_t page, SummaryEntry state){
	return Result::nimpl;
}

SummaryEntry getPageStatus(Dev* dev, AreaPos area, uint8_t page, Result *result){
	*result = Result::nimpl;
/*
	if(trace_mask & PAFFS_TRACE_VERIFY_AS){
		for(unsigned int j = 0; j < dev->param->data_pages_per_area; j++){
			if(curr[j] > SummaryEntry::dirty)
				PAFFS_DBG(PAFFS_TRACE_BUG, "Summary of %u contains invalid Entries!", j);
		}
	}
*/

	return SummaryEntry::dirty;
}

SummaryEntry* getSummaryStatus(Dev* dev, AreaPos area, Result *result){
	*result = Result::nimpl;
	return NULL;
}

Result setSummaryStatus(Dev* dev, AreaPos area, SummaryEntry* summary){
	return Result::nimpl;
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
