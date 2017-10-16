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
PageAbs getPageNumber(Addr addr, Device& dev){
	if(extractLogicalArea(addr) >= areasNo){
		PAFFS_DBG(PAFFS_TRACE_BUG, "Tried accessing area %" PRIu32 ", but we have only %" PRIu32,
				extractLogicalArea(addr), areasNo);
		return 0;
	}
	PageAbs page = dev.areaMgmt.getPos(extractLogicalArea(addr)) *
								totalPagesPerArea;
	page += extractPageOffs(addr);
	if(page > areasNo * totalPagesPerArea){
		PAFFS_DBG(PAFFS_TRACE_BUG, "calculated Page number out of range!");
		return 0;
	}
	return page;
}

//Returns the absolute page number from *direct* address
PageAbs getPageNumberFromDirect(Addr addr){
	PageAbs page = extractLogicalArea(addr) * totalPagesPerArea;
	page += extractPageOffs(addr);
	if(page > areasNo * totalPagesPerArea){
		PAFFS_DBG(PAFFS_TRACE_BUG, "calculated Page number out of range!");
		return 0;
	}
	return page;
}

//Returns the absolute page number from *logical* address
BlockAbs getBlockNumber(Addr addr, Device& dev){
	return dev.areaMgmt.getPos(extractLogicalArea(addr)) * blocksPerArea + extractPageOffs(addr) / pagesPerBlock;
}

