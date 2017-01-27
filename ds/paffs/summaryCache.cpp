/*
 * summaryCache.cpp
 *
 *  Created on: 27 Jan 2017
 *      Author: rooot
 */


#include "summaryCache.hpp"

namespace paffs {

SummaryEntry* summaryCache[AREASUMMARYCACHESIZE];

Result setState(Dev* dev, AreaPos area, uint8_t page, SummaryEntry state){
	return Result::nimpl;
}

SummaryEntry getState(Dev* dev, AreaPos area, uint8_t page, Result *result){
	*result = Result::nimpl;
	return SummaryEntry::dirty;
}

Result flushAreaSummaries(Dev* dev){
	return Result::nimpl;
}

}  // namespace paffs

