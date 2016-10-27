/*
 * paffs_flash.c
 *
 *  Created on: 30.06.2016
 *      Author: rooot
 */

#include "paffs_flash.h"
#include <stdlib.h>
#include <string.h>

unsigned int activeArea[area_types_no] = {0};

unsigned int findWritableArea(p_areaType areaType, p_dev* dev){
	if(activeArea[areaType] == 0 || dev->areaMap[activeArea[areaType]].status == CLOSED){
		for(int try = 1; try <= 2; try++){
			for(int area = 0; area < dev->param.areas_no; area++){
				if(dev->areaMap[area].type != areaType){
					continue;
				}
				if(try == 1){
					if(dev->areaMap[area].status == ACTIVE){	//ACTIVE oder closed first?
						return area;
					}
				}else{
					//Now look for "new", empty one. Ideal would be to pick the one with less erases
					if(dev->areaMap[area].status == EMPTY){	//unclosed oder empty first?
						return area;
					}
				}

			}
		}
	}else{
		//current Area has still space left
		return activeArea[areaType];
	}
	paffs_lasterr = PAFFS_NOSP;
	return 0;
}

PAFFS_RESULT findFirstFreePage(unsigned int* p_out, p_dev* dev, unsigned int area){

	for(int i = 0; i < dev->param.blocks_per_area * dev->param.pages_per_block; i++){
		if(dev->areaMap[area].areaSummary[i] == FREE){
			*p_out = i;
			return PAFFS_OK;
		}
	}
	return PAFFS_NOSP;
}

uint64_t getPageNumber(p_addr addr, p_dev *dev){
	uint64_t page = dev->areaMap[extractLogicalArea(addr)].position *
								dev->param.blocks_per_area * dev->param.pages_per_block;
	page += extractPage(addr);
	return page;
}

PAFFS_RESULT checkActiveAreaFull(p_dev *dev, unsigned int *area, p_areaType areaType){
	if(dev->areaMap[*area].areaSummary == NULL){
		PAFFS_DBG(PAFFS_TRACE_BUG, "Tried to access invalid areaSummary!");
		return PAFFS_BUG;
	}

	unsigned int usedPages = 0;
	for(int i = 0; i < dev->param.pages_per_area; i++){
		if(dev->areaMap[*area].areaSummary[i] != FREE)
			usedPages++;
	}

	if(usedPages == dev->param.pages_per_area){
		PAFFS_DBG(PAFFS_TRACE_AREA, "Info: Area %u (Type %d) full.", *area, areaType);
		//Area is full!
		//TODO: Check if dirty Pages are inside and
		//garbage collect this instead of just closing it...
		dev->areaMap[*area].status = CLOSED;
		//Second try. Normally there would be more, because
		//areas could be full without being closed
		*area = findWritableArea(areaType, dev);
		if(paffs_lasterr != PAFFS_OK){
			return paffs_lasterr;
		}
		initArea(dev, *area);
		if(paffs_lasterr != PAFFS_OK){
			return paffs_lasterr;
		}
	}
	//Safety-check
	if(usedPages > dev->param.pages_per_area){
		PAFFS_DBG(PAFFS_TRACE_BUG, "BUG: used Pages bigger than actual pagecount (was: %u, should %u)", usedPages, dev->param.pages_per_area);
		return PAFFS_BUG;
	}
	return PAFFS_OK;
}

