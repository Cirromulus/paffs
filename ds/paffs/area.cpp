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

const char* areaNames[] = {
		"UNSET",
		"SUPERBLOCK",
		"INDEX",
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

//Returns the absolute page number from *indirect* address
PageAbs getPageNumber(Addr addr, Device* dev){
	PageAbs page = dev->areaMap[extractLogicalArea(addr)].position *
								totalPagesPerArea;
	page += extractPage(addr);
	if(page > areasNo * totalPagesPerArea){
		PAFFS_DBG(PAFFS_TRACE_BUG, "calculated Page number out of range!");
		return 0;
	}
	return page;
}

//Returns the absolute page number from *direct* address
PageAbs getPageNumberFromDirect(Addr addr){
	PageAbs page = extractLogicalArea(addr) * totalPagesPerArea;
	page += extractPage(addr);
	if(page > areasNo * totalPagesPerArea){
		PAFFS_DBG(PAFFS_TRACE_BUG, "calculated Page number out of range!");
		return 0;
	}
	return page;
}

//Returns the absolute page number from *logical* address
BlockAbs getBlockNumber(Addr addr, Device* dev){
	return dev->areaMap[extractLogicalArea(addr)].position * blocksPerArea + extractPage(addr) / pagesPerBlock;
}

//Returns the absolute page number from *direct* address
BlockAbs getBlockNumberFromDirect(Addr addr){
	return extractLogicalArea(addr) * blocksPerArea + extractPage(addr) / pagesPerBlock;
}

//Combines the area number with the relative page starting from first page in area
Addr combineAddress(AreaPos logical_area, PageOffs page){
	Addr addr = 0;
	memcpy(&addr, &page, sizeof(uint32_t));
	memcpy(&reinterpret_cast<char*>(&addr)[sizeof(AreaPos)], &logical_area, sizeof(PageOffs));

	return addr;
}

unsigned int extractLogicalArea(Addr addr){
	unsigned int area = 0;
	memcpy(&area, &reinterpret_cast<char*>(&addr)[sizeof(AreaPos)], sizeof(PageOffs));
	return area;
}
unsigned int extractPage(Addr addr){
	unsigned int page = 0;
	memcpy(&page, &addr, sizeof(PageOffs));
	return page;
}

//May call garbage collection
unsigned int AreaManagement::findWritableArea(AreaType areaType){
	if(dev->activeArea[areaType] != 0 && dev->areaMap[dev->activeArea[areaType]].status != AreaStatus::closed){
		//current Area has still space left
		return dev->activeArea[areaType];
	}

	for(unsigned int area = 0; area < areasNo; area++){
		if(dev->areaMap[area].type == AreaType::unset){
			dev->areaMap[area].type = areaType;
			initArea(area);
			PAFFS_DBG_S(PAFFS_TRACE_AREA, "Found unset Area %u for %s", area, areaNames[areaType]);
			return area;
		}
	}

 	Result r = gc.collectGarbage(areaType);
	if(r != Result::ok){
		PAFFS_DBG(PAFFS_TRACE_ERROR, "GarbageCollection did not free an Area");
		dev->lasterr = r;
		return 0;
	}

	if(dev->areaMap[dev->activeArea[areaType]].status > AreaStatus::empty){
		PAFFS_DBG(PAFFS_TRACE_BUG, "garbage Collection returned invalid Status! (was %d, should <%d)",dev->areaMap[dev->activeArea[areaType]].status, AreaStatus::empty);
		dev->lasterr = Result::bug;
		return 0;
	}

	if(dev->activeArea[areaType] != 0 &&
			dev->areaMap[dev->activeArea[areaType]].status != AreaStatus::closed){
		PAFFS_DBG_S(PAFFS_TRACE_AREA, "Found GC'ed Area %u for %s",
				dev->activeArea[areaType], areaNames[areaType]);
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
	unsigned int ffp;
	if(findFirstFreePage(&ffp, *area) != Result::ok){
		PAFFS_DBG_S(PAFFS_TRACE_AREA, "Info: Area %u (Type %s) full.", *area, areaNames[areaType]);
		//Current Area is full!
		closeArea(*area);
	}else{
		//PAFFS_DBG_S(PAFFS_TRACE_AREA, "Info: Area %u still page %u free.", *area, ffp);
	}

	return Result::ok;
}


//TODO: Add initAreaAs(...) to handle typical areaMap[abc].type = def; initArea(...);
void AreaManagement::initArea(AreaPos area){
	PAFFS_DBG_S(PAFFS_TRACE_AREA, "Info: Init Area %u (pos %u) as %s.", static_cast<unsigned int>(area), static_cast<unsigned int>(dev->areaMap[area].position), areaNames[dev->areaMap[area].type]);
	if(dev->areaMap[area].status == AreaStatus::empty){
		dev->usedAreas++;
	}
	dev->areaMap[area].status = AreaStatus::active;
}

Result AreaManagement::closeArea(AreaPos area){
	dev->areaMap[area].status = AreaStatus::closed;

	PAFFS_DBG_S(PAFFS_TRACE_AREA, "Info: Closed %s Area %u at pos. %u.", areaNames[dev->areaMap[area].type], area, dev->areaMap[area].position);
	return Result::ok;
}

void AreaManagement::retireArea(AreaPos area){
	dev->areaMap[area].status = AreaStatus::closed;
	dev->areaMap[area].type = AreaType::retired;
	for(unsigned block = 0; block < blocksPerArea; block++)
			dev->driver->markBad(dev->areaMap[area].position * blocksPerArea + block);
	PAFFS_DBG_S(PAFFS_TRACE_AREA, "Info: RETIRED Area %u at pos. %u.", area, dev->areaMap[area].position);
}

void AreaManagement::deleteArea(AreaPos area){
	gc.deleteArea(area);
	dev->areaMap[area].status = AreaStatus::empty;
	dev->areaMap[area].type = AreaType::unset;
	dev->usedAreas--;
	PAFFS_DBG_S(PAFFS_TRACE_AREA, "Info: FREED Area %u at pos. %u.", area, dev->areaMap[area].position);
}

}  // namespace paffs
