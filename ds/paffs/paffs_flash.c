/*
 * paffs_flash.c
 *
 *  Created on: 30.06.2016
 *      Author: rooot
 */

#include "paffs_flash.h"
#include <stdlib.h>
#include <string.h>

unsigned int findWritableArea(p_areaType areaType, p_dev* dev){
	if(activeArea[areaType] == 0 || dev->areaMap[activeArea[areaType]].status == CLOSED){
		for(int try = 1; try <= 2; try++){
			for(int area = 0; area < dev->param.areas_no; area++){
				if(dev->areaMap[area].type != areaType){
					continue;
				}
				if(try == 1){
					if(dev->areaMap[area].status == UNCLOSED){	//unclosed oder closed first?
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

PAFFS_RESULT checkActiveAreaFull(p_dev *dev, unsigned int *area, p_areaType areaType){
	if(dev->areaMap[*area].usedPages == dev->param.pages_per_area){
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
	if(dev->areaMap[*area].usedPages > dev->param.pages_per_area){
		PAFFS_DBG(PAFFS_TRACE_BUG, "BUG: used Pages bigger than actual pagecount (was: %d, should %d)", dev->areaMap[activeArea[DATAAREA]].usedPages, dev->param.pages_per_area);
		return PAFFS_BUG;
	}
	return PAFFS_OK;
}

void initArea(p_dev* dev, unsigned long int area){
	PAFFS_DBG(PAFFS_TRACE_AREA, "Info: Init new Area %lu.", area);
	//generate the areaSummary in Memory
	dev->areaMap[area].status = UNCLOSED;
	dev->areaMap[area].dirtyPages = 0;
	dev->areaMap[area].usedPages = 0;
	dev->areaMap[area].areaSummary = malloc(
			sizeof(p_summaryEntry)
			* dev->param.blocks_per_area
			* dev->param.pages_per_block);
	memset(dev->areaMap[area].areaSummary, 0,
			sizeof(p_summaryEntry)
			* dev->param.blocks_per_area
			* dev->param.pages_per_block);
}


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
			return paffs_lasterr = PAFFS_BUG;
		}
		p_addr pageAddress = combineAddress(dev->areaMap[activeArea[DATAAREA]].position, firstFreePage);

		dev->areaMap[activeArea[DATAAREA]].areaSummary[firstFreePage] = USED;
		dev->areaMap[activeArea[DATAAREA]].usedPages++;


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
			dev->areaMap[oldArea].areaSummary[oldPage] = DIRTY;
			dev->areaMap[oldArea].dirtyPages ++;

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
				*bytes_written += btw;
			}

		}else{
			*bytes_written += btw;
		}
		inode->direct[page+pageFrom] = pageAddress;

		PAFFS_RESULT res = dev->drv.drv_write_page_fn(dev, getPageNumber(pageAddress, dev), buf, btw);

		if(misaligned)
			free(buf);

		PAFFS_DBG(PAFFS_TRACE_WRITE, "DBG: write r.P: %d/%d, phy.P: %llu", page+1, pageTo+1, getPageNumber(pageAddress, dev));
		if(res != PAFFS_OK){
			PAFFS_DBG(PAFFS_TRACE_ERROR, "ERR: write returned FAIL at phy.P: %llu", getPageNumber(pageAddress, dev));
			return PAFFS_FAIL;
		}

		res = checkActiveAreaFull(dev, &activeArea[DATAAREA], DATAAREA);
		if(res != PAFFS_OK)
			return paffs_lasterr = res;

	}

	if((pageTo+1)*dev->param.data_bytes_per_page > inode->size){
		//filesize increased
		inode->reservedSize = (pageTo+1) * dev->param.total_bytes_per_page;
	}
	return PAFFS_OK;
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
		PAFFS_DBG(PAFFS_TRACE_ERROR, "Read bigger than size of object! (was: %d, max: %llu)", offs+bytes, inode->size);
		//TODO: return less bytes_read
		return paffs_lasterr = PAFFS_NIMPL;
	}

	if(pageTo > 11){
		//todo Read indirection Layers
		return paffs_lasterr = PAFFS_NIMPL;
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
			PAFFS_DBG(PAFFS_TRACE_BUG, "READ INODE operation of invalid data at %d:%d", extractLogicalArea(inode->direct[page + pageFrom]),extractPage(inode->direct[page + pageFrom]));
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

PAFFS_RESULT deleteInodeData(pInode* inode, p_dev* dev){

	return PAFFS_NIMPL;
}

//TODO: Save Rootnode's Address in Flash (Superblockarea)
static p_addr rootnode_addr;

void registerRootnode(p_dev* dev, p_addr addr){
	if(addr == 0)
		PAFFS_DBG(PAFFS_TRACE_BUG, "BUG: Tried to set Rootnode to 0");
	rootnode_addr = addr;
}

p_addr getRootnodeAddr(p_dev* dev){
	return rootnode_addr;
}

//Does not change addresses in parent Nodes
PAFFS_RESULT writeTreeNode(p_dev* dev, treeNode* node){
	if(node == NULL){
		PAFFS_DBG(PAFFS_TRACE_BUG, "BUG: treeNode NULL");
				return paffs_lasterr = PAFFS_BUG;
	}
	if(sizeof(treeNode) > dev->param.data_bytes_per_page){
		PAFFS_DBG(PAFFS_TRACE_BUG, "BUG: treeNode bigger than Page (Was %lu, should %u)", sizeof(treeNode), dev->param.data_bytes_per_page);
		return paffs_lasterr = PAFFS_BUG;
	}

	if(node->self != 0){
		//We have to invalidate former position first
		dev->areaMap[extractLogicalArea(node->self)].areaSummary[extractPage(node->self)] = DIRTY;
		dev->areaMap[extractLogicalArea(node->self)].dirtyPages ++;

	}

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

	dev->areaMap[activeArea[INDEXAREA]].usedPages++;
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
		return paffs_lasterr = r;

	if(node->self != addr){
		PAFFS_DBG(PAFFS_TRACE_BUG, "Read Treenode at %X:%X, but its content stated that it was on %X:%X", extractLogicalArea(addr), extractPage(addr), extractLogicalArea(node->self), extractPage(node->self));
		return PAFFS_BUG;
	}

	return PAFFS_OK;
}

PAFFS_RESULT deleteTreeNode(p_dev* dev, treeNode* node){
	dev->areaMap[extractLogicalArea(node->self)].areaSummary[extractPage(node->self)] = DIRTY;
	dev->areaMap[extractLogicalArea(node->self)].dirtyPages ++;
	return PAFFS_OK;
}

