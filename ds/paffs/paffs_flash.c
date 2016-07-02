/*
 * paffs_flash.c
 *
 *  Created on: 30.06.2016
 *      Author: rooot
 */

#include "paffs_flash.h"
#include <stdlib.h>

static unsigned int activeArea = 0;

unsigned int  findWritableArea(p_dev* dev){
	if(activeArea == 0 || dev->areaMap[activeArea].status == CLOSED){
		for(int try = 1; try <= 2; try++){
			for(int area = 0; area < dev->param.areas_no; area++){
				if(dev->areaMap[area].type != DATAAREA){
					continue;
				}
				if(try == 1){
					if(dev->areaMap[area].status == UNCLOSED){	//unclosed oder closed first?
						activeArea = area;
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
		return activeArea;
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

PAFFS_RESULT writeInodeData(pInode* inode,
					unsigned int offs, unsigned int bytes,
					void* data, p_dev* dev){
	unsigned int pageFrom = offs/dev->param.data_bytes_per_page;
	unsigned int pageTo = (offs + bytes) / dev->param.data_bytes_per_page;
	if(pageTo - pageFrom > 11){
		//Would use first indirection Layer also
		return paffs_lasterr = PAFFS_NIMPL;
	}

	if(offs != 0){
		//todo: check misaligned writes
		return paffs_lasterr = PAFFS_NIMPL;
	}

	for(int page = 0; page < 1 + pageTo - pageFrom; page++){

		activeArea = findWritableArea(dev);
		if(paffs_lasterr != PAFFS_OK){
			return paffs_lasterr;
		}
		if(dev->areaMap[activeArea].status == EMPTY){
			//We'll have to use a fresh area,
			//so generate the areaSummary in Memory
			dev->areaMap[activeArea].status = UNCLOSED;
			dev->areaMap[activeArea].areaSummary = malloc(
					sizeof(p_summaryEntry)
					* dev->param.blocks_per_area
					* dev->param.pages_per_block);
			memset(dev->areaMap[activeArea].areaSummary, 0,
					sizeof(p_summaryEntry)
					* dev->param.blocks_per_area
					* dev->param.pages_per_block);
		}
		unsigned int firstFreePage = 0;
		if(findFirstFreePage(&firstFreePage, dev, activeArea) == PAFFS_NOSP){
			//Area is full!
			//TODO: Check if dirty Pages are inside and
			//garbage collect this instead of just closing it...
			dev->areaMap[activeArea].status = CLOSED;
			//Second try. Normally there would be more, because
			//areas could be full without being closed
			activeArea = findWritableArea(dev);
			if(paffs_lasterr != PAFFS_OK){
				return paffs_lasterr;
			}
			if(findFirstFreePage(&firstFreePage, dev, activeArea) == PAFFS_NOSP)
				return PAFFS_BUG;
		}
		unsigned long long phyPageNumber =
					dev->param.blocks_per_area * activeArea
					* dev->param.pages_per_block
					+ firstFreePage;

		dev->areaMap[activeArea].areaSummary[firstFreePage] = USED;
		inode->direct[page] = phyPageNumber;
		void* buf = &data[page*dev->param.total_bytes_per_page];
		unsigned int btw = bytes;
		if(bytes > dev->param.total_bytes_per_page){
			btw = bytes > (page+1)*dev->param.total_bytes_per_page ?
						dev->param.total_bytes_per_page :
						(page+1)*dev->param.total_bytes_per_page - bytes;
		}
		dev->drv.drv_write_page_fn(dev, phyPageNumber, buf, btw);
	}

	inode->reservedSize = pageTo * dev->param.total_bytes_per_page;
	return PAFFS_OK;
}
PAFFS_RESULT readInodeData(pInode* inode,
					unsigned int offs, unsigned int bytes,
					void* data, p_dev* dev){

	unsigned int pageFrom = offs/dev->param.data_bytes_per_page;
	unsigned int pageTo = (offs + bytes) / dev->param.data_bytes_per_page;

	if(offs != 0){
		//todo: check misaligned reads
		return paffs_lasterr = PAFFS_NIMPL;
	}

	if(pageTo > 11){
		//todo Read indirection Layers
		return paffs_lasterr = PAFFS_NIMPL;
	}

	for(int page = 0; page < 1 + pageTo - pageFrom; page++){
		char* wrap = &data[page*dev->param.data_bytes_per_page];
		unsigned int btr = bytes;
		if(bytes > dev->param.total_bytes_per_page){
			btr = bytes > (page+1)*dev->param.total_bytes_per_page ?
						dev->param.total_bytes_per_page :
						(page+1)*dev->param.total_bytes_per_page - bytes;
		}
		PAFFS_RESULT r = dev->drv.drv_read_page_fn(dev, inode->direct[page], wrap, btr);
		if(r != PAFFS_OK){
			return paffs_lasterr = r;
		}

	}

	return PAFFS_OK;
}

PAFFS_RESULT deleteInodeData(pInode* inode, p_dev* dev){

	return PAFFS_NIMPL;
}
