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

Result loadAreaSummaries(Dev* dev);

Result commitAreaSummaries(Dev* dev);

//If Area is retired, it has to be deleted.
Result removeAreaSummary(Dev* dev, AreaPos area);



}  // namespace paffs