//Returns the absolute page number from *direct* address
BlockAbs getBlockNumberFromDirect(Addr addr){
	return extractLogicalArea(addr) * blocksPerArea + extractPageOffs(addr) / pagesPerBlock;
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
unsigned int extractPageOffs(Addr addr){
	unsigned int page = 0;
	memcpy(&page, &addr, sizeof(PageOffs));
	return page;
}

AreaType   AreaManagement::getType(AreaPos area){
	if(area >= areasNo){
		PAFFS_DBG(PAFFS_TRACE_BUG, "Tried to get Type out of bounds! "
				"(%" PRIu32 " >= %" PRIu32 ")", area, areasNo);
		return AreaType::no;
	}
	return map[area].type;
}
AreaStatus AreaManagement::getStatus(AreaPos area){
	if(area >= areasNo){
		PAFFS_DBG(PAFFS_TRACE_BUG, "Tried to get Status out of bounds! "
				"(%" PRIu32 " >= %" PRIu32 ")", area, areasNo);
		return AreaStatus::active;
	}
	return map[area].status;
}
uint32_t   AreaManagement::getErasecount(AreaPos area){
	if(area >= areasNo){
		PAFFS_DBG(PAFFS_TRACE_BUG, "Tried to get Erasecount out of bounds! "
				"(%" PRIu32 " >= %" PRIu32 ")", area, areasNo);
		return 0;
	}
	return map[area].erasecount;
}
AreaPos    AreaManagement::getPos(AreaPos area){
	if(area >= areasNo){
		PAFFS_DBG(PAFFS_TRACE_BUG, "Tried to get Position out of bounds! "
				"(%" PRIu32 " >= %" PRIu32 ")", area, areasNo);
		return 0;
	}
	return map[area].position;
}

void AreaManagement::setType(AreaPos area, AreaType type){
	if(area >= areasNo){
		PAFFS_DBG(PAFFS_TRACE_BUG, "Tried to set AreaMap Type out of bounds! "
				"(%" PRIu32 " >= %" PRIu32 ")", area, areasNo);
		return;
	}
	map[area].type = type;
}
void AreaManagement::setStatus(AreaPos area, AreaStatus status){
	if(area >= areasNo){
		PAFFS_DBG(PAFFS_TRACE_BUG, "Tried to set AreaMap Status out of bounds! "
				"(%" PRIu32 " >= %" PRIu32 ")", area, areasNo);
		return;
	}
	map[area].status = status;
}
void AreaManagement::increaseErasecount(AreaPos area){
	if(area >= areasNo){
		PAFFS_DBG(PAFFS_TRACE_BUG, "Tried to increase AreaMap Erasecount out of bounds! "
				"(%" PRIu32 " >= %" PRIu32 ")", area, areasNo);
		return;
	}
	map[area].erasecount++;
}
void AreaManagement::setErasecount(AreaPos area, uint32_t erasecount){
	if(area >= areasNo){
		PAFFS_DBG(PAFFS_TRACE_BUG, "Tried to set AreaMap Erasecount out of bounds! "
				"(%" PRIu32 " >= %" PRIu32 ")", area, areasNo);
		return;
	}
	map[area].erasecount = erasecount;
}
void AreaManagement::setPos(AreaPos area, AreaPos pos){
	if(area >= areasNo){
		PAFFS_DBG(PAFFS_TRACE_BUG, "Tried to set AreaMap position out of bounds! "
				"(%" PRIu32 " >= %" PRIu32 ")", area, areasNo);
		return;
	}
	map[area].position = pos;
}

void AreaManagement::swapAreaPosition(AreaPos a, AreaPos b){
	if(a >= areasNo){
		PAFFS_DBG(PAFFS_TRACE_BUG, "Tried to swap AreaMap a out of bounds! "
				"(%" PRIu32 " >= %" PRIu32 ")", a, areasNo);
		return;
	}
	if(b >= areasNo){
		PAFFS_DBG(PAFFS_TRACE_BUG, "Tried to swap AreaMap b out of bounds! "
				"(%" PRIu32 " >= %" PRIu32 ")", b, areasNo);
		return;
	}
	AreaPos tmp1 = map[a].position;
	uint32_t tmp2 = map[a].erasecount;

	map[a].position = map[b].position;
	map[a].erasecount = map[b].erasecount;

	map[b].position = tmp1;
	map[b].erasecount = tmp2;
}
//Only for serializing areMap in Superblock
Area* AreaManagement::getMap(){
	return map;
}

unsigned int AreaManagement::findWritableArea(AreaType areaType){
	if(dev->activeArea[areaType] != 0){
		if(getStatus(dev->activeArea[areaType]) != AreaStatus::active){
			PAFFS_DBG(PAFFS_TRACE_BUG, "ActiveArea of %s not active "
					"(%s, %" PRIu32 " on %" PRIu32 ")",
					areaNames[areaType],
					areaStatusNames[getStatus(dev->activeArea[areaType])],
					dev->activeArea[areaType],
					getPos(dev->activeArea[areaType]));
		}
		//current Area has still space left
		if(getType(dev->activeArea[areaType]) != areaType){
			PAFFS_DBG(PAFFS_TRACE_BUG, "ActiveArea does not contain correct "
					"areaType! (Should %s, was %s)", areaNames[areaType],
					areaNames[getType(dev->activeArea[areaType])]);
		}
		return dev->activeArea[areaType];
	}

	if(dev->usedAreas < areasNo - minFreeAreas || areaType == AreaType::index){
		/**We only take new areas, if we dont hit the reserved pool.
		 * The exeption is Index area, which is needed for committing caches.
		 * If some day we support data cache, this would be allowed to use this pool as well.
		**/
		for(unsigned int area = 0; area < areasNo; area++){
			if(getStatus(area) ==AreaStatus::empty &&
					getType(area) != AreaType::retired){
				setType(area, areaType);
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

	if(getStatus(dev->activeArea[areaType]) > AreaStatus::empty){
		PAFFS_DBG(PAFFS_TRACE_BUG, "garbage Collection returned invalid Status! (was %d, should <%d)",getStatus(dev->activeArea[areaType]), AreaStatus::empty);
		dev->lasterr = Result::bug;
		return 0;
	}

	if(dev->activeArea[areaType] != 0){
		PAFFS_DBG_S(PAFFS_TRACE_AREA, "Found GC'ed Area %u for %s",
				dev->activeArea[areaType], areaNames[areaType]);
		if(getStatus(dev->activeArea[areaType]) != AreaStatus::active){
			PAFFS_DBG(PAFFS_TRACE_BUG, "An Active Area is not active after GC!"
					" (Area %" PRIu32 " on %" PRIu32 ")", dev->activeArea[areaType],
					getPos(dev->activeArea[areaType]));
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


//TODO: Add initAreaAs(...) to handle typical areaMgmt.setType(abc, def; initArea(...));
void AreaManagement::initArea(AreaPos area){
	if(getType(area) == AreaType::unset){
		PAFFS_DBG(PAFFS_TRACE_BUG, "Initing Area with invalid type!");
	}
	if(dev->activeArea[getType(area)] != 0 &&
			dev->activeArea[getType(area)] != area){
		PAFFS_DBG(PAFFS_TRACE_BUG, "Activating area %" PRIu32 " while different Area "
				"(%" PRIu32 " on %" PRIu32 ") still active!",
				area, dev->activeArea[getType(area)],
				getPos(dev->activeArea[getType(area)]));
	}
	PAFFS_DBG_S(PAFFS_TRACE_AREA, "Info: Init Area %u (pos %u) as %s.", static_cast<unsigned int>(area), static_cast<unsigned int>(getPos(area)), areaNames[getType(area)]);
	if(getStatus(area) ==AreaStatus::empty){
		dev->usedAreas++;
	}
	setStatus(area, AreaStatus::active);
}

Result AreaManagement::closeArea(AreaPos area){
	setStatus(area, AreaStatus::closed);
	dev->activeArea[getType(area)] = 0;
	PAFFS_DBG_S(PAFFS_TRACE_AREA, "Info: Closed %s Area %u at pos. %u.", areaNames[getType(area)], area, getPos(area));
	return Result::ok;
}

void AreaManagement::retireArea(AreaPos area){
	setStatus(area, AreaStatus::closed);
	setType(area, AreaType::retired);
	for(unsigned block = 0; block < blocksPerArea; block++)
			dev->driver.markBad(getPos(area) * blocksPerArea + block);
	PAFFS_DBG_S(PAFFS_TRACE_AREA, "Info: RETIRED Area %u at pos. %u.", area, getPos(area));
}

Result AreaManagement::deleteAreaContents(AreaPos area){
	if(area >= areasNo){
		PAFFS_DBG(PAFFS_TRACE_BUG, "Invalid area! "
				"Was %" PRIu32 ", should < %" PRIu32, area, areasNo);
		return Result::bug;
	}
	if(getType(area) == AreaType::retired){
		PAFFS_DBG(PAFFS_TRACE_BUG, "Tried deleting a retired area contents! %"
				PRIu32 " on %" PRIu32, area, getPos(area));
		return Result::bug;
	}
	if(area == dev->activeArea[AreaType::data] ||
		area == dev->activeArea[AreaType::index]){
		PAFFS_DBG(PAFFS_TRACE_BUG,
				"deleted content of active area %" PRIu32 ", is this OK?", area);
	}
	Result r = Result::ok;

	for(unsigned int i = 0; i < blocksPerArea; i++){
		r = dev->driver.eraseBlock(getPos(area)*blocksPerArea + i);
		if(r != Result::ok){
			PAFFS_DBG_S(PAFFS_TRACE_GC, "Could not delete block n° %u (Area %u)!", getPos(area)*blocksPerArea + i, area);
			retireArea(area);
			r = Result::badflash;
			break;
		}
	}
	increaseErasecount(area);
	if(dev->sumCache.isCached(area))
		dev->sumCache.resetASWritten(area);

	if(r == Result::badflash){
		PAFFS_DBG_S(PAFFS_TRACE_GC, "Could not delete block in area %u "
				"on position %u! Retired Area.", area, getPos(area));
		if(traceMask & (PAFFS_TRACE_AREA | PAFFS_TRACE_GC_DETAIL)){
			printf("Info: \n");
			for(unsigned int i = 0; i < areasNo; i++){
				printf("\tArea %d on %u as %10s with %3u erases\n", i,
						getPos(i), areaNames[getType(i)],
						getErasecount(i));
			}
		}
	}
	PAFFS_DBG_S(PAFFS_TRACE_AREA, "Info: Deleted Area %u Contents at pos. %u.", area, getPos(area));
	dev->sumCache.deleteSummary(area);
	return r;
}

Result AreaManagement::deleteArea(AreaPos area){
	if(area >= areasNo){
		PAFFS_DBG(PAFFS_TRACE_BUG, "Invalid area! "
				"Was %" PRIu32 ", should < %" PRIu32, area, areasNo);
		return Result::bug;
	}
	if(getType(area) == AreaType::retired){
		PAFFS_DBG(PAFFS_TRACE_BUG, "Tried deleting a retired area! %"
				PRIu32 " on %" PRIu32, area, getPos(area));
		return Result::bug;
	}
	if(area == dev->activeArea[AreaType::data] ||
		area == dev->activeArea[AreaType::index]){
		PAFFS_DBG(PAFFS_TRACE_BUG,
				"deleted active area %" PRIu32 ", is this OK?", area);
	}

	Result r = deleteAreaContents(area);

	setStatus(area, AreaStatus::empty);
	setType(area, AreaType::unset);
	dev->usedAreas--;
	PAFFS_DBG_S(PAFFS_TRACE_AREA, "Info: FREED Area %u at pos. %u.", area, getPos(area));
	return r;
}

}  // namespace paffs
