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
					unsigned int offs, unsigned int bytes,
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

	for(int page = 0; page <= pageTo - pageFrom; page++){
		bool misaligned = false;
		activeArea = findWritableArea(dev);
		if(paffs_lasterr != PAFFS_OK){
			return paffs_lasterr;
		}
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
		unsigned int btw = bytes;
		if((bytes+pageOffs) > dev->param.data_bytes_per_page){
			btw = (bytes+pageOffs) > (page+1)*dev->param.data_bytes_per_page ?
						dev->param.data_bytes_per_page :
						(bytes+pageOffs) - page*dev->param.data_bytes_per_page;
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

			if((btw < dev->param.data_bytes_per_page &&
				page*dev->param.data_bytes_per_page + btw + offs < inode->size) ||  //End Misaligned
				(pageOffs > 0 && page == 0)){				//Start Misaligned

				//fill write buffer with valid Data
				misaligned = true;
				buf = (char*)malloc(dev->param.data_bytes_per_page);
				memset(buf, 0xFF, dev->param.data_bytes_per_page);

				unsigned int btr = dev->param.data_bytes_per_page;

				if((pageTo+1+page)*dev->param.data_bytes_per_page > inode->size){
					btr = inode->size - dev->param.data_bytes_per_page;
				}

				if(readInodeData(inode, (pageFrom+page)*dev->param.data_bytes_per_page, btr, buf, dev) != PAFFS_OK){
					free(buf);
					return PAFFS_FAIL;
				}
				if(page == 0){
					//Handle pageOffset on first page (could also be last page)
					memcpy(&buf[pageOffs], data, btw);

					btw = btr > (pageOffs + btw) ? btr : pageOffs + btw;
				}else{
					//last Page is misaligned
					memcpy(buf, data, btw);
					btw = dev->param.data_bytes_per_page;
				}

			}

		}
		inode->direct[page+pageFrom] = phyPageNumber;

		PAFFS_RESULT res = dev->drv.drv_write_page_fn(dev, phyPageNumber, buf, btw);

		//while(getchar() == EOF);

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
					unsigned int offs, unsigned int bytes,
					char* data, p_dev* dev){

	if(offs+bytes == 0){
		PAFFS_DBG(PAFFS_TRACE_ERROR, "Read size 0! Bug?");
		return paffs_lasterr = PAFFS_EINVAL;
	}

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

		unsigned int btr = bytes + pageOffs;
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

	}

	if(misaligned) {
		memcpy(data, &wrap[pageOffs], bytes);
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
