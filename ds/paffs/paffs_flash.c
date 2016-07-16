/*
 * paffs_flash.c
 *
 *  Created on: 30.06.2016
 *      Author: rooot
 */

#include "paffs_flash.h"
#include <stdlib.h>
#include <string.h>

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
		activeArea = findWritableArea(dev);
		if(paffs_lasterr != PAFFS_OK){
			return paffs_lasterr;
		}

		//Handle Areas
		if(dev->areaMap[activeArea].status == EMPTY){
			//We'll have to use a fresh area,
			//so generate the areaSummary in Memory
			initArea(dev, activeArea);
		}
		unsigned int firstFreePage = 0;
		if(findFirstFreePage(&firstFreePage, dev, activeArea) == PAFFS_NOSP){
			PAFFS_DBG(PAFFS_TRACE_AREA, "Info: Area %d full.", activeArea);
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
			initArea(dev, activeArea);
			if(paffs_lasterr != PAFFS_OK){
				return paffs_lasterr;
			}
			//find fresh Page in new selected Area
			if(findFirstFreePage(&firstFreePage, dev, activeArea) == PAFFS_NOSP)
				return PAFFS_BUG;
		}
		unsigned long long phyPageNumber =
					dev->param.blocks_per_area * activeArea
					* dev->param.pages_per_block
					+ firstFreePage;

		dev->areaMap[activeArea].areaSummary[firstFreePage] = USED;

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
			unsigned long oldArea = inode->direct[page+pageFrom] / (dev->param.pages_per_block
									* dev->param.blocks_per_area);
			unsigned long oldPage = inode->direct[page+pageFrom] % (dev->param.pages_per_block
									* dev->param.blocks_per_area);
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
			}

		}else{
			*bytes_written += btw;
		}
		inode->direct[page+pageFrom] = phyPageNumber;

		PAFFS_RESULT res = dev->drv.drv_write_page_fn(dev, phyPageNumber, buf, btw);

		if(misaligned)
			free(buf);

		PAFFS_DBG(PAFFS_TRACE_WRITE, "DBG: write r.P: %d/%d, phy.P: %llu", page+1, pageTo+1, phyPageNumber);
		if(res != PAFFS_OK){
			PAFFS_DBG(PAFFS_TRACE_ERROR, "ERR: write returned FAIL at phy.P: %llu", phyPageNumber);
			return PAFFS_FAIL;
		}

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
		PAFFS_RESULT r = dev->drv.drv_read_page_fn(dev, inode->direct[page + pageFrom], buf, btr);
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

void initArea(p_dev* dev, unsigned long int area){
	PAFFS_DBG(PAFFS_TRACE_AREA, "Info: Init new Area %d.", activeArea);
	//generate the areaSummary in Memory
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
