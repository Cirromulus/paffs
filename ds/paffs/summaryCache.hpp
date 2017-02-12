/*
 * summaryCache.hpp
 *
 *  Created on: 27 Jan 2017
 *      Author: rooot
 */

#pragma once
#include "paffs.hpp"
namespace paffs {

extern SummaryEntry* summaryCache[];

Result setPageStatus(Dev* dev, AreaPos area, uint8_t page, SummaryEntry state);

SummaryEntry getPageStatus(Dev* dev, AreaPos area, uint8_t page, Result *result);

Result setSummaryStatus(Dev* dev, AreaPos area, SummaryEntry* summary);

SummaryEntry* getSummaryStatus(Dev* dev, AreaPos area, Result *result);

//for retired or unused Areas
Result deleteSummary(Dev* dev, AreaPos area);

//Loads all unclosed AreaSummaries in RAM upon Mount
Result loadAreaSummaries(Dev* dev);

Result commitAreaSummaries(Dev* dev);


}  // namespace paffs
