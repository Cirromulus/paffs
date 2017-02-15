/*
 * area.cpp
 *
 *  Created on: 27 Jan 2017
 *      Author: Pascal Pieper
 */
#include "area.hpp"
#include "device.hpp"
#include "garbage_collection.hpp"
#include "summaryCache.hpp"
#include <stdlib.h>
namespace paffs {

const char* area_names[] = {
		"UNSET",
		"SUPERBLOCK",
		"INDEX",
		"JOURNAL",
		"DATA",
		"GC_BUFFER",
		"RETIRED",
		"YOUSHOULDNOTBESEEINGTHIS"
};

const char* areaStatusNames[] = {
		"CLOSED",
		"ACTIVE",
		"EMPTY "
};


unsigned int findWritableArea(AreaType areaType, Device* dev){
	if(dev->activeArea[areaType] != 0 && dev->areaMap[dev->activeArea[areaType]].status != AreaStatus::closed){
		//current Area has still space left
		return dev->activeArea[areaType];
	}

	/* This is now done by SummaryCache
	//Look for active Areas loaded from Superblock
	for(unsigned int area = 0; area < dev->param.areas_no; area++){
		if(dev->areaMap[area].type == areaType && dev->areaMap[area].status == AreaStatus::active){
			PAFFS_DBG_S(PAFFS_TRACE_AREA, "Active Area %d is chosen for new Target.", area);
			return area;
		}
	}*/

	for(unsigned int area = 0; area < dev->param.areasNo; area++){
		if(dev->areaMap[area].type == AreaType::unset){
			dev->areaMap[area].type = areaType;
			initArea(dev, area);
			return area;
		}
	}

 	Result r = collectGarbage(dev, areaType);
	if(r != Result::ok){
		lasterr = r;
		return 0;
	}

	if(dev->areaMap[dev->activeArea[areaType]].status > AreaStatus::empty){
		PAFFS_DBG(PAFFS_TRACE_BUG, "garbage Collection returned invalid Status! (was %d, should <%d)",dev->areaMap[dev->activeArea[areaType]].status, AreaStatus::empty);
		lasterr = Result::bug;
		return 0;
	}

	if(dev->activeArea[areaType] != 0 && dev->areaMap[dev->activeArea[areaType]].status != AreaStatus::closed){
		return dev->activeArea[areaType];
	}

	//If we arrive here, something buggy must have happened
	PAFFS_DBG(PAFFS_TRACE_BUG, "Garbagecollection pointed to invalid area!");
	lasterr = Result::bug;
	return 0;
}

Result findFirstFreePage(unsigned int* p_out, Device* dev, unsigned int area){
	Result r;
	for(unsigned int i = 0; i < dev->param.dataPagesPerArea; i++){
		if(dev->sumCache.getPageStatus(area, i,&r) == SummaryEntry::free){
			*p_out = i;
			return Result::ok;
		}
		if(r != Result::ok)
			return r;
	}
	return Result::nosp;
}

Result manageActiveAreaFull(Device *dev, AreaPos *area, AreaType areaType){
	Result r;
	if(trace_mask & PAFFS_TRACE_VERIFY_AS){
		for(unsigned int i = 0; i < dev->param.dataPagesPerArea; i++){
			if(dev->sumCache.getPageStatus(*area, i,&r) > SummaryEntry::dirty)
				PAFFS_DBG(PAFFS_TRACE_BUG, "Summary of %u contains invalid Entries (%s)!", *area, resultMsg[(int)r]);
		}
	}

	bool isFull = true;
	for(unsigned int i = 0; i < dev->param.dataPagesPerArea; i++){
		if(dev->sumCache.getPageStatus(*area, i,&r) == SummaryEntry::free) {
			isFull = false;
			break;
		}
		if(r != Result::ok)
			return r;
	}

	if(isFull){
		PAFFS_DBG_S(PAFFS_TRACE_AREA, "Info: Area %u (Type %s) full.", *area, area_names[areaType]);
		//Current Area is full!
		closeArea(dev, *area);
	}

	return Result::ok;
}


//TODO: Add initAreaAs(...) to handle typical areaMap[abc].type = def; initArea(...);
void initArea(Device* dev, AreaPos area){
	PAFFS_DBG_S(PAFFS_TRACE_AREA, "Info: Init Area %u (pos %u) as %s.", (unsigned int)area, (unsigned int)dev->areaMap[area].position, area_names[dev->areaMap[area].type]);
	dev->areaMap[area].status = AreaStatus::active;
}

/*Result loadArea(Device *dev, AreaPos area){
	PAFFS_DBG_S(PAFFS_TRACE_AREA, "Info: Loading Areasummary of Area %u (pos %u) as %s.", area, dev->areaMap[area].position, area_names[dev->areaMap[area].type]);
	if(dev->areaMap[area].type != AreaType::dataarea && dev->areaMap[area].type != AreaType::indexarea){
		return Result::ok;
	}
	return readAreasummary(area, dev->areaMap[area].areaSummary, true);
}*/

Result closeArea(Device *dev, AreaPos area){
	dev->areaMap[area].status = AreaStatus::closed;

	PAFFS_DBG_S(PAFFS_TRACE_AREA, "Info: Closed %s Area %u at pos. %u.", area_names[dev->areaMap[area].type], area, dev->areaMap[area].position);
	return Result::ok;
}

void retireArea(Device *dev, AreaPos area){
	dev->areaMap[area].status = AreaStatus::closed;
	dev->areaMap[area].type = AreaType::retired;

	PAFFS_DBG_S(PAFFS_TRACE_AREA, "Info: RETIRED Area %u at pos. %u.", area, dev->areaMap[area].position);
}

}  // namespace paffs
