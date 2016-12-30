/*
 * garbage_collection.c
 *
 *  Created on: 28.12.2016
 *      Author: urinator
 */

#include "garbage_collection.h"
#include "paffs_flash.h"

/**
 * It could be possible that closed area contains free pages which count as
 * dirty in this case.
 */
uint32_t countDirtyPages(p_dev* dev, p_summaryEntry* summary){
	uint32_t dirty = 0;
	for(uint32_t i = 0; i < dev->param.data_pages_per_area; i++){
		if(summary[i] != USED)
			dirty++;
	}
	return dirty;
}

/**
 * TODO: in Worst case scenario, all areas of other areatype contain only
 * one valid page per area and use all remaining space. This way,
 * the other areatype consumes all space without needing it. It would have to
 * be rewritten in terms of crawling through all addresses and changing their target...
 * Costly!
 */
PAFFS_RESULT collectGarbage(p_dev* dev, p_areaType target){
	uint32_t favourite_area = 0;
	uint32_t fav_dirty_pages = 0;
	p_summaryEntry tmp[dev->param.data_pages_per_area];
	p_summaryEntry* entry;

	//Look for the most dirty block
	for(uint32_t i = 0; i < dev->param.areas_no; i++){
		if(dev->areaMap[i].status == CLOSED && (dev->areaMap[i].type == DATAAREA || dev->areaMap[i].type == INDEXAREA)){
			if(dev->areaMap[favourite_area].areaSummary == NULL){
				entry = tmp;
				PAFFS_RESULT r = readAreasummary(dev, i, entry);
				if(r != PAFFS_OK){
					PAFFS_DBG(PAFFS_TRACE_BUG,"Could not read areaSummary for GC!");
					return r;
				}
			}else{
				entry = dev->areaMap[i].areaSummary;
			}

			uint32_t dirty_pages = countDirtyPages(dev, entry);
			if (fav_dirty_pages == dev->param.data_pages_per_area){
				//We can't find a block with more dirty pages in it
				favourite_area = i;
				fav_dirty_pages = dirty_pages;
				break;
			}

			if(dev->areaMap[i].type != target)
				continue; 	//We cant change types yet if area is not completely empty

			if(dirty_pages > fav_dirty_pages){
				favourite_area = i;
				fav_dirty_pages = dirty_pages;
			}

		}
	}

	if(favourite_area == 0){
		PAFFS_DBG(PAFFS_TRACE_GC, "Could not find any GC'able pages for type %s!", area_names[target]);
		return PAFFS_NOSP;
	}

	if(fav_dirty_pages != dev->param.data_pages_per_area){
		//still some valid data, copy to new area
		for(unsigned long page = 0; page < dev->param.data_pages_per_area; i++){
			if(entry[i] == USED){
				uint64_t dst = dev->areaMap[activeArea[GARBAGE_BUFFER]].position * dev->param.total_pages_per_area + i;
				uint64_t src = dev->areaMap[favourite_area].position * dev->param.total_pages_per_area + i;


				//TODO: Manage areasummary of new area
				char buf[dev->param.total_bytes_per_page];
				PAFFS_RESULT r = dev->drv.drv_read_page_fn(dev, src, buf, dev->param.total_bytes_per_page);
				if(r != PAFFS_OK)
					return r;
				r = dev->drv.drv_write_page_fn(dev, dst, buf, dev->param.total_bytes_per_page);
				if(r != PAFFS_OK)
					return r;
			}
		}

	}

	//Delete old area
	for(int i = 0; i < dev->param.blocks_per_area; i++){
		PAFFS_RESULT r = dev->drv.drv_erase_fn(dev, dev->areaMap[favourite_area].position*dev->param.blocks_per_area + i);
		if(r != PAFFS_OK)
			return r;
	}

	if(fav_dirty_pages != dev->param.data_pages_per_area){
		//it contained valid pages
		//TODO: Set area active. Swap areaSummarys
	}else{
		//TODO: Free areaSummary of old
		dev->areaMap[dev->activeArea[GARBAGE_BUFFER]].type = UNSET;
	}

	dev->areaMap[favourite_area].type = GARBAGE_BUFFER;
	dev->activeArea[GARBAGE_BUFFER] = favourite_area;


	return PAFFS_BUG;
}
