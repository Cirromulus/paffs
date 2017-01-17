/*
 * paffs_flash.c
 *
 *  Created on: 30.06.2016
 *      Author: rooot
 */

#include "paffs_flash.h"
#include "garbage_collection.h"
#include <stdlib.h>
#include <string.h>


char* area_names[] = {
		"UNSET",
		"SUPERBLOCK",
		"INDEX",
		"JOURNAL",
		"DATA",
		"GC_BUFFER",
		"RETIRED",
		"YOUSHOULDNOTBESEEINGTHIS"
};


unsigned int findWritableArea(areaType areaType, p_dev* dev){
	if(dev->activeArea[areaType] != 0 && dev->areaMap[dev->activeArea[areaType]].status != CLOSED){
		//current Area has still space left
		return dev->activeArea[areaType];
	}

	for(unsigned int area = 0; area < dev->param.areas_no; area++){
		if(dev->areaMap[area].type == UNSET){
			dev->areaMap[area].type = areaType;
			initArea(dev, area);
			return area;
		}
	}

	PAFFS_RESULT r = collectGarbage(dev, areaType);
	if(r != PAFFS_OK){
		paffs_lasterr = r;
		return 0;
	}

	if(dev->activeArea[areaType] != 0 && dev->areaMap[dev->activeArea[areaType]].status != CLOSED){
		return dev->activeArea[areaType];
	}

	//If we arrive here, something buggy must have happened
	PAFFS_DBG(PAFFS_TRACE_BUG, "Garbagecollection pointed to invalid area!");
	paffs_lasterr = PAFFS_BUG;
	return 0;
}

PAFFS_RESULT findFirstFreePage(unsigned int* p_out, p_dev* dev, unsigned int area){

	for(int i = 0; i < dev->param.data_pages_per_area; i++){
		if(dev->areaMap[area].areaSummary[i] == FREE){
			*p_out = i;
			return PAFFS_OK;
		}
	}
	return PAFFS_NOSP;
}

uint64_t getPageNumber(p_addr addr, p_dev *dev){
	uint64_t page = dev->areaMap[extractLogicalArea(addr)].position *
								dev->param.total_pages_per_area;
	page += extractPage(addr);
	return page;
}

p_addr combineAddress(uint32_t logical_area, uint32_t page){
	p_addr addr = 0;
	memcpy(&addr, &logical_area, sizeof(uint32_t));
	memcpy(&((char*)&addr)[sizeof(uint32_t)], &page, sizeof(uint32_t));

	return addr;
}

unsigned int extractLogicalArea(p_addr addr){
	unsigned int area = 0;
	memcpy(&area, &addr, sizeof(uint32_t));
	return area;
}
unsigned int extractPage(p_addr addr){
	unsigned int page = 0;
	memcpy(&page, &((char*)&addr)[sizeof(uint32_t)], sizeof(uint32_t));
	return page;
}

PAFFS_RESULT manageActiveAreaFull(p_dev *dev, area_pos_t *area, areaType areaType){
	if(dev->areaMap[*area].areaSummary == NULL){
		PAFFS_DBG(PAFFS_TRACE_BUG, "Tried to access invalid areaSummary!");
		return PAFFS_BUG;
	}

	if(paffs_trace_mask & PAFFS_TRACE_VERIFY_AS){
		for(unsigned int i = 0; i < dev->param.data_pages_per_area; i++){
			if(dev->areaMap[*area].areaSummary[i] > DIRTY)
				PAFFS_DBG(PAFFS_TRACE_BUG, "Summary of %u contains invalid Entries!", *area);
		}
	}

	bool isFull = true;
	for(int i = 0; i < dev->param.data_pages_per_area; i++){
		if(dev->areaMap[*area].areaSummary[i] == FREE) {
			isFull = false;
			break;
		}
	}

	if(isFull){
		PAFFS_DBG_S(PAFFS_TRACE_AREA, "Info: Area %u (Type %s) full.", *area, area_names[areaType]);
		//Current Area is full!
		closeArea(dev, *area);
	}

	return PAFFS_OK;
}


