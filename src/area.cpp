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
#include <inttypes.h>

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
		"EMPTY"
};

//Returns the absolute page number from *indirect* address
PageAbs getPageNumber(Addr addr, Device* dev){
	if(extractLogicalArea(addr) >= areasNo){
		PAFFS_DBG(PAFFS_TRACE_BUG, "Tried accessing area %" PRIu32 ", but we have only %" PRIu32,
				extractLogicalArea(addr), areasNo);
		return 0;
	}
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

unsigned int AreaManagement::findWritableArea(AreaType areaType){
	if(dev->activeArea[areaType] != 0){
		if(dev->areaMap[dev->activeArea[areaType]].status != AreaStatus::active){
			PAFFS_DBG(PAFFS_TRACE_BUG, "ActiveArea of %s not active "
					"(%s, %" PRIu32 " on %" PRIu32 ")",
					areaNames[areaType],
					areaStatusNames[dev->areaMap[dev->activeArea[areaType]].status],
					dev->activeArea[areaType],
					dev->areaMap[dev->activeArea[areaType]].position);
		}
		//current Area has still space left
		if(dev->areaMap[dev->activeArea[areaType]].type != areaType){
			PAFFS_DBG(PAFFS_TRACE_BUG, "ActiveArea does not contain correct "
					"areaType! (Should %s, was %s)", areaNames[areaType],
					areaNames[dev->areaMap[dev->activeArea[areaType]].type]);
		}
		return dev->activeArea[areaType];
	}

	if(dev->usedAreas < areasNo - minFreeAreas || areaType == AreaType::index){
		/**We only take new areas, if we dont hit the reserved pool.
		 * The exeption is Index area, which is needed for committing caches.
		 * If some day we support data cache, this would be allowed to use this pool as well.
		**/
		for(unsigned int area = 0; area < areasNo; area++){
			if(dev->areaMap[area].status == AreaStatus::empty &&
					dev->areaMap[area].type != AreaType::retired){
				dev->areaMap[area].type = areaType;
				initArea(area);
				PAFFS_DBG_S(PAFFS_TRACE_AREA, "Found empty Area %u for %s", area, areaNames[areaType]);
				return area;
			}
		}
	}else if(dev->usedAreas < areasNo){
		PAFFS_DBG_S(PAFFS_TRACE_AREA, "FindWritableArea ignored reserved area");
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

	if(dev->activeArea[areaType] != 0){
		PAFFS_DBG_S(PAFFS_TRACE_AREA, "Found GC'ed Area %u for %s",
				dev->activeArea[areaType], areaNames[areaType]);
		if(dev->areaMap[dev->activeArea[areaType]].status != AreaStatus::active){
			PAFFS_DBG(PAFFS_TRACE_BUG, "An Active Area is not active after GC!"
					" (Area %" PRIu32 " on %" PRIu32 ")", dev->activeArea[areaType],
					dev->areaMap[dev->activeArea[areaType]].position);
			dev->lasterr = Result::bug;
			return 0;
		}
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
	return Result::nospace;
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
	if(dev->areaMap[area].type == AreaType::unset){
		PAFFS_DBG(PAFFS_TRACE_BUG, "Initing Area with invalid type!");
	}
	if(dev->activeArea[dev->areaMap[area].type] != 0 &&
			dev->activeArea[dev->areaMap[area].type] != area){
		PAFFS_DBG(PAFFS_TRACE_BUG, "Activating area %" PRIu32 " while different Area "
				"(%" PRIu32 " on %" PRIu32 ") still active!",
				area, dev->activeArea[dev->areaMap[area].type],
				dev->areaMap[dev->activeArea[dev->areaMap[area].type]].position);
	}
	PAFFS_DBG_S(PAFFS_TRACE_AREA, "Info: Init Area %u (pos %u) as %s.", static_cast<unsigned int>(area), static_cast<unsigned int>(dev->areaMap[area].position), areaNames[dev->areaMap[area].type]);
	if(dev->areaMap[area].status == AreaStatus::empty){
		dev->usedAreas++;
	}
	dev->areaMap[area].status = AreaStatus::active;
}

Result AreaManagement::closeArea(AreaPos area){
	dev->areaMap[area].status = AreaStatus::closed;
	dev->activeArea[dev->areaMap[area].type] = 0;
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

Result AreaManagement::deleteAreaContents(AreaPos area){
	if(area >= areasNo){
		PAFFS_DBG(PAFFS_TRACE_BUG, "Invalid area! "
				"Was %" PRIu32 ", should < %" PRIu32, area, areasNo);
		return Result::bug;
	}
	if(dev->areaMap[area].type == AreaType::retired){
		PAFFS_DBG(PAFFS_TRACE_BUG, "Tried deleting a retired area contents! %"
				PRIu32 " on %" PRIu32, area, dev->areaMap[area].position);
		return Result::bug;
	}
	if(area == dev->activeArea[AreaType::data] ||
		area == dev->activeArea[AreaType::index]){
		PAFFS_DBG(PAFFS_TRACE_BUG,
				"deleted content of active area %" PRIu32 ", is this OK?", area);
	}
	Result r = Result::ok;

	for(unsigned int i = 0; i < blocksPerArea; i++){
		r = dev->driver->eraseBlock(dev->areaMap[area].position*blocksPerArea + i);
		if(r != Result::ok){
			PAFFS_DBG_S(PAFFS_TRACE_GC, "Could not delete block n° %u (Area %u)!", dev->areaMap[area].position*blocksPerArea + i, area);
			dev->areaMgmt.retireArea(area);
			r = Result::badflash;
			break;
		}
	}
	dev->areaMap[area].erasecount++;
	if(dev->sumCache.isCached(area))
		dev->sumCache.resetASWritten(area);

	if(r == Result::badflash){
		PAFFS_DBG_S(PAFFS_TRACE_GC, "Could not delete block in area %u "
				"on position %u! Retired Area.", area, dev->areaMap[area].position);
		if(traceMask & (PAFFS_TRACE_AREA | PAFFS_TRACE_GC_DETAIL)){
			printf("Info: \n");
			for(unsigned int i = 0; i < areasNo; i++){
				printf("\tArea %d on %u as %10s with %3u erases\n", i,
						dev->areaMap[i].position, areaNames[dev->areaMap[i].type],
						dev->areaMap[i].erasecount);
			}
		}
	}
	PAFFS_DBG_S(PAFFS_TRACE_AREA, "Info: Deleted Area %u Contents at pos. %u.", area, dev->areaMap[area].position);
	dev->sumCache.deleteSummary(area);
	return r;
}

Result AreaManagement::deleteArea(AreaPos area){
	if(area >= areasNo){
		PAFFS_DBG(PAFFS_TRACE_BUG, "Invalid area! "
				"Was %" PRIu32 ", should < %" PRIu32, area, areasNo);
		return Result::bug;
	}
	if(dev->areaMap[area].type == AreaType::retired){
		PAFFS_DBG(PAFFS_TRACE_BUG, "Tried deleting a retired area! %"
				PRIu32 " on %" PRIu32, area, dev->areaMap[area].position);
		return Result::bug;
	}
	if(area == dev->activeArea[AreaType::data] ||
		area == dev->activeArea[AreaType::index]){
		PAFFS_DBG(PAFFS_TRACE_BUG,
				"deleted active area %" PRIu32 ", is this OK?", area);
	}

	Result r = deleteAreaContents(area);

	dev->areaMap[area].status = AreaStatus::empty;
	dev->areaMap[area].type = AreaType::unset;
	dev->usedAreas--;
	PAFFS_DBG_S(PAFFS_TRACE_AREA, "Info: FREED Area %u at pos. %u.", area, dev->areaMap[area].position);
	return r;
}

}  // namespace paffs