void initArea(p_dev* dev, unsigned long int area){
	PAFFS_DBG(PAFFS_TRACE_AREA, "Info: Init new Area %lu.", area);
	//generate the areaSummary in Memory
	dev->areaMap[area].status = ACTIVE;
	if(dev->areaMap[area].type == INDEXAREA || dev->areaMap[area].type == DATAAREA){
		dev->areaMap[area].areaSummary = malloc(
				sizeof(p_summaryEntry)
				* dev->param.blocks_per_area
				* dev->param.pages_per_block);
		memset(dev->areaMap[area].areaSummary, 0,
				sizeof(p_summaryEntry)
				* dev->param.blocks_per_area
				* dev->param.pages_per_block);
		dev->areaMap[area].has_areaSummary = true;
	}else{
		dev->areaMap[area].has_areaSummary = false;
	}
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
		activeArea[DATAAREA] = findWritableArea(DATAAREA, dev);
		if(paffs_lasterr != PAFFS_OK){
			return paffs_lasterr;
		}

		//Handle Areas
		if(dev->areaMap[activeArea[DATAAREA]].status == EMPTY){
			//We'll have to use a fresh area,
			//so generate the areaSummary in Memory
			initArea(dev, activeArea[DATAAREA]);
		}
		unsigned int firstFreePage = 0;
		if(findFirstFreePage(&firstFreePage, dev, activeArea[DATAAREA]) == PAFFS_NOSP){
			PAFFS_DBG(PAFFS_BUG, "BUG: findWritableArea returned full area (%d).", activeArea[DATAAREA]);
			return PAFFS_BUG;
		}
		p_addr pageAddress = combineAddress(dev->areaMap[activeArea[DATAAREA]].position, firstFreePage);

		dev->areaMap[activeArea[DATAAREA]].areaSummary[firstFreePage] = USED;

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

		PAFFS_DBG(PAFFS_TRACE_WRITE, "DBG: write r.P: %d/%d, phy.P: %llu", page+1, pageTo+1, (long long unsigned int) getPageNumber(pageAddress, dev));
		if(res != PAFFS_OK){
			PAFFS_DBG(PAFFS_TRACE_ERROR, "ERR: write returned FAIL at phy.P: %llu", (long long unsigned int) getPageNumber(pageAddress, dev));
			return PAFFS_FAIL;
		}

		res = checkActiveAreaFull(dev, &activeArea[DATAAREA], DATAAREA);
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

		if(dev->areaMap[extractLogicalArea(inode->direct[page + pageFrom])].type != DATAAREA){
			PAFFS_DBG(PAFFS_TRACE_BUG, "READ INODE operation of invalid area at %d:%d", extractLogicalArea(inode->direct[page + pageFrom]),extractPage(inode->direct[page + pageFrom]));
			return PAFFS_BUG;
		}

		if(dev->areaMap[extractLogicalArea(inode->direct[page + pageFrom])].areaSummary[extractPage(inode->direct[page + pageFrom])] == DIRTY){
			PAFFS_DBG(PAFFS_TRACE_BUG, "READ INODE operation of outdated (dirty) data at %d:%d", extractLogicalArea(inode->direct[page + pageFrom]),extractPage(inode->direct[page + pageFrom]));
			return PAFFS_BUG;
		}

		unsigned long long addr = getPageNumber(inode->direct[page + pageFrom], dev);
		PAFFS_RESULT r = dev->drv.drv_read_page_fn(dev, addr, buf, btr);
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
	activeArea[INDEXAREA] = findWritableArea(INDEXAREA, dev);
	if(paffs_lasterr != PAFFS_OK){
		return paffs_lasterr;
	}

	if(activeArea[INDEXAREA] == 0){
		PAFFS_DBG(PAFFS_TRACE_BUG, "WRITE TREE NODE findWritableArea returned 0");
		return PAFFS_BUG;
	}

	unsigned int firstFreePage = 0;
	if(findFirstFreePage(&firstFreePage, dev, activeArea[INDEXAREA]) == PAFFS_NOSP){
		PAFFS_DBG(PAFFS_TRACE_BUG, "BUG: findWritableArea returned full area (%d).", activeArea[INDEXAREA]);
		return paffs_lasterr = PAFFS_BUG;
	}
	p_addr addr = combineAddress(dev->areaMap[activeArea[INDEXAREA]].position, firstFreePage);
	node->self = addr;

	dev->areaMap[activeArea[INDEXAREA]].areaSummary[firstFreePage] = USED;

	PAFFS_RESULT r = dev->drv.drv_write_page_fn(dev, getPageNumber(node->self, dev), node, sizeof(treeNode));
	if(r != PAFFS_OK)
		return paffs_lasterr = r;

	r = checkActiveAreaFull(dev, &activeArea[INDEXAREA], INDEXAREA);
	if(r != PAFFS_OK)
			return paffs_lasterr = r;

	return PAFFS_OK;
}

