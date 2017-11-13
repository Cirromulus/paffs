/*
 * garbage_collection.h
 *
 *  Created on: 28.12.2016
 *      Author: Pascal Pieper
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

	/**
	 *	Moves all valid Pages to new Area.
	 */
	Result moveValidDataToNewArea(AreaPos srcArea,
			AreaPos dstArea, SummaryEntry* summary);
private:
	PageOffs countDirtyPages(SummaryEntry* summary);
	AreaPos findNextBestArea(AreaType target,
			SummaryEntry* out_summary, bool* srcAreaContainsData);
};



}