//TODO: Add initAreaAs(...) to handle typical areaMap[abc].type = def; initArea(...);
void initArea(p_dev* dev, area_pos_t area){
	PAFFS_DBG_S(PAFFS_TRACE_AREA, "Info: Init Area %lu (pos %u) as %s.", area, dev->areaMap[area].position, area_names[dev->areaMap[area].type]);
	//generate the areaSummary in Memory
	dev->areaMap[area].status = ACTIVE;
	dev->areaMap[area].isAreaSummaryDirty = false;
	if(dev->areaMap[area].type == INDEXAREA || dev->areaMap[area].type == DATAAREA){
		if(dev->areaMap[area].areaSummary == NULL){
			dev->areaMap[area].areaSummary = malloc(
					sizeof(p_summaryEntry)
					* dev->param.blocks_per_area
					* dev->param.pages_per_block);
		}
		memset(dev->areaMap[area].areaSummary, 0,
				sizeof(p_summaryEntry)
				* dev->param.blocks_per_area
				* dev->param.pages_per_block);
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

PAFFS_RESULT loadArea(p_dev *dev, area_pos_t area){
	PAFFS_DBG_S(PAFFS_TRACE_AREA, "Info: Loading Areasummary of Area %u (pos %u) as %s.", area, dev->areaMap[area].position, area_names[dev->areaMap[area].type]);
	if(dev->areaMap[area].type != DATAAREA && dev->areaMap[area].type != INDEXAREA){
		return PAFFS_OK;
	}
	if(dev->areaMap[area].areaSummary != NULL){
		PAFFS_DBG(PAFFS_TRACE_BUG, "Tried to load Area with existing areaSummary!");
		return PAFFS_BUG;
	}

	if(dev->areaMap[area].isAreaSummaryDirty == true){
		PAFFS_DBG(PAFFS_TRACE_BUG, "Tried to load Area without areaSummary but dirty flag!");
		return PAFFS_BUG;
	}

	dev->areaMap[area].isAreaSummaryDirty = true;
	dev->areaMap[area].areaSummary = malloc(
			sizeof(p_summaryEntry)
			* dev->param.blocks_per_area
			* dev->param.pages_per_block);

	return readAreasummary(dev, area, dev->areaMap[area].areaSummary, true);
}

PAFFS_RESULT closeArea(p_dev *dev, area_pos_t area){
	dev->areaMap[area].status = CLOSED;

	if(paffs_trace_mask & PAFFS_TRACE_VERIFY_AS){
		for(unsigned int i = 0; i < dev->param.data_pages_per_area; i++){
			if(dev->areaMap[area].areaSummary[i] > DIRTY)
				PAFFS_DBG(PAFFS_TRACE_BUG, "Summary of %u contains invalid Entries!", area);
		}
	}

	//TODO: Suspend areaSummary write until RAM cache runs low
	if(dev->areaMap[area].type == DATAAREA || dev->areaMap[area].type == INDEXAREA){
		PAFFS_RESULT r = writeAreasummary(dev, area, dev->areaMap[area].areaSummary);
		if(r != PAFFS_OK)
			return r;
	}
	//TODO: delete all area summaries if low on RAM
	if(dev->areaMap[area].type != DATAAREA && dev->areaMap[area].type != INDEXAREA){
		free(dev->areaMap[area].areaSummary);
		dev->areaMap[area].areaSummary = NULL;
	}

	PAFFS_DBG_S(PAFFS_TRACE_AREA, "Info: Closed %s Area %u at pos. %u.", area_names[dev->areaMap[area].type], area, dev->areaMap[area].position);
	return PAFFS_OK;
}

void retireArea(p_dev *dev, area_pos_t area){
	dev->areaMap[area].status = CLOSED;

	if((dev->areaMap[area].type == DATAAREA || dev->areaMap[area].type == INDEXAREA) && paffs_trace_mask & PAFFS_TRACE_VERIFY_AS){
		for(unsigned int i = 0; i < dev->param.data_pages_per_area; i++){
			if(dev->areaMap[area].areaSummary[i] > DIRTY)
				PAFFS_DBG(PAFFS_TRACE_BUG, "Summary of %u contains invalid Entries!", area);
		}
	}

	//TODO: delete all area summaries if low on RAM
	if(dev->areaMap[area].type != DATAAREA && dev->areaMap[area].type != INDEXAREA){
		free(dev->areaMap[area].areaSummary);
		dev->areaMap[area].areaSummary = NULL;
	}

	dev->areaMap[area].type = RETIRED;

	PAFFS_DBG_S(PAFFS_TRACE_AREA, "Info: RETIRED Area %u at pos. %u.", area, dev->areaMap[area].position);
}

PAFFS_RESULT writeAreasummary(p_dev *dev, area_pos_t area, p_summaryEntry* summary){
	unsigned int needed_bytes = 1 + dev->param.data_pages_per_area / 8;
	unsigned int needed_pages = 1 + needed_bytes / dev->param.data_bytes_per_page;
	if(needed_pages != dev->param.total_pages_per_area - dev->param.data_pages_per_area){
		PAFFS_DBG(PAFFS_TRACE_ERROR, "AreaSummary size differs with formatting infos!");
		return PAFFS_FAIL;
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
	for(unsigned int j = 0; j < dev->param.data_pages_per_area; j++){
		if(summary[j] != DIRTY)
			buf[j/8] |= 1 << j%8;
	}

	uint32_t pointer = 0;
	uint64_t page_offs = getPageNumber(combineAddress(area, dev->param.data_pages_per_area), dev);
	PAFFS_RESULT r;
	for(unsigned int page = 0; page < needed_pages; page++){
		unsigned int btw = pointer + dev->param.data_bytes_per_page < needed_bytes ? dev->param.data_bytes_per_page
							: needed_bytes - pointer;
		r = dev->drv.drv_write_page_fn(dev, page_offs + page, &buf[pointer], btw);
		if(r != PAFFS_OK)
			return r;

		pointer += btw;
	}
	return PAFFS_OK;
}

//FIXME: readAreasummary is untested, b/c areaSummaries remain in RAM during unmount
PAFFS_RESULT readAreasummary(p_dev *dev, area_pos_t area, p_summaryEntry* out_summary, bool complete){
	unsigned int needed_bytes = 1 + dev->param.data_pages_per_area / 8 /* One bit per entry*/;

	unsigned int needed_pages = 1 + needed_bytes / dev->param.data_bytes_per_page;
	if(needed_pages != dev->param.total_pages_per_area - dev->param.data_pages_per_area){
		PAFFS_DBG(PAFFS_TRACE_ERROR, "AreaSummary size differs with formatting infos!");
		return PAFFS_FAIL;
	}

	char buf[needed_bytes];
	memset(buf, 0, needed_bytes);
	uint32_t pointer = 0;
	uint64_t page_offs = getPageNumber(combineAddress(area, dev->param.data_pages_per_area), dev);
	PAFFS_RESULT r;
	for(unsigned int page = 0; page < needed_pages; page++){
		unsigned int btr = pointer + dev->param.data_bytes_per_page < needed_bytes ? dev->param.data_bytes_per_page
							: needed_bytes - pointer;
		r = dev->drv.drv_read_page_fn(dev, page_offs + page, &buf[pointer], btr);
		if(r != PAFFS_OK)
			return r;

		pointer += btr;
	}
	//buffer ready
	PAFFS_DBG_S(PAFFS_TRACE_WRITE, "SuperIndex Buffer was filled with %u Bytes.", pointer);


	for(unsigned int j = 0; j < dev->param.data_pages_per_area; j++){
		if(buf[j/8] & 1 << j%8){
			if(complete){
				unsigned char pagebuf[BYTES_PER_PAGE];
				p_addr tmp = combineAddress(area, j);
				r = dev->drv.drv_read_page_fn(dev, getPageNumber(tmp, dev), pagebuf, dev->param.data_bytes_per_page);
				if(r != PAFFS_OK)
					return r;
				bool contains_data = false;
				for(int byte = 0; byte < dev->param.data_bytes_per_page; byte++){
					if(pagebuf[byte] != 0xFF){
						contains_data = true;
						break;
					}
				}
				if(contains_data)
					out_summary[j] = USED;
				else
					out_summary[j] = FREE;
			}else{
				//This is just a guess b/c we are in incomplete mode.
				out_summary[j] = USED;
			}
		}else{
			out_summary[j] = DIRTY;
		}
	}

	return PAFFS_OK;
}

//modifies inode->size and inode->reserved size as well
PAFFS_RESULT writeInodeData(pInode* inode,
					unsigned int offs, unsigned int bytes, unsigned int *bytes_written,
					const char* data, p_dev* dev){

	if(offs+bytes == 0){
		PAFFS_DBG(PAFFS_TRACE_ERROR, "Write size 0! Bug?");
		return paffs_lasterr = PAFFS_EINVAL;
	}

	unsigned int pageFrom = offs/dev->param.data_bytes_per_page;
	unsigned int pageTo = (offs + bytes - 1) / dev->param.data_bytes_per_page;

	if(pageTo - pageFrom > 11){
		//Would use first indirection Layer
		PAFFS_DBG(PAFFS_TRACE_ALWAYS, "Write would use first indirection layer, too big!");
		return paffs_lasterr = PAFFS_NIMPL;
	}

	unsigned int pageOffs = offs % dev->param.data_bytes_per_page;
	*bytes_written = 0;

	for(int page = 0; page <= pageTo - pageFrom; page++){
		bool misaligned = false;
		dev->activeArea[DATAAREA] = findWritableArea(DATAAREA, dev);
		if(paffs_lasterr != PAFFS_OK){
			return paffs_lasterr;
		}

		//Handle Areas
		if(dev->areaMap[dev->activeArea[DATAAREA]].status == EMPTY){
			//We'll have to use a fresh area,
			//so generate the areaSummary in Memory
			initArea(dev, dev->activeArea[DATAAREA]);
		}
		unsigned int firstFreePage = 0;
		if(findFirstFreePage(&firstFreePage, dev, dev->activeArea[DATAAREA]) == PAFFS_NOSP){
			PAFFS_DBG(PAFFS_BUG, "BUG: findWritableArea returned full area (%d).", dev->activeArea[DATAAREA]);
			return PAFFS_BUG;
		}
		p_addr pageAddress = combineAddress(dev->activeArea[DATAAREA], firstFreePage);

		dev->areaMap[dev->activeArea[DATAAREA]].areaSummary[firstFreePage] = USED;

		//Prepare buffer and calculate bytes to write
		char* buf = &((char*)data)[page*dev->param.data_bytes_per_page];
		unsigned int btw = bytes - *bytes_written;
		if((bytes+pageOffs) > dev->param.data_bytes_per_page){
			btw = (bytes+pageOffs) > (page+1)*dev->param.data_bytes_per_page ?
						dev->param.data_bytes_per_page - pageOffs :
						bytes - page*dev->param.data_bytes_per_page;
		}



		if(inode->direct[page+pageFrom] != 0){
			//We are overriding existing data
			//mark old Page in Areamap
			unsigned long oldArea = extractLogicalArea(inode->direct[page+pageFrom]);
			unsigned long oldPage = extractPage(inode->direct[page+pageFrom]);


			if((btw + pageOffs < dev->param.data_bytes_per_page &&
				page*dev->param.data_bytes_per_page + btw < inode->size) ||  //End Misaligned
				(pageOffs > 0 && page == 0)){				//Start Misaligned

				//fill write buffer with valid Data
				misaligned = true;
				buf = (char*)malloc(dev->param.data_bytes_per_page);
				memset(buf, 0xFF, dev->param.data_bytes_per_page);

				unsigned int btr = dev->param.data_bytes_per_page;

				if((pageFrom+1+page)*dev->param.data_bytes_per_page > inode->size){
					btr = inode->size - (pageFrom+page) * dev->param.data_bytes_per_page;
				}

				unsigned int bytes_read = 0;
				PAFFS_RESULT r = readInodeData(inode, (pageFrom+page)*dev->param.data_bytes_per_page, btr, &bytes_read, buf, dev);
				if(r != PAFFS_OK || bytes_read != btr){
					free(buf);
					return PAFFS_BUG;
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
				PAFFS_RESULT r = loadArea(dev, oldArea);
				if(r != PAFFS_OK){
					PAFFS_DBG(PAFFS_TRACE_ERROR, "Could not read A/S from CLOSED area!");
					return r;
				}
			}
			dev->areaMap[oldArea].areaSummary[oldPage] = DIRTY;
		}else{
			//we are writing to a new page
			*bytes_written += btw;
			inode->reservedSize += dev->param.data_bytes_per_page;
		}
		inode->direct[page+pageFrom] = pageAddress;

		PAFFS_RESULT res = dev->drv.drv_write_page_fn(dev, getPageNumber(pageAddress, dev), buf, btw);

		if(misaligned)
			free(buf);

		PAFFS_DBG_S(PAFFS_TRACE_WRITE, "write r.P: %d/%d, phy.P: %llu", page+1, pageTo+1, (long long unsigned int) getPageNumber(pageAddress, dev));
		if(res != PAFFS_OK){
			PAFFS_DBG(PAFFS_TRACE_ERROR, "ERR: write returned FAIL at phy.P: %llu", (long long unsigned int) getPageNumber(pageAddress, dev));
			return PAFFS_FAIL;
		}

		res = manageActiveAreaFull(dev, &dev->activeArea[DATAAREA], DATAAREA);
		if(res != PAFFS_OK)
			return res;

	}

	if(inode->size < *bytes_written + offs)
		inode->size = *bytes_written + offs;

	return updateExistingInode(dev, inode);
}
PAFFS_RESULT readInodeData(pInode* inode,
					unsigned int offs, unsigned int bytes, unsigned int *bytes_read,
					char* data, p_dev* dev){

	if(offs+bytes == 0){
		PAFFS_DBG(PAFFS_TRACE_ERROR, "Read size 0! Bug?");
		return paffs_lasterr = PAFFS_EINVAL;
	}

	*bytes_read = 0;
	unsigned int pageFrom = offs/dev->param.data_bytes_per_page;
	unsigned int pageTo = (offs + bytes - 1) / dev->param.data_bytes_per_page;
	unsigned int pageOffs = offs % dev->param.data_bytes_per_page;


	if(offs + bytes > inode->size){
		PAFFS_DBG(PAFFS_TRACE_ERROR, "Read bigger than size of object! (was: %d, max: %lu)", offs+bytes, (long unsigned) inode->size);
		//TODO: return less bytes_read
		return PAFFS_NIMPL;
	}

	if(pageTo > 11){
		//todo Read indirection Layers
		return PAFFS_NIMPL;
	}

	char* wrap = data;
	bool misaligned = false;
	if(pageOffs > 0){
		misaligned = true;
		wrap = malloc(bytes + offs);
	}

	for(int page = 0; page <= pageTo - pageFrom; page++){
		char* buf = &wrap[page*dev->param.data_bytes_per_page];

		unsigned int btr = bytes + pageOffs - *bytes_read;
		if(btr > dev->param.data_bytes_per_page){
			btr = (bytes + pageOffs) > (page+1)*dev->param.data_bytes_per_page ?
						dev->param.data_bytes_per_page :
						(bytes + pageOffs) - page*dev->param.data_bytes_per_page;
		}

		area_pos_t area = extractLogicalArea(inode->direct[page + pageFrom]);
		if(dev->areaMap[area].type != DATAAREA){
			PAFFS_DBG(PAFFS_TRACE_BUG, "READ INODE operation of invalid area at %d:%d", extractLogicalArea(inode->direct[page + pageFrom]),extractPage(inode->direct[page + pageFrom]));
			return PAFFS_BUG;
		}

		PAFFS_RESULT r = PAFFS_OK;
		if(dev->areaMap[area].areaSummary == NULL){
			//TODO: This is very expensive. Either build switch "safety mode" that loads complete A/S
			//		Or just load (everytime) incomplete A/S
			r = loadArea(dev, area);
			if(r != PAFFS_OK){
				PAFFS_DBG(PAFFS_TRACE_ERROR, "Could not load AreaSummary for safetycheck!");
			}
		}

		if(r == PAFFS_OK){
			if(dev->areaMap[extractLogicalArea(inode->direct[page + pageFrom])].areaSummary[extractPage(inode->direct[page + pageFrom])] == DIRTY){
				PAFFS_DBG(PAFFS_TRACE_BUG, "READ INODE operation of outdated (dirty) data at %d:%d", extractLogicalArea(inode->direct[page + pageFrom]),extractPage(inode->direct[page + pageFrom]));
				return PAFFS_BUG;
			}

			if(dev->areaMap[extractLogicalArea(inode->direct[page + pageFrom])].areaSummary[extractPage(inode->direct[page + pageFrom])] == FREE){
				PAFFS_DBG(PAFFS_TRACE_BUG, "READ INODE operation of invalid (FREE) data at %d:%d", extractLogicalArea(inode->direct[page + pageFrom]),extractPage(inode->direct[page + pageFrom]));
				return PAFFS_BUG;
			}
		}

		unsigned long long addr = getPageNumber(inode->direct[page + pageFrom], dev);
		r = dev->drv.drv_read_page_fn(dev, addr, buf, btr);
		if(r != PAFFS_OK){
			if(misaligned)
				free (wrap);
			return paffs_lasterr = r;
		}
		*bytes_read += btr;

	}

	if(misaligned) {
		memcpy(data, &wrap[pageOffs], bytes);
		*bytes_read -= pageOffs;
		free (wrap);
	}

	return PAFFS_OK;
}


//inode->size and inode->reservedSize is altered.
PAFFS_RESULT deleteInodeData(pInode* inode, p_dev* dev, unsigned int offs){
	//TODO: This calculation contains errors in border cases
	unsigned int pageFrom = offs/dev->param.data_bytes_per_page;
	unsigned int pageTo = (inode->size - 1) / dev->param.data_bytes_per_page;

	if(inode->size < offs){
		//Offset bigger than actual filesize
		return PAFFS_EINVAL;
	}

	if(pageTo > 11){
		//todo Read indirection Layers
		return PAFFS_NIMPL;
	}


	inode->size = offs;

	if(inode->reservedSize == 0)
		return PAFFS_OK;

	if(inode->size >= inode->reservedSize - dev->param.data_bytes_per_page)
		//doesn't leave a whole page blank
		return PAFFS_OK;


	for(int page = 0; page <= pageTo - pageFrom; page++){

		unsigned int area = extractLogicalArea(inode->direct[page + pageFrom]);
		unsigned int relPage = extractPage(inode->direct[page + pageFrom]);

		if(dev->areaMap[area].type != DATAAREA){
			PAFFS_DBG(PAFFS_TRACE_BUG, "DELETE INODE operation of invalid area at %d:%d", extractLogicalArea(inode->direct[page + pageFrom]),extractPage(inode->direct[page + pageFrom]));
			return PAFFS_BUG;
		}

		if(dev->areaMap[area].areaSummary[relPage] == DIRTY){
			PAFFS_DBG(PAFFS_TRACE_BUG, "DELETE INODE operation of outdated (dirty) data at %d:%d", extractLogicalArea(inode->direct[page + pageFrom]),extractPage(inode->direct[page + pageFrom]));
			return PAFFS_BUG;
		}

		//Mark old pages dirty
		dev->areaMap[area].areaSummary[relPage] = DIRTY;

		inode->reservedSize -= dev->param.data_bytes_per_page;
		inode->direct[page+pageFrom] = 0;

	}

	return PAFFS_OK;
}

//Does not change addresses in parent Nodes
PAFFS_RESULT writeTreeNode(p_dev* dev, treeNode* node){
	if(node == NULL){
		PAFFS_DBG(PAFFS_TRACE_BUG, "BUG: treeNode NULL");
				return PAFFS_BUG;
	}
	if(sizeof(treeNode) > dev->param.data_bytes_per_page){
		PAFFS_DBG(PAFFS_TRACE_BUG, "BUG: treeNode bigger than Page (Was %lu, should %u)", sizeof(treeNode), dev->param.data_bytes_per_page);
		return PAFFS_BUG;
	}

	if(node->self != 0){
		//We have to invalidate former position first
		dev->areaMap[extractLogicalArea(node->self)].areaSummary[extractPage(node->self)] = DIRTY;
	}

	paffs_lasterr = PAFFS_OK;
	dev->activeArea[INDEXAREA] = findWritableArea(INDEXAREA, dev);
	if(paffs_lasterr != PAFFS_OK){
		return paffs_lasterr;
	}

	if(dev->activeArea[INDEXAREA] == 0){
		PAFFS_DBG(PAFFS_TRACE_BUG, "WRITE TREE NODE findWritableArea returned 0");
		return PAFFS_BUG;
	}

	unsigned int firstFreePage = 0;
	if(findFirstFreePage(&firstFreePage, dev, dev->activeArea[INDEXAREA]) == PAFFS_NOSP){
		PAFFS_DBG(PAFFS_TRACE_BUG, "BUG: findWritableArea returned full area (%d).", dev->activeArea[INDEXAREA]);
		return paffs_lasterr = PAFFS_BUG;
	}
	p_addr addr = combineAddress(dev->activeArea[INDEXAREA], firstFreePage);
	node->self = addr;

	dev->areaMap[dev->activeArea[INDEXAREA]].areaSummary[firstFreePage] = USED;

	PAFFS_RESULT r = dev->drv.drv_write_page_fn(dev, getPageNumber(node->self, dev), node, sizeof(treeNode));
	if(r != PAFFS_OK)
		return r;

	r = manageActiveAreaFull(dev, &dev->activeArea[INDEXAREA], INDEXAREA);
	if(r != PAFFS_OK)
		return r;

	return PAFFS_OK;
}

PAFFS_RESULT readTreeNode(p_dev* dev, p_addr addr, treeNode* node){
	if(node == NULL){
		PAFFS_DBG(PAFFS_TRACE_BUG, "BUG: treeNode NULL");
		return PAFFS_BUG;
	}
	if(sizeof(treeNode) > dev->param.data_bytes_per_page){
		PAFFS_DBG(PAFFS_TRACE_BUG, "BUG: treeNode bigger than Page (Was %lu, should %u)", sizeof(treeNode), dev->param.data_bytes_per_page);
		return PAFFS_BUG;
	}

	if(dev->areaMap[extractLogicalArea(addr)].type != INDEXAREA){
		PAFFS_DBG(PAFFS_TRACE_ERROR, "READ TREEENODE operation on %s!", area_names[dev->areaMap[extractLogicalArea(addr)].type]);
		return PAFFS_BUG;
	}

	if(dev->areaMap[extractLogicalArea(addr)].areaSummary == 0){
		PAFFS_DBG_S(PAFFS_TRACE_SCAN, "READ operation on INDEXAREA without areaSummary!");
		//TODO: Could be safer if areaSummary would be read from flash for safety
	}else{
		if(dev->areaMap[extractLogicalArea(addr)].areaSummary[extractPage(addr)] == DIRTY){
			PAFFS_DBG(PAFFS_TRACE_ERROR, "READ operation of obsoleted data at %X:%X", extractLogicalArea(addr), extractPage(addr));
			return PAFFS_BUG;
		}

		if(extractLogicalArea(addr) == 0){
			PAFFS_DBG(PAFFS_TRACE_ERROR, "READ TREE NODE operation on (log.) first Area at %X:%X", extractLogicalArea(addr), extractPage(addr));
			return PAFFS_BUG;
		}
	}

	PAFFS_RESULT r = dev->drv.drv_read_page_fn(dev, getPageNumber(addr, dev), node, sizeof(treeNode));
	if(r != PAFFS_OK)
		return r;

	if(node->self != addr){
		PAFFS_DBG(PAFFS_TRACE_BUG, "Read Treenode at %X:%X, but its content stated that it was on %X:%X", extractLogicalArea(addr), extractPage(addr), extractLogicalArea(node->self), extractPage(node->self));
		return PAFFS_BUG;
	}

	return PAFFS_OK;
}

PAFFS_RESULT deleteTreeNode(p_dev* dev, treeNode* node){
	dev->areaMap[extractLogicalArea(node->self)].areaSummary[extractPage(node->self)] = DIRTY;
	return PAFFS_OK;
}


// Superblock related

PAFFS_RESULT findFirstFreeEntryInBlock(p_dev* dev, uint32_t area, uint8_t block, uint32_t* out_pos, unsigned int required_pages){
	unsigned int in_a_row = 0;
	uint64_t page_offs = dev->param.pages_per_block * block;
	for(unsigned int i = 0; i < dev->param.pages_per_block; i++) {
		p_addr addr = combineAddress(area, i + page_offs);
		uint32_t no;
		PAFFS_RESULT r = dev->drv.drv_read_page_fn(dev, getPageNumber(addr, dev), &no, sizeof(uint32_t));
		if(r != PAFFS_OK)
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
			return PAFFS_OK;
	}
	return PAFFS_NF;
}

PAFFS_RESULT findMostRecentEntryInBlock(p_dev* dev, uint32_t area, uint8_t block, uint32_t* out_pos, uint32_t* out_index){
	uint32_t* maximum = out_index;
	*maximum = 0;
	*out_pos = 0;
	uint32_t page_offs = dev->param.pages_per_block * block;
	for(unsigned int i = 0; i < dev->param.pages_per_block; i++) {
		p_addr addr = combineAddress(area, i + page_offs);
		uint32_t no;
		PAFFS_RESULT r = dev->drv.drv_read_page_fn(dev, getPageNumber(addr, dev), &no, sizeof(uint32_t));
		if(r != PAFFS_OK)
			return r;
		if(no == 0xFFFFFFFF){
			// Unprogrammed, therefore empty
			if(*maximum != 0)
				return PAFFS_OK;
			return PAFFS_NF;
		}

		if(no > *maximum){
			*out_pos = i + page_offs;
			*maximum = no;
		}
	}

	return PAFFS_OK;
}


PAFFS_RESULT writeAnchorEntry(p_dev* dev, p_addr addr, anchorEntry* entry){
	//Currently not implemented to simplify Find-Strategy
	return PAFFS_NIMPL;
}
PAFFS_RESULT readAnchorEntry(p_dev* dev, p_addr addr, anchorEntry* entry){
	//Currently not implemented to simplify Find-Strategy
	return PAFFS_NIMPL;
}

PAFFS_RESULT deleteAnchorBlock(p_dev* dev, uint32_t area, uint8_t block) {
	if(dev->areaMap[area].type != SUPERBLOCKAREA){
		PAFFS_DBG(PAFFS_TRACE_BUG, "Tried to delete Block outside of SUPARBLCOKAREA");
		return PAFFS_BUG;
	}
	uint32_t block_offs = dev->areaMap[area].position * dev->param.blocks_per_area;
	return dev->drv.drv_erase_fn(dev, block_offs + block);
}

PAFFS_RESULT writeJumpPadEntry(p_dev* dev, p_addr addr, jumpPadEntry* entry){
	//Currently not implemented to simplify Find-Strategy
	return PAFFS_NIMPL;
}

PAFFS_RESULT readJumpPadEntry(p_dev* dev, p_addr addr, jumpPadEntry* entry){
	//Currently not implemented to simplify Find-Strategy
	return PAFFS_NIMPL;
}


//Make sure that free space is sufficient!
PAFFS_RESULT writeSuperIndex(p_dev* dev, p_addr addr, superIndex* entry){
	if(dev->areaMap[extractLogicalArea(addr)].type != SUPERBLOCKAREA){
		PAFFS_DBG(PAFFS_TRACE_BUG, "Tried to write superIndex outside of superblock Area");
		return PAFFS_BUG;
	}

	unsigned int needed_bytes = sizeof(uint32_t) + sizeof(p_addr) +
		dev->param.areas_no * (sizeof(p_area) - sizeof(p_summaryEntry*))+ // AreaMap without summaryEntry pointer
		2 * dev->param.data_pages_per_area / 8 /* One bit per entry, two entrys for INDEX and DATA section*/;

	unsigned int needed_pages = needed_bytes / BYTES_PER_PAGE + 1;

	unsigned int pointer = 0;
	char buf[needed_bytes];
	memset(buf, 0, needed_bytes);
	memcpy(buf, &entry->no, sizeof(uint32_t));
	pointer += sizeof(uint32_t);
	memcpy(&buf[pointer], &entry->rootNode, sizeof(p_addr));
	pointer += sizeof(p_addr);
	long areaSummaryPositions[2];
	areaSummaryPositions[0] = -1;
	areaSummaryPositions[1] = -1;
	unsigned char pospos = 0;	//Stupid name

	for(unsigned int i = 0; i < dev->param.areas_no; i++){
		if((entry->areaMap[i].type == INDEXAREA || entry->areaMap[i].type == DATAAREA) && entry->areaMap[i].status == ACTIVE){
			areaSummaryPositions[pospos++] = i;
			entry->areaMap[i].has_areaSummary = true;
		}else{
			entry->areaMap[i].has_areaSummary = false;
		}

		memcpy(&buf[pointer], &entry->areaMap[i], sizeof(p_area) - sizeof(p_summaryEntry*));
		//TODO: Optimize bitusage, currently wasting 1,25 Bytes per Entry
		pointer += sizeof(p_area) - sizeof(p_summaryEntry*);
	}

	for(unsigned int i = 0; i < 2; i++){
		if(areaSummaryPositions[i] < 0)
			continue;
		for(unsigned int j = 0; j < dev->param.data_pages_per_area; j++){
			if(entry->areaMap[areaSummaryPositions[i]].areaSummary[j] != DIRTY)
				buf[pointer + j/8] |= 1 << j%8;
		}
		pointer += dev->param.data_pages_per_area / 8;
	}

	PAFFS_DBG_S(PAFFS_TRACE_WRITE, "%u bytes have been written to Buffer", pointer);

	pointer = 0;
	uint64_t page_offs = getPageNumber(addr, dev);
	PAFFS_RESULT r;
	for(unsigned page = 0; page < needed_pages; page++){
		unsigned int btw = pointer + dev->param.data_bytes_per_page < needed_bytes ? dev->param.data_bytes_per_page
							: needed_bytes - pointer;
		r = dev->drv.drv_write_page_fn(dev, page_offs + page, &buf[pointer], btw);
		if(r != PAFFS_OK)
			return r;

		pointer += btw;
	}

	return PAFFS_OK;
}

PAFFS_RESULT readSuperPageIndex(p_dev* dev, p_addr addr, superIndex* entry, p_summaryEntry* summary_Containers[2], bool withAreaMap){
	if(!withAreaMap)
		 return dev->drv.drv_read_page_fn(dev, getPageNumber(addr, dev), entry, sizeof(uint32_t) + sizeof(p_addr));

	if(entry->areaMap == 0)
		return PAFFS_EINVAL;

	unsigned int summary_Container_count = 0;

	unsigned int needed_bytes = sizeof(uint32_t) + sizeof(p_addr) +
		dev->param.areas_no * (sizeof(p_area) - sizeof(p_summaryEntry*))+ // AreaMap without summaryEntry pointer
		16 * dev->param.data_pages_per_area / 8 /* One bit per entry, two entries for INDEX and DATA section. Others dont have summaries*/;

	unsigned int needed_pages = needed_bytes / BYTES_PER_PAGE + 1;

	char buf[needed_bytes];
	memset(buf, 0, needed_bytes);
	uint32_t pointer = 0;
	uint64_t page_offs = getPageNumber(addr, dev);
	PAFFS_RESULT r;
	for(unsigned page = 0; page < needed_pages; page++){
		unsigned int btr = pointer + dev->param.data_bytes_per_page < needed_bytes ? dev->param.data_bytes_per_page
							: needed_bytes - pointer;
		r = dev->drv.drv_read_page_fn(dev, page_offs + page, &buf[pointer], btr);
		if(r != PAFFS_OK)
			return r;

		pointer += btr;
	}
	//buffer ready
	PAFFS_DBG_S(PAFFS_TRACE_WRITE, "SuperIndex Buffer was filled with %u Bytes.", pointer);

	pointer = 0;
	memcpy(&entry->no, buf, sizeof(uint32_t));
	pointer += sizeof(uint32_t);
	memcpy(&entry->rootNode, &buf[pointer], sizeof(p_addr));
	pointer += sizeof(p_addr);
	long areaSummaryPositions[2];
	areaSummaryPositions[0] = -1;
	areaSummaryPositions[1] = -1;
	unsigned char pospos = 0;	//Stupid name
	for(unsigned int i = 0; i < dev->param.areas_no; i++){
		memcpy(&entry->areaMap[i], &buf[pointer], sizeof(p_area) - sizeof(p_summaryEntry*));
		pointer += sizeof(p_area) - sizeof(p_summaryEntry*);
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

		for(unsigned int j = 0; j < dev->param.data_pages_per_area; j++){
			if(buf[pointer + j/8] & 1 << j%8){
				//TODO: Normally, we would check in the OOB for a Checksum or so, which is present all the time
				p_addr tmp = combineAddress(areaSummaryPositions[i], j);
				r = dev->drv.drv_read_page_fn(dev, getPageNumber(tmp, dev), pagebuf, dev->param.data_bytes_per_page);
				if(r != PAFFS_OK)
					return r;
				bool contains_data = false;
				for(int byte = 0; byte < dev->param.data_bytes_per_page; byte++){
					if(pagebuf[byte] != 0xFF){
						contains_data = true;
						break;
					}
				}
				if(contains_data)
					entry->areaMap[areaSummaryPositions[i]].areaSummary[j] = USED;
				else
					entry->areaMap[areaSummaryPositions[i]].areaSummary[j] = FREE;
			}else{
				entry->areaMap[areaSummaryPositions[i]].areaSummary[j] = DIRTY;
			}
		}
		pointer += dev->param.data_pages_per_area / 8;
	}

	return PAFFS_OK;
}



