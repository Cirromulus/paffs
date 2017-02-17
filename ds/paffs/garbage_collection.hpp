/*
 * garbage_collection.h
 *
 *  Created on: 28.12.2016
 *      Author: urinator
 */

#pragma once

#include "commonTypes.hpp"

namespace paffs {

class GarbageCollection{
	Device *dev;

public:
	GarbageCollection(Device *dev) : dev(dev) {};

	Result collectGarbage(AreaType target);

private:
	uint32_t countDirtyPages(AreaPos area);
	AreaPos findNextBestArea(AreaType target,
			SummaryEntry* out_summary, bool* srcAreaContainsData);
	Result moveValidDataToNewArea(AreaPos srcArea,
			AreaPos dstArea, SummaryEntry* summary);
	Result deleteArea(AreaPos area);
};



}
