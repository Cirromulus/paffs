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
	GarbageCollection(Device *mdev) : dev(mdev) {};

	/* Special case: Target=unset.
	 * This frees any Type (with a favour to areas with committed AS'es)
	 */
	Result collectGarbage(AreaType target);

private:
	PageOffs countDirtyPages(SummaryEntry* summary);
	AreaPos findNextBestArea(AreaType target,
			SummaryEntry* out_summary, bool* srcAreaContainsData);
	Result moveValidDataToNewArea(AreaPos srcArea,
			AreaPos dstArea, SummaryEntry* summary);
	Result deleteArea(AreaPos area);
};



}
