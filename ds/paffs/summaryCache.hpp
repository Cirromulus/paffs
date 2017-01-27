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

Result setState(Dev* dev, AreaPos area, uint8_t page, SummaryEntry state);

SummaryEntry getState(Dev* dev, AreaPos area, uint8_t page, Result *result);

Result flushAreaSummaries(Dev* dev);

}  // namespace paffs
