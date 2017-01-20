/*
 * paffs_flash.c
 *
 *  Created on: 30.06.2016
 *      Author: rooot
 */

#include "paffs_flash.hpp"
#include "driver/driver.hpp"
#include "garbage_collection.hpp"
#include <stdlib.h>
#include <string.h>

namespace paffs{

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

Addr combineAddress(uint32_t logical_area, uint32_t page){
	Addr addr = 0;
	memcpy(&addr, &logical_area, sizeof(uint32_t));
	memcpy(&((char*)&addr)[sizeof(uint32_t)], &page, sizeof(uint32_t));

	return addr;
}

unsigned int extractLogicalArea(Addr addr){
	unsigned int area = 0;
	memcpy(&area, &addr, sizeof(uint32_t));
	return area;
}
unsigned int extractPage(Addr addr){
	unsigned int page = 0;
	memcpy(&page, &((char*)&addr)[sizeof(uint32_t)], sizeof(uint32_t));
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
	PAFFS_DBG_S(PAFFS_TRACE_WRITE, "SuperIndex Buffer was filled with %u Bytes.", pointer);


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

//modifies inode->size and inode->reserved size as well
Result writeInodeData(Inode* inode,
					unsigned int offs, unsigned int bytes, unsigned int *bytes_written,
					const char* data, Dev* dev){

	if(offs+bytes == 0){
		PAFFS_DBG(PAFFS_TRACE_ERROR, "Write size 0! Bug?");
		return lasterr = Result::einval;
	}

	unsigned int pageFrom = offs/dev->param->data_bytes_per_page;
	unsigned int pageTo = (offs + bytes - 1) / dev->param->data_bytes_per_page;

	if(pageTo - pageFrom > 11){
		//Would use first indirection Layer
		PAFFS_DBG(PAFFS_TRACE_ALWAYS, "Write would use first indirection layer, too big!");
		return lasterr = Result::nimpl;
	}

	unsigned int pageOffs = offs % dev->param->data_bytes_per_page;
	*bytes_written = 0;

	for(unsigned int page = 0; page <= pageTo - pageFrom; page++){
		bool misaligned = false;
		dev->activeArea[AreaType::dataarea] = findWritableArea(AreaType::dataarea, dev);
		if(lasterr != Result::ok){
			return lasterr;
		}

		//Handle Areas
		if(dev->areaMap[dev->activeArea[AreaType::dataarea]].status == AreaStatus::empty){
			//We'll have to use a fresh area,
			//so generate the areaSummary in Memory
			initArea(dev, dev->activeArea[AreaType::dataarea]);
		}
		unsigned int firstFreePage = 0;
		if(findFirstFreePage(&firstFreePage, dev, dev->activeArea[AreaType::dataarea]) == Result::nosp){
			PAFFS_DBG(PAFFS_TRACE_BUG, "BUG: findWritableArea returned full area (%d).", dev->activeArea[AreaType::dataarea]);
			return Result::bug;
		}
		Addr pageAddress = combineAddress(dev->activeArea[AreaType::dataarea], firstFreePage);

		dev->areaMap[dev->activeArea[AreaType::dataarea]].areaSummary[firstFreePage] = SummaryEntry::used;

		//Prepare buffer and calculate bytes to write
		char* buf = &((char*)data)[page*dev->param->data_bytes_per_page];
		unsigned int btw = bytes - *bytes_written;
		if((bytes+pageOffs) > dev->param->data_bytes_per_page){
			btw = (bytes+pageOffs) > (page+1)*dev->param->data_bytes_per_page ?
						dev->param->data_bytes_per_page - pageOffs :
						bytes - page*dev->param->data_bytes_per_page;
		}



		if(inode->direct[page+pageFrom] != 0){
			//We are overriding existing data
			//mark old Page in Areamap
			unsigned long oldArea = extractLogicalArea(inode->direct[page+pageFrom]);
			unsigned long oldPage = extractPage(inode->direct[page+pageFrom]);


			if((btw + pageOffs < dev->param->data_bytes_per_page &&
				page*dev->param->data_bytes_per_page + btw < inode->size) ||  //End Misaligned
				(pageOffs > 0 && page == 0)){				//Start Misaligned

				//fill write buffer with valid Data
				misaligned = true;
				buf = (char*)malloc(dev->param->data_bytes_per_page);
				memset(buf, 0xFF, dev->param->data_bytes_per_page);

				unsigned int btr = dev->param->data_bytes_per_page;

				if((pageFrom+1+page)*dev->param->data_bytes_per_page > inode->size){
					btr = inode->size - (pageFrom+page) * dev->param->data_bytes_per_page;
				}

				unsigned int bytes_read = 0;
				Result r = readInodeData(inode, (pageFrom+page)*dev->param->data_bytes_per_page, btr, &bytes_read, buf, dev);
				if(r != Result::ok || bytes_read != btr){
					free(buf);
					return Result::bug;
				}

				//Handle pageOffset
				memcpy(&buf[pageOffs], &data[*bytes_written], btw);

				//this is here, because btw will be modified
				*bytes_written += btw;

				//increase btw to whole page to write existing data back
				btw = btr > (pageOffs + btw) ? btr : pageOffs + btw;

				//pageoffset is only at applied to first page
				pageOffs = 0;
			}else{
				//not misaligned
				*bytes_written += btw;
			}

			//Mark old pages dirty
			if(dev->areaMap[oldArea].areaSummary == NULL){
				Result r = loadArea(dev, oldArea);
				if(r != Result::ok){
					PAFFS_DBG(PAFFS_TRACE_ERROR, "Could not read A/S from CLOSED area!");
					return r;
				}
			}
			dev->areaMap[oldArea].areaSummary[oldPage] = SummaryEntry::dirty;
		}else{
			//we are writing to a new page
			*bytes_written += btw;
			inode->reservedSize += dev->param->data_bytes_per_page;
		}
		inode->direct[page+pageFrom] = pageAddress;

		Result res = dev->driver->writePage(getPageNumber(pageAddress, dev), buf, btw);

		if(misaligned)
			free(buf);

		PAFFS_DBG_S(PAFFS_TRACE_WRITE, "write r.P: %d/%d, phy.P: %llu", page+1, pageTo+1, (long long unsigned int) getPageNumber(pageAddress, dev));
		if(res != Result::ok){
			PAFFS_DBG(PAFFS_TRACE_ERROR, "ERR: write returned FAIL at phy.P: %llu", (long long unsigned int) getPageNumber(pageAddress, dev));
			return Result::fail;
		}

		res = manageActiveAreaFull(dev, &dev->activeArea[AreaType::dataarea], AreaType::dataarea);
		if(res != Result::ok)
			return res;

	}

	if(inode->size < *bytes_written + offs)
		inode->size = *bytes_written + offs;

	return updateExistingInode(dev, inode);
}
Result readInodeData(Inode* inode,
					unsigned int offs, unsigned int bytes, unsigned int *bytes_read,
					char* data, Dev* dev){

	if(offs+bytes == 0){
		PAFFS_DBG(PAFFS_TRACE_ERROR, "Read size 0! Bug?");
		return lasterr = Result::einval;
	}

	*bytes_read = 0;
	unsigned int pageFrom = offs/dev->param->data_bytes_per_page;
	unsigned int pageTo = (offs + bytes - 1) / dev->param->data_bytes_per_page;
	unsigned int pageOffs = offs % dev->param->data_bytes_per_page;


	if(offs + bytes > inode->size){
		PAFFS_DBG(PAFFS_TRACE_ERROR, "Read bigger than size of object! (was: %d, max: %lu)", offs+bytes, (long unsigned) inode->size);
		//TODO: return less bytes_read
		return Result::nimpl;
	}

	if(pageTo > 11){
		//todo Read indirection Layers
		return Result::nimpl;
	}

	char* wrap = data;
	bool misaligned = false;
	if(pageOffs > 0){
		misaligned = true;
		wrap = (char*) malloc(bytes + offs);
	}

	for(unsigned int page = 0; page <= pageTo - pageFrom; page++){
		char* buf = &wrap[page*dev->param->data_bytes_per_page];

		unsigned int btr = bytes + pageOffs - *bytes_read;
		if(btr > dev->param->data_bytes_per_page){
			btr = (bytes + pageOffs) > (page+1)*dev->param->data_bytes_per_page ?
						dev->param->data_bytes_per_page :
						(bytes + pageOffs) - page*dev->param->data_bytes_per_page;
		}

		AreaPos area = extractLogicalArea(inode->direct[page + pageFrom]);
		if(dev->areaMap[area].type != AreaType::dataarea){
			PAFFS_DBG(PAFFS_TRACE_BUG, "READ INODE operation of invalid area at %d:%d", extractLogicalArea(inode->direct[page + pageFrom]),extractPage(inode->direct[page + pageFrom]));
			return Result::bug;
		}

		Result r = Result::ok;
		if(dev->areaMap[area].areaSummary == NULL){
			//TODO: This is very expensive. Either build switch "safety mode" that loads complete A/S
			//		Or just load (everytime) incomplete A/S
			r = loadArea(dev, area);
			if(r != Result::ok){
				PAFFS_DBG(PAFFS_TRACE_ERROR, "Could not load AreaSummary for safetycheck!");
			}
		}

		if(r == Result::ok){
			if(dev->areaMap[extractLogicalArea(inode->direct[page + pageFrom])].areaSummary[extractPage(inode->direct[page + pageFrom])] == SummaryEntry::dirty){
				PAFFS_DBG(PAFFS_TRACE_BUG, "READ INODE operation of outdated (dirty) data at %d:%d", extractLogicalArea(inode->direct[page + pageFrom]),extractPage(inode->direct[page + pageFrom]));
				return Result::bug;
			}

			if(dev->areaMap[extractLogicalArea(inode->direct[page + pageFrom])].areaSummary[extractPage(inode->direct[page + pageFrom])] == SummaryEntry::free){
				PAFFS_DBG(PAFFS_TRACE_BUG, "READ INODE operation of invalid (SummaryEntry::free) data at %d:%d", extractLogicalArea(inode->direct[page + pageFrom]),extractPage(inode->direct[page + pageFrom]));
				return Result::bug;
			}
		}

		unsigned long long addr = getPageNumber(inode->direct[page + pageFrom], dev);
		r = dev->driver->readPage(addr, buf, btr);
		if(r != Result::ok){
			if(misaligned)
				free (wrap);
			return lasterr = r;
		}
		*bytes_read += btr;

	}

	if(misaligned) {
		memcpy(data, &wrap[pageOffs], bytes);
		*bytes_read -= pageOffs;
		free (wrap);
	}

	return Result::ok;
}


//inode->size and inode->reservedSize is altered.
Result deleteInodeData(Inode* inode, Dev* dev, unsigned int offs){
	//TODO: This calculation contains errors in border cases
	unsigned int pageFrom = offs/dev->param->data_bytes_per_page;
	unsigned int pageTo = (inode->size - 1) / dev->param->data_bytes_per_page;

	if(inode->size < offs){
		//Offset bigger than actual filesize
		return Result::einval;
	}

	if(pageTo > 11){
		//todo Read indirection Layers
		return Result::nimpl;
	}


	inode->size = offs;

	if(inode->reservedSize == 0)
		return Result::ok;

	if(inode->size >= inode->reservedSize - dev->param->data_bytes_per_page)
		//doesn't leave a whole page blank
		return Result::ok;


	for(unsigned int page = 0; page <= pageTo - pageFrom; page++){

		unsigned int area = extractLogicalArea(inode->direct[page + pageFrom]);
		unsigned int relPage = extractPage(inode->direct[page + pageFrom]);

		if(dev->areaMap[area].type != AreaType::dataarea){
			PAFFS_DBG(PAFFS_TRACE_BUG, "DELETE INODE operation of invalid area at %d:%d", extractLogicalArea(inode->direct[page + pageFrom]),extractPage(inode->direct[page + pageFrom]));
			return Result::bug;
		}

		if(dev->areaMap[area].areaSummary[relPage] == SummaryEntry::dirty){
			PAFFS_DBG(PAFFS_TRACE_BUG, "DELETE INODE operation of outdated (dirty) data at %d:%d", extractLogicalArea(inode->direct[page + pageFrom]),extractPage(inode->direct[page + pageFrom]));
			return Result::bug;
		}

		//Mark old pages dirty
		dev->areaMap[area].areaSummary[relPage] = SummaryEntry::dirty;

		inode->reservedSize -= dev->param->data_bytes_per_page;
		inode->direct[page+pageFrom] = 0;

	}

	return Result::ok;
}

//Does not change addresses in parent Nodes
Result writeTreeNode(Dev* dev, TreeNode* node){
	if(node == NULL){
		PAFFS_DBG(PAFFS_TRACE_BUG, "BUG: TreeNode NULL");
				return Result::bug;
	}
	if(sizeof(TreeNode) > dev->param->data_bytes_per_page){
		PAFFS_DBG(PAFFS_TRACE_BUG, "BUG: TreeNode bigger than Page (Was %u, should %u)", sizeof(TreeNode), dev->param->data_bytes_per_page);
		return Result::bug;
	}

	if(node->self != 0){
		//We have to invalidate former position first
		dev->areaMap[extractLogicalArea(node->self)].areaSummary[extractPage(node->self)] = SummaryEntry::dirty;
	}

	lasterr = Result::ok;
	dev->activeArea[AreaType::indexarea] = findWritableArea(AreaType::indexarea, dev);
	if(lasterr != Result::ok){
		return lasterr;
	}

	if(dev->activeArea[AreaType::indexarea] == 0){
		PAFFS_DBG(PAFFS_TRACE_BUG, "WRITE TREE NODE findWritableArea returned 0");
		return Result::bug;
	}

	unsigned int firstFreePage = 0;
	if(findFirstFreePage(&firstFreePage, dev, dev->activeArea[AreaType::indexarea]) == Result::nosp){
		PAFFS_DBG(PAFFS_TRACE_BUG, "BUG: findWritableArea returned full area (%d).", dev->activeArea[AreaType::indexarea]);
		return lasterr = Result::bug;
	}
	Addr addr = combineAddress(dev->activeArea[AreaType::indexarea], firstFreePage);
	node->self = addr;

	dev->areaMap[dev->activeArea[AreaType::indexarea]].areaSummary[firstFreePage] = SummaryEntry::used;

	Result r = dev->driver->writePage(getPageNumber(node->self, dev), node, sizeof(TreeNode));
	if(r != Result::ok)
		return r;

	r = manageActiveAreaFull(dev, &dev->activeArea[AreaType::indexarea], AreaType::indexarea);
	if(r != Result::ok)
		return r;

	return Result::ok;
}

Result readTreeNode(Dev* dev, Addr addr, TreeNode* node){
	if(node == NULL){
		PAFFS_DBG(PAFFS_TRACE_BUG, "BUG: TreeNode NULL");
		return Result::bug;
	}
	if(sizeof(TreeNode) > dev->param->data_bytes_per_page){
		PAFFS_DBG(PAFFS_TRACE_BUG, "BUG: TreeNode bigger than Page (Was %u, should %u)", sizeof(TreeNode), dev->param->data_bytes_per_page);
		return Result::bug;
	}

	if(dev->areaMap[extractLogicalArea(addr)].type != AreaType::indexarea){
		PAFFS_DBG(PAFFS_TRACE_ERROR, "READ TREEENODE operation on %s!", area_names[dev->areaMap[extractLogicalArea(addr)].type]);
		return Result::bug;
	}

	if(dev->areaMap[extractLogicalArea(addr)].areaSummary == 0){
		PAFFS_DBG_S(PAFFS_TRACE_SCAN, "READ operation on indexarea without areaSummary!");
		//TODO: Could be safer if areaSummary would be read from flash for safety
	}else{
		if(dev->areaMap[extractLogicalArea(addr)].areaSummary[extractPage(addr)] == SummaryEntry::dirty){
			PAFFS_DBG(PAFFS_TRACE_ERROR, "READ operation of obsoleted data at %X:%X", extractLogicalArea(addr), extractPage(addr));
			return Result::bug;
		}

		if(extractLogicalArea(addr) == 0){
			PAFFS_DBG(PAFFS_TRACE_ERROR, "READ TREE NODE operation on (log.) first Area at %X:%X", extractLogicalArea(addr), extractPage(addr));
			return Result::bug;
		}
	}

	Result r = dev->driver->readPage(getPageNumber(addr, dev), node, sizeof(TreeNode));
	if(r != Result::ok)
		return r;

	if(node->self != addr){
		PAFFS_DBG(PAFFS_TRACE_BUG, "Read Treenode at %X:%X, but its content stated that it was on %X:%X", extractLogicalArea(addr), extractPage(addr), extractLogicalArea(node->self), extractPage(node->self));
		return Result::bug;
	}

	return Result::ok;
}

Result deleteTreeNode(Dev* dev, TreeNode* node){
	dev->areaMap[extractLogicalArea(node->self)].areaSummary[extractPage(node->self)] = SummaryEntry::dirty;
	return Result::ok;
}


// Superblock related

Result findFirstFreeEntryInBlock(Dev* dev, uint32_t area, uint8_t block, uint32_t* out_pos, unsigned int required_pages){
	unsigned int in_a_row = 0;
	uint64_t page_offs = dev->param->pages_per_block * block;
	for(unsigned int i = 0; i < dev->param->pages_per_block; i++) {
		Addr addr = combineAddress(area, i + page_offs);
		uint32_t no;
		Result r = dev->driver->readPage(getPageNumber(addr, dev), &no, sizeof(uint32_t));
		if(r != Result::ok)
			return r;
		if(no != 0xFFFFFFFF){
			if(in_a_row != 0){
				*out_pos = 0;
				in_a_row = 0;
			}
			continue;
		}

		// Unprogrammed, therefore empty
		*out_pos = i;
		if(++in_a_row == required_pages)
			return Result::ok;
	}
	return Result::nf;
}

Result findMostRecentEntryInBlock(Dev* dev, uint32_t area, uint8_t block, uint32_t* out_pos, uint32_t* out_index){
	uint32_t* maximum = out_index;
	*maximum = 0;
	*out_pos = 0;
	uint32_t page_offs = dev->param->pages_per_block * block;
	for(unsigned int i = 0; i < dev->param->pages_per_block; i++) {
		Addr addr = combineAddress(area, i + page_offs);
		uint32_t no;
		Result r = dev->driver->readPage(getPageNumber(addr, dev), &no, sizeof(uint32_t));
		if(r != Result::ok)
			return r;
		if(no == 0xFFFFFFFF){
			// Unprogrammed, therefore empty
			if(*maximum != 0)
				return Result::ok;
			return Result::nf;
		}

		if(no > *maximum){
			*out_pos = i + page_offs;
			*maximum = no;
		}
	}

	return Result::ok;
}


Result writeAnchorEntry(Dev* dev, Addr addr, AnchorEntry* entry){
	//Currently not implemented to simplify Find-Strategy
	return Result::nimpl;
}
Result readAnchorEntry(Dev* dev, Addr addr, AnchorEntry* entry){
	//Currently not implemented to simplify Find-Strategy
	return Result::nimpl;
}

Result deleteAnchorBlock(Dev* dev, uint32_t area, uint8_t block) {
	if(dev->areaMap[area].type != AreaType::superblockarea){
		PAFFS_DBG(PAFFS_TRACE_BUG, "Tried to delete Block outside of SUPARBLCOKAREA");
		return Result::bug;
	}
	uint32_t block_offs = dev->areaMap[area].position * dev->param->blocks_per_area;
	return dev->driver->eraseBlock(block_offs + block);
}

Result writeJumpPadEntry(Dev* dev, Addr addr, JumpPadEntry* entry){
	//Currently not implemented to simplify Find-Strategy
	return Result::nimpl;
}

Result readJumpPadEntry(Dev* dev, Addr addr, JumpPadEntry* entry){
	//Currently not implemented to simplify Find-Strategy
	return Result::nimpl;
}


//Make sure that free space is sufficient!
Result writeSuperIndex(Dev* dev, Addr addr, superIndex* entry){
	if(dev->areaMap[extractLogicalArea(addr)].type != AreaType::superblockarea){
		PAFFS_DBG(PAFFS_TRACE_BUG, "Tried to write superIndex outside of superblock Area");
		return Result::bug;
	}

	unsigned int needed_bytes = sizeof(uint32_t) + sizeof(Addr) +
		dev->param->areas_no * (sizeof(Area) - sizeof(SummaryEntry*))+ // AreaMap without summaryEntry pointer
		2 * dev->param->data_pages_per_area / 8 /* One bit per entry, two entrys for INDEX and DATA section*/;

	unsigned int needed_pages = needed_bytes / BYTES_PER_PAGE + 1;

	unsigned int pointer = 0;
	char buf[needed_bytes];
	memset(buf, 0, needed_bytes);
	memcpy(buf, &entry->no, sizeof(uint32_t));
	pointer += sizeof(uint32_t);
	memcpy(&buf[pointer], &entry->rootNode, sizeof(Addr));
	pointer += sizeof(Addr);
	long areaSummaryPositions[2];
	areaSummaryPositions[0] = -1;
	areaSummaryPositions[1] = -1;
	unsigned char pospos = 0;	//Stupid name

	for(unsigned int i = 0; i < dev->param->areas_no; i++){
		if((entry->areaMap[i].type == AreaType::indexarea || entry->areaMap[i].type == AreaType::dataarea) && entry->areaMap[i].status == AreaStatus::active){
			areaSummaryPositions[pospos++] = i;
			entry->areaMap[i].has_areaSummary = true;
		}else{
			entry->areaMap[i].has_areaSummary = false;
		}

		memcpy(&buf[pointer], &entry->areaMap[i], sizeof(Area) - sizeof(SummaryEntry*));
		//TODO: Optimize bitusage, currently wasting 1,25 Bytes per Entry
		pointer += sizeof(Area) - sizeof(SummaryEntry*);
	}

	for(unsigned int i = 0; i < 2; i++){
		if(areaSummaryPositions[i] < 0)
			continue;
		for(unsigned int j = 0; j < dev->param->data_pages_per_area; j++){
			if(entry->areaMap[areaSummaryPositions[i]].areaSummary[j] != SummaryEntry::dirty)
				buf[pointer + j/8] |= 1 << j%8;
		}
		pointer += dev->param->data_pages_per_area / 8;
	}

	PAFFS_DBG_S(PAFFS_TRACE_WRITE, "%u bytes have been written to Buffer", pointer);

	pointer = 0;
	uint64_t page_offs = getPageNumber(addr, dev);
	Result r;
	for(unsigned page = 0; page < needed_pages; page++){
		unsigned int btw = pointer + dev->param->data_bytes_per_page < needed_bytes ? dev->param->data_bytes_per_page
							: needed_bytes - pointer;
		r = dev->driver->writePage(page_offs + page, &buf[pointer], btw);
		if(r != Result::ok)
			return r;

		pointer += btw;
	}

	return Result::ok;
}

Result readSuperPageIndex(Dev* dev, Addr addr, superIndex* entry, SummaryEntry* summary_Containers[2], bool withAreaMap){
	if(!withAreaMap)
		 return dev->driver->readPage(getPageNumber(addr, dev), entry, sizeof(uint32_t) + sizeof(Addr));

	if(entry->areaMap == 0)
		return Result::einval;

	unsigned int summary_Container_count = 0;

	unsigned int needed_bytes = sizeof(uint32_t) + sizeof(Addr) +
		dev->param->areas_no * (sizeof(Area) - sizeof(SummaryEntry*))+ // AreaMap without summaryEntry pointer
		16 * dev->param->data_pages_per_area / 8 /* One bit per entry, two entries for INDEX and DATA section. Others dont have summaries*/;

	unsigned int needed_pages = needed_bytes / BYTES_PER_PAGE + 1;

	char buf[needed_bytes];
	memset(buf, 0, needed_bytes);
	uint32_t pointer = 0;
	uint64_t page_offs = getPageNumber(addr, dev);
	Result r;
	for(unsigned page = 0; page < needed_pages; page++){
		unsigned int btr = pointer + dev->param->data_bytes_per_page < needed_bytes ? dev->param->data_bytes_per_page
							: needed_bytes - pointer;
		r = dev->driver->readPage(page_offs + page, &buf[pointer], btr);
		if(r != Result::ok)
			return r;

		pointer += btr;
	}
	//buffer ready
	PAFFS_DBG_S(PAFFS_TRACE_WRITE, "SuperIndex Buffer was filled with %u Bytes.", pointer);

	pointer = 0;
	memcpy(&entry->no, buf, sizeof(uint32_t));
	pointer += sizeof(uint32_t);
	memcpy(&entry->rootNode, &buf[pointer], sizeof(Addr));
	pointer += sizeof(Addr);
	long areaSummaryPositions[2];
	areaSummaryPositions[0] = -1;
	areaSummaryPositions[1] = -1;
	unsigned char pospos = 0;	//Stupid name
	for(unsigned int i = 0; i < dev->param->areas_no; i++){
		memcpy(&entry->areaMap[i], &buf[pointer], sizeof(Area) - sizeof(SummaryEntry*));
		pointer += sizeof(Area) - sizeof(SummaryEntry*);
		if(entry->areaMap[i].has_areaSummary)
			areaSummaryPositions[pospos++] = i;
	}

	unsigned char pagebuf[BYTES_PER_PAGE];
	for(unsigned int i = 0; i < 2; i++){
		if(areaSummaryPositions[i] < 0)
			continue;
		if(entry->areaMap[areaSummaryPositions[i]].areaSummary == 0){
			entry->areaMap[areaSummaryPositions[i]].areaSummary = summary_Containers[summary_Container_count++];
		}

		for(unsigned int j = 0; j < dev->param->data_pages_per_area; j++){
			if(buf[pointer + j/8] & 1 << j%8){
				//TODO: Normally, we would check in the OOB for a Checksum or so, which is present all the time
				Addr tmp = combineAddress(areaSummaryPositions[i], j);
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
					entry->areaMap[areaSummaryPositions[i]].areaSummary[j] = SummaryEntry::used;
				else
					entry->areaMap[areaSummaryPositions[i]].areaSummary[j] = SummaryEntry::free;
			}else{
				entry->areaMap[areaSummaryPositions[i]].areaSummary[j] = SummaryEntry::dirty;
			}
		}
		pointer += dev->param->data_pages_per_area / 8;
	}

	return Result::ok;
}

}