PAFFS_RESULT readTreeNode(p_dev* dev, p_addr addr, treeNode* node){
	if(node == NULL){
		PAFFS_DBG(PAFFS_TRACE_BUG, "BUG: treeNode NULL");
				return paffs_lasterr = PAFFS_BUG;
	}
	if(sizeof(treeNode) > dev->param.data_bytes_per_page){
		PAFFS_DBG(PAFFS_TRACE_BUG, "BUG: treeNode bigger than Page (Was %lu, should %u)", sizeof(treeNode), dev->param.data_bytes_per_page);
		return paffs_lasterr = PAFFS_BUG;
	}

	if(dev->areaMap[extractLogicalArea(addr)].areaSummary[extractPage(addr)] == DIRTY){
		PAFFS_DBG(PAFFS_TRACE_ERROR, "READ operation of obsoleted data at %X:%X", extractLogicalArea(addr), extractPage(addr));
		return PAFFS_BUG;
	}

	if(extractLogicalArea(addr) == 0){
		PAFFS_DBG(PAFFS_TRACE_ERROR, "READ TREE NODE operation on (log.) first Area at %X:%X", extractLogicalArea(addr), extractPage(addr));
		return PAFFS_BUG;
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

	//This is constant for given Devices
	unsigned int needed_bytes = sizeof(uint32_t) + sizeof(p_addr) +
		dev->param.areas_no * (sizeof(p_area) - sizeof(p_summaryEntry*))+ // AreaMap without summaryEntry pointer
		2 * dev->param.pages_per_area / 8 /* One bit per entry, two entrys for INDEX and DATA section*/;

	unsigned int needed_pages = needed_bytes / BYTES_PER_PAGE + 1;

	char buf[needed_bytes];
	memset(buf, 0, needed_bytes);
	memcpy(buf, entry, sizeof(uint32_t) + sizeof(p_addr));

	long areaSummaryPositions[2];
	unsigned char pospos = 0;	//Stupid name
	unsigned int pointer = sizeof(uint32_t) + sizeof(p_addr);
	for(unsigned int i = 0; i < dev->param.areas_no; i++){
		if((entry->areaMap[i].type == INDEXAREA || entry->areaMap[i].type == DATAAREA) && entry->areaMap[i].areaSummary != 0){
			areaSummaryPositions[pospos++] = i;
			entry->areaMap[i].has_areaSummary = true;
		}

		memcpy(&buf[pointer], &entry->areaMap[i], sizeof(p_area) - sizeof(p_summaryEntry*));
		//TODO: Optimize bitusage, currently wasting 1,25 Bytes per Entry
		pointer += sizeof(p_area) - sizeof(p_summaryEntry*);
	}

	for(unsigned int i = 0; i < 2; i++){
		for(unsigned int j = 0; j < dev->param.pages_per_area; j++){
			if(entry->areaMap[areaSummaryPositions[i]].areaSummary[j] != DIRTY)
				buf[pointer + j/8] |= 1 << j%8;
		}
		pointer += dev->param.pages_per_area / 8;
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

PAFFS_RESULT readSuperPageIndex(p_dev* dev, p_addr addr, superIndex* entry, bool withAreaMap){
	if(!withAreaMap)
		 return dev->drv.drv_read_page_fn(dev, getPageNumber(addr, dev), entry, sizeof(uint32_t) + sizeof(p_addr));

	if(entry->areaMap == 0)
		return PAFFS_EINVAL;

	//This is constant for given Devices
	unsigned int needed_bytes = sizeof(uint32_t) + sizeof(p_addr) +
		dev->param.areas_no * (sizeof(p_area) - sizeof(p_summaryEntry*))+ // AreaMap without summaryEntry pointer
		2 * dev->param.pages_per_area / 8 /* One bit per entry, two entrys for INDEX and DATA section*/;

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

	memcpy(entry, buf, sizeof(uint32_t) + sizeof(p_addr));

	long areaSummaryPositions[2];
	unsigned char pospos = 0;	//Stupid name
	pointer = sizeof(uint32_t) + sizeof(p_addr);
	for(unsigned int i = 0; i < dev->param.areas_no; i++){
		memcpy(&entry->areaMap[i], &buf[pointer], sizeof(p_area) - sizeof(p_summaryEntry*));
		pointer += sizeof(p_area) - sizeof(p_summaryEntry*);
		if(entry->areaMap[i].has_areaSummary)
			areaSummaryPositions[pospos++] = i;
	}

	unsigned char pagebuf[BYTES_PER_PAGE];
	for(unsigned int i = 0; i < 2; i++){

		if(entry->areaMap[areaSummaryPositions[i]].areaSummary == 0){
			PAFFS_DBG(PAFFS_TRACE_BUG, "Schlawienering some bytes to areaMap");
			//FIXME: This should be taken from static space
			entry->areaMap[areaSummaryPositions[i]].areaSummary = malloc(sizeof(p_summaryEntry) * dev->param.pages_per_area);
		}

		for(unsigned int j = 0; j < dev->param.pages_per_area; j++){
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
		pointer += dev->param.pages_per_area / 8;
	}

	return PAFFS_OK;
}



