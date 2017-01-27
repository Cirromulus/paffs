/*
 * area.cpp
 *
 *  Created on: 27 Jan 2017
 *      Author: rooot
 */
#include "area.hpp"
#include "garbage_collection.hpp"
#include "driver/driver.hpp"
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


unsigned int findWritableArea(AreaType areaType, Dev* dev){
	if(dev->activeArea[areaType] != 0 && dev->areaMap[dev->activeArea[areaType]].status != AreaStatus::closed){
		//current Area has still space left
		return dev->activeArea[areaType];
	}

	for(unsigned int area = 0; area < dev->param->areas_no; area++){
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

	if(dev->activeArea[areaType] != 0 && dev->areaMap[dev->activeArea[areaType]].status != AreaStatus::closed){
		return dev->activeArea[areaType];
	}

	//If we arrive here, something buggy must have happened
	PAFFS_DBG(PAFFS_TRACE_BUG, "Garbagecollection pointed to invalid area!");
	lasterr = Result::bug;
	return 0;
}

Result findFirstFreePage(unsigned int* p_out, Dev* dev, unsigned int area){

	for(unsigned int i = 0; i < dev->param->data_pages_per_area; i++){
		if(dev->areaMap[area].areaSummary[i] == SummaryEntry::free){
			*p_out = i;
			return Result::ok;
		}
	}
	return Result::nosp;
}

uint64_t getPageNumber(Addr addr, Dev *dev){
	uint64_t page = dev->areaMap[extractLogicalArea(addr)].position *
								dev->param->total_pages_per_area;
	page += extractPage(addr);
	if(page > dev->param->areas_no * dev->param->total_pages_per_area){
		PAFFS_DBG(PAFFS_TRACE_BUG, "calculated Page number out of range!");
		return 0;
	}
	return page;
}

Result manageActiveAreaFull(Dev *dev, AreaPos *area, AreaType areaType){
	if(dev->areaMap[*area].areaSummary == NULL){
		PAFFS_DBG(PAFFS_TRACE_BUG, "Tried to access invalid areaSummary!");
		return Result::bug;
	}

	if(trace_mask & PAFFS_TRACE_VERIFY_AS){
		for(unsigned int i = 0; i < dev->param->data_pages_per_area; i++){
			if(dev->areaMap[*area].areaSummary[i] > SummaryEntry::dirty)
				PAFFS_DBG(PAFFS_TRACE_BUG, "Summary of %u contains invalid Entries!", *area);
		}
	}

	bool isFull = true;
	for(unsigned int i = 0; i < dev->param->data_pages_per_area; i++){
		if(dev->areaMap[*area].areaSummary[i] == SummaryEntry::free) {
			isFull = false;
			break;
		}
	}

	if(isFull){
		PAFFS_DBG_S(PAFFS_TRACE_AREA, "Info: Area %u (Type %s) full.", *area, area_names[areaType]);
		//Current Area is full!
		closeArea(dev, *area);
	}

	return Result::ok;
}


//TODO: Add initAreaAs(...) to handle typical areaMap[abc].type = def; initArea(...);
void initArea(Dev* dev, AreaPos area){
	PAFFS_DBG_S(PAFFS_TRACE_AREA, "Info: Init Area %u (pos %u) as %s.", (unsigned int)area, (unsigned int)dev->areaMap[area].position, area_names[dev->areaMap[area].type]);
	//generate the areaSummary in Memory
	dev->areaMap[area].status = AreaStatus::active;
	dev->areaMap[area].isAreaSummaryDirty = false;
	if(dev->areaMap[area].type == AreaType::indexarea || dev->areaMap[area].type == AreaType::dataarea){
		if(dev->areaMap[area].areaSummary == NULL){
			dev->areaMap[area].areaSummary = (SummaryEntry*) malloc(
					sizeof(SummaryEntry)
					* dev->param->blocks_per_area
					* dev->param->pages_per_block);
		}
		memset(dev->areaMap[area].areaSummary, 0,
				sizeof(SummaryEntry)
				* dev->param->blocks_per_area
				* dev->param->pages_per_block);
		dev->areaMap[area].has_areaSummary = true;
	}else{
		if(dev->areaMap[area].areaSummary != NULL){
			//Former areatype had summary
			free(dev->areaMap[area].areaSummary);
			dev->areaMap[area].areaSummary = NULL;
		}
		dev->areaMap[area].has_areaSummary = false;
	}
}

Result loadArea(Dev *dev, AreaPos area){
	PAFFS_DBG_S(PAFFS_TRACE_AREA, "Info: Loading Areasummary of Area %u (pos %u) as %s.", area, dev->areaMap[area].position, area_names[dev->areaMap[area].type]);
	if(dev->areaMap[area].type != AreaType::dataarea && dev->areaMap[area].type != AreaType::indexarea){
		return Result::ok;
	}
	if(dev->areaMap[area].areaSummary != NULL){
		PAFFS_DBG(PAFFS_TRACE_BUG, "Tried to load Area with existing areaSummary!");
		return Result::bug;
	}

	if(dev->areaMap[area].isAreaSummaryDirty == true){
		PAFFS_DBG(PAFFS_TRACE_BUG, "Tried to load Area without areaSummary but dirty flag!");
		return Result::bug;
	}

	dev->areaMap[area].isAreaSummaryDirty = true;
	dev->areaMap[area].areaSummary = (SummaryEntry*) malloc(
			sizeof(SummaryEntry)
			* dev->param->blocks_per_area
			* dev->param->pages_per_block);

	return readAreasummary(dev, area, dev->areaMap[area].areaSummary, true);
}

Result closeArea(Dev *dev, AreaPos area){
	dev->areaMap[area].status = AreaStatus::closed;

	if(trace_mask & PAFFS_TRACE_VERIFY_AS){
		for(unsigned int i = 0; i < dev->param->data_pages_per_area; i++){
			if(dev->areaMap[area].areaSummary[i] > SummaryEntry::dirty)
				PAFFS_DBG(PAFFS_TRACE_BUG, "Summary of %u contains invalid Entries!", area);
		}
	}

	//TODO: Suspend areaSummary write until RAM cache runs low
	if(dev->areaMap[area].type == AreaType::dataarea || dev->areaMap[area].type == AreaType::indexarea){
		Result r = writeAreasummary(dev, area, dev->areaMap[area].areaSummary);
		if(r != Result::ok)
			return r;
	}
	//TODO: delete all area summaries if low on RAM
	if(dev->areaMap[area].type != AreaType::dataarea && dev->areaMap[area].type != AreaType::indexarea){
		free(dev->areaMap[area].areaSummary);
		dev->areaMap[area].areaSummary = NULL;
	}

	PAFFS_DBG_S(PAFFS_TRACE_AREA, "Info: Closed %s Area %u at pos. %u.", area_names[dev->areaMap[area].type], area, dev->areaMap[area].position);
	return Result::ok;
}

void retireArea(Dev *dev, AreaPos area){
	dev->areaMap[area].status = AreaStatus::closed;

	if((dev->areaMap[area].type == AreaType::dataarea || dev->areaMap[area].type == AreaType::indexarea) && trace_mask & PAFFS_TRACE_VERIFY_AS){
		for(unsigned int i = 0; i < dev->param->data_pages_per_area; i++){
			if(dev->areaMap[area].areaSummary[i] > SummaryEntry::dirty)
				PAFFS_DBG(PAFFS_TRACE_BUG, "Summary of %u contains invalid Entries!", area);
		}
	}

	//TODO: delete all area summaries if low on RAM
	if(dev->areaMap[area].type != AreaType::dataarea && dev->areaMap[area].type != AreaType::indexarea){
		free(dev->areaMap[area].areaSummary);
		dev->areaMap[area].areaSummary = NULL;
	}

	dev->areaMap[area].type = AreaType::retired;

	PAFFS_DBG_S(PAFFS_TRACE_AREA, "Info: RETIRED Area %u at pos. %u.", area, dev->areaMap[area].position);
}

Result writeAreasummary(Dev *dev, AreaPos area, SummaryEntry* summary){
	unsigned int needed_bytes = 1 + dev->param->data_pages_per_area / 8;
	unsigned int needed_pages = 1 + needed_bytes / dev->param->data_bytes_per_page;
	if(needed_pages != dev->param->total_pages_per_area - dev->param->data_pages_per_area){
		PAFFS_DBG(PAFFS_TRACE_ERROR, "AreaSummary size differs with formatting infos!");
		return Result::fail;
	}

	char buf[needed_bytes];
	memset(buf, 0, needed_bytes);

	/*Is it really necessary to save 16 bit while slowing down garbage collection?
	 *TODO: Check how cost reduction scales with bigger flashes.
	 *		AreaSummary is without optimization 2 bit per page. 2 Kib per Page would
	 *		allow roughly 1000 pages per Area. Usually big pages come with big Blocks,
	 *		so a Block would be ~500 pages, so an area would be limited to two Blocks.
	 *		Not good.
	 *
	 *		Thought 2: GC just cares if dirty or not. Areasummary says exactly that.
	 *		Win.
	 */
	for(unsigned int j = 0; j < dev->param->data_pages_per_area; j++){
		if(summary[j] != SummaryEntry::dirty)
			buf[j/8] |= 1 << j%8;
	}

	uint32_t pointer = 0;
	uint64_t page_offs = getPageNumber(combineAddress(area, dev->param->data_pages_per_area), dev);
	Result r;
	for(unsigned int page = 0; page < needed_pages; page++){
		unsigned int btw = pointer + dev->param->data_bytes_per_page < needed_bytes ? dev->param->data_bytes_per_page
							: needed_bytes - pointer;
		r = dev->driver->writePage(page_offs + page, &buf[pointer], btw);
		if(r != Result::ok)
			return r;

		pointer += btw;
	}
	return Result::ok;
}

//FIXME: readAreasummary is untested, b/c areaSummaries remain in RAM during unmount
Result readAreasummary(Dev *dev, AreaPos area, SummaryEntry* out_summary, bool complete){
	unsigned int needed_bytes = 1 + dev->param->data_pages_per_area / 8 /* One bit per entry*/;

	unsigned int needed_pages = 1 + needed_bytes / dev->param->data_bytes_per_page;
	if(needed_pages != dev->param->total_pages_per_area - dev->param->data_pages_per_area){
		PAFFS_DBG(PAFFS_TRACE_ERROR, "AreaSummary size differs with formatting infos!");
		return Result::fail;
	}

	char buf[needed_bytes];
	memset(buf, 0, needed_bytes);
	uint32_t pointer = 0;
	uint64_t page_offs = getPageNumber(combineAddress(area, dev->param->data_pages_per_area), dev);
	Result r;
	for(unsigned int page = 0; page < needed_pages; page++){
		unsigned int btr = pointer + dev->param->data_bytes_per_page < needed_bytes ? dev->param->data_bytes_per_page
							: needed_bytes - pointer;
		r = dev->driver->readPage(page_offs + page, &buf[pointer], btr);
		if(r != Result::ok)
			return r;

		pointer += btr;
	}
	//buffer ready
	PAFFS_DBG_S(PAFFS_TRACE_SUPERBLOCK, "SuperIndex Buffer was filled with %u Bytes.", pointer);


	for(unsigned int j = 0; j < dev->param->data_pages_per_area; j++){
		if(buf[j/8] & 1 << j%8){
			if(complete){
				unsigned char pagebuf[BYTES_PER_PAGE];
				Addr tmp = combineAddress(area, j);
				r = dev->driver->readPage(getPageNumber(tmp, dev), pagebuf, dev->param->data_bytes_per_page);
				if(r != Result::ok)
					return r;
				bool contains_data = false;
				for(unsigned int byte = 0; byte < dev->param->data_bytes_per_page; byte++){
					if(pagebuf[byte] != 0xFF){
						contains_data = true;
						break;
					}
				}
				if(contains_data)
					out_summary[j] = SummaryEntry::used;
				else
					out_summary[j] = SummaryEntry::free;
			}else{
				//This is just a guess b/c we are in incomplete mode.
				out_summary[j] = SummaryEntry::used;
			}
		}else{
			out_summary[j] = SummaryEntry::dirty;
		}
	}

	return Result::ok;
}

}  // namespace paffs
