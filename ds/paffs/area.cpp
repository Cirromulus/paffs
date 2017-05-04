/*
 * area.cpp
 *
 *  Created on: 27 Jan 2017
 *      Author: Pascal Pieper
 */
#include "area.hpp"
#include "device.hpp"
#include "garbage_collection.hpp"
#include "paffs_trace.hpp"
#include "summaryCache.hpp"
#include <string.h>
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

//Returns the absolute page number
uint64_t getPageNumber(Addr addr, Device* dev){
	uint64_t page = dev->areaMap[extractLogicalArea(addr)].position *
								totalPagesPerArea;
	page += extractPage(addr);
	if(page > areasNo * totalPagesPerArea){
		PAFFS_DBG(PAFFS_TRACE_BUG, "calculated Page number out of range!");
		return 0;
	}
	return page;
}

//Combines the area number with the relative page starting from first page in area
Addr combineAddress(uint32_t logical_area, uint32_t page){
	Addr addr = 0;
	memcpy(&addr, &page, sizeof(uint32_t));
	memcpy(&reinterpret_cast<char*>(&addr)[sizeof(uint32_t)], &logical_area, sizeof(uint32_t));

	return addr;
}

unsigned int extractLogicalArea(Addr addr){
	unsigned int area = 0;
	memcpy(&area, &reinterpret_cast<char*>(&addr)[sizeof(uint32_t)], sizeof(uint32_t));
	return area;
}
unsigned int extractPage(Addr addr){
	unsigned int page = 0;
	memcpy(&page, &addr, sizeof(uint32_t));
	return page;
}

unsigned int AreaManagement::findWritableArea(AreaType areaType){
	if(dev->activeArea[areaType] != 0 && dev->areaMap[dev->activeArea[areaType]].status != AreaStatus::closed){
		//current Area has still space left
		return dev->activeArea[areaType];
	}

	/* This is now done by SummaryCache
	//Look for active Areas loaded from Superblock
	for(unsigned int area = 0; area < areas_no; area++){
		if(dev->areaMap[area].type == areaType && dev->areaMap[area].status == AreaStatus::active){
			PAFFS_DBG_S(PAFFS_TRACE_AREA, "Active Area %d is chosen for new Target.", area);
			return area;
		}
	}*/

	for(unsigned int area = 0; area < areasNo; area++){
		if(dev->areaMap[area].type == AreaType::unset){
			dev->areaMap[area].type = areaType;
			initArea(area);
			return area;
		}
	}

 	Result r = gc.collectGarbage(areaType);
	if(r != Result::ok){
		dev->lasterr = r;
		return 0;
	}

	if(dev->areaMap[dev->activeArea[areaType]].status > AreaStatus::empty){
		PAFFS_DBG(PAFFS_TRACE_BUG, "garbage Collection returned invalid Status! (was %d, should <%d)",dev->areaMap[dev->activeArea[areaType]].status, AreaStatus::empty);
		dev->lasterr = Result::bug;
		return 0;
	}

	if(dev->activeArea[areaType] != 0 && dev->areaMap[dev->activeArea[areaType]].status != AreaStatus::closed){
		return dev->activeArea[areaType];
	}

	//If we arrive here, something buggy must have happened
	PAFFS_DBG(PAFFS_TRACE_BUG, "Garbagecollection pointed to invalid area!");
	dev->lasterr = Result::bug;
	return 0;
}

Result AreaManagement::findFirstFreePage(unsigned int* p_out, unsigned int area){
	Result r;
	for(unsigned int i = 0; i < dataPagesPerArea; i++){
		if(dev->sumCache.getPageStatus(area, i,&r) == SummaryEntry::free){
			*p_out = i;
			return Result::ok;
		}
		if(r != Result::ok)
			return r;
	}
	return Result::nosp;
}

Result AreaManagement::manageActiveAreaFull(AreaPos *area, AreaType areaType){
	Result r;
	if(traceMask & PAFFS_TRACE_VERIFY_AS){
		for(unsigned int i = 0; i < dataPagesPerArea; i++){
			if(dev->sumCache.getPageStatus(*area, i,&r) > SummaryEntry::dirty)
				PAFFS_DBG(PAFFS_TRACE_BUG, "Summary of %u contains invalid Entries (%s)!", *area, resultMsg[static_cast<int>(r)]);
		}
	}

	bool isFull = true;
	for(unsigned int i = 0; i < dataPagesPerArea; i++){
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
		closeArea(*area);
	}

	return Result::ok;
}


//TODO: Add initAreaAs(...) to handle typical areaMap[abc].type = def; initArea(...);
void AreaManagement::initArea(AreaPos area){
	PAFFS_DBG_S(PAFFS_TRACE_AREA, "Info: Init Area %u (pos %u) as %s.", static_cast<unsigned int>(area), static_cast<unsigned int>(dev->areaMap[area].position), area_names[dev->areaMap[area].type]);
	dev->areaMap[area].status = AreaStatus::active;
}

/*Result loadArea(AreaPos area){
	PAFFS_DBG_S(PAFFS_TRACE_AREA, "Info: Loading Areasummary of Area %u (pos %u) as %s.", area, dev->areaMap[area].position, area_names[dev->areaMap[area].type]);
	if(dev->areaMap[area].type != AreaType::dataarea && dev->areaMap[area].type != AreaType::indexarea){
		return Result::ok;
	}
	return readAreasummary(area, dev->areaMap[area].areaSummary, true);
}*/

Result AreaManagement::closeArea(AreaPos area){
	dev->areaMap[area].status = AreaStatus::closed;

	PAFFS_DBG_S(PAFFS_TRACE_AREA, "Info: Closed %s Area %u at pos. %u.", area_names[dev->areaMap[area].type], area, dev->areaMap[area].position);
	return Result::ok;
}

void AreaManagement::retireArea(AreaPos area){
	dev->areaMap[area].status = AreaStatus::closed;
	dev->areaMap[area].type = AreaType::retired;
	for(unsigned block = 0; block < blocksPerArea; block++)
			dev->driver->markBad(dev->areaMap[area].position * blocksPerArea + block);
	PAFFS_DBG_S(PAFFS_TRACE_AREA, "Info: RETIRED Area %u at pos. %u.", area, dev->areaMap[area].position);
}

}  // namespace paffs
