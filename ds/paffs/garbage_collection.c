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

area_pos_t findNextBestArea(p_dev* dev, p_areaType target, p_summaryEntry* summary, bool* srcAreaContainsData){
	area_pos_t favourite_area = 0;
	uint32_t fav_dirty_pages = 0;
	*srcAreaContainsData = true;
	p_summaryEntry* tmp = summary;

	//Look for the most dirty block
	for(area_pos_t i = 0; i < dev->param.areas_no; i++){
		if(dev->areaMap[i].status == CLOSED && (dev->areaMap[i].type == DATAAREA || dev->areaMap[i].type == INDEXAREA)){
			if(dev->areaMap[i].areaSummary == NULL){
				summary = tmp;
				PAFFS_RESULT r = readAreasummary(dev, i, summary, false);
				if(r != PAFFS_OK){
					PAFFS_DBG(PAFFS_TRACE_BUG,"Could not read areaSummary for GC!");
					paffs_lasterr = r;
					return (area_pos_t)0;
				}
			}else{
				summary = dev->areaMap[i].areaSummary;
			}

			uint32_t dirty_pages = countDirtyPages(dev, summary);
			if (dirty_pages == dev->param.data_pages_per_area){
				//We can't find a block with more dirty pages in it
				favourite_area = i;
				fav_dirty_pages = dirty_pages;
				*srcAreaContainsData = false;
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
	return favourite_area;
}

PAFFS_RESULT moveValidDataToNewArea(p_dev* dev, area_pos_t srcArea, area_pos_t dstArea, p_summaryEntry* summary){
	for(unsigned long page = 0; page < dev->param.data_pages_per_area; page++){
		if(summary[page] == USED){
			uint64_t src = dev->areaMap[srcArea].position * dev->param.total_pages_per_area + page;
			uint64_t dst = dev->areaMap[dstArea].position * dev->param.total_pages_per_area + page;

			char buf[dev->param.total_bytes_per_page];
			PAFFS_RESULT r = dev->drv.drv_read_page_fn(dev, src, buf, dev->param.total_bytes_per_page);
			if(r != PAFFS_OK){
				PAFFS_DBG_S(PAFFS_TRACE_GC, "Could not read page n° %lu!", src);
				return r;
			}
			r = dev->drv.drv_write_page_fn(dev, dst, buf, dev->param.total_bytes_per_page);
			if(r != PAFFS_OK){
				PAFFS_DBG_S(PAFFS_TRACE_GC, "Could not write page n° %lu!", dst);
				return PAFFS_BADFLASH;
			}

			//copy valid entries of Area Summary to new area
			dev->areaMap[dstArea].areaSummary[page] = USED;
		}
	}
	//swap logical position
	area_pos_t tmp = dev->areaMap[srcArea].position;
	dev->areaMap[srcArea].position = dev->areaMap[dstArea].position;
	dev->areaMap[dstArea].position = tmp;

	return PAFFS_OK;
}

PAFFS_RESULT deleteArea(p_dev* dev, area_pos_t area){
	for(int i = 0; i < dev->param.blocks_per_area; i++){
		PAFFS_RESULT r = dev->drv.drv_erase_fn(dev, dev->areaMap[area].position*dev->param.blocks_per_area + i);
		if(r != PAFFS_OK){
			PAFFS_DBG_S(PAFFS_TRACE_GC, "Could not delete block n° %u (Area %u)!", dev->areaMap[area].position*dev->param.blocks_per_area + i, area);
			dev->areaMap[area].type = RETIRED;
			initArea(dev, area);
			dev->areaMap[area].status = CLOSED;
			return PAFFS_BADFLASH;
		}
	}
	return PAFFS_OK;
}

/**
 * TODO: in Worst case scenario, all areas of other areatype contain only
 * one valid page per area and use all remaining space. This way,
 * the other areatype consumes all space without needing it. It would have to
 * be rewritten in terms of crawling through all addresses and changing their target...
 * Costly!
 *
 * Changes active Area to one of the new freed areas.
 */
PAFFS_RESULT collectGarbage(p_dev* dev, p_areaType target){
	p_summaryEntry summary[dev->param.data_pages_per_area];
	bool srcAreaContainsData = false;
	bool lastAreaContainedData = false;
	bool desperateMode = dev->activeArea[GARBAGE_BUFFER] == 0;	//If we have no GARBAGE_BUFFER left
	area_pos_t favourite_deletion_target = 0;
	PAFFS_RESULT r;

	if(desperateMode){
		/*TODO: The last Straw.
		 * If we find a completely dirty block
		 * that can be successfully erased
		 * AND we find another arbitrary erasable block,
		 * we can escape desperate mode restoring a Garbage buffer.
		 */

		PAFFS_DBG(PAFFS_TRACE_GC, "GC is in desperate mode! Recovery is not implemented.");
		return PAFFS_NOSP;
	}


	while(!lastAreaContainedData){
		favourite_deletion_target = findNextBestArea(dev, target, summary, &srcAreaContainsData);
		if(favourite_deletion_target == 0){
			PAFFS_DBG(PAFFS_TRACE_GC, "Could not find any GC'able pages for type %s!", area_names[target]);
			return PAFFS_NOSP;
		}

		if(lastAreaContainedData && srcAreaContainsData){
			//This happens if we couldn't erase former srcArea which was not empty
			//The last resort is using our protected GC_BUFFER block...
			PAFFS_DBG(PAFFS_TRACE_GC, "GC cant merge two areas containing USED data! Using GARBAGE_BUFFER as last resort.");
			desperateMode = true;
			dev->activeArea[GARBAGE_BUFFER] = 0;
			break;
		}
		lastAreaContainedData = srcAreaContainsData;

		//init new Area to receive potential data
		dev->areaMap[dev->activeArea[GARBAGE_BUFFER]].type = dev->areaMap[favourite_deletion_target].type;
		initArea(dev, dev->activeArea[GARBAGE_BUFFER]);

		if(srcAreaContainsData){
			//still some valid data, copy to new area
			PAFFS_DBG_S(PAFFS_TRACE_GC_DETAIL, "GC just found partially dirty areas, this is a sign of fragmentation :(");
			r = moveValidDataToNewArea(dev, favourite_deletion_target, dev->activeArea[GARBAGE_BUFFER], summary);
			if(r != PAFFS_OK){
				PAFFS_DBG_S(PAFFS_TRACE_GC, "Could not copy valid pages from area %u to %u!", favourite_deletion_target, dev->activeArea[GARBAGE_BUFFER]);
				//TODO: Handle something, maybe put area in ReadOnly or copy somewhere else..
				//TODO: Maybe copy rest of Pages before quitting
				return r;
			}
		}

		//Delete old area
		r = deleteArea(dev, favourite_deletion_target);
		dev->areaMap[favourite_deletion_target].erasecount++;
		if(r == PAFFS_BADFLASH){
			PAFFS_DBG_S(PAFFS_TRACE_GC, "Could not delete block in area %u! Retiring Area...", favourite_deletion_target);
		}else if(r != PAFFS_OK){
			//Something unexpected happened
			return r;
		}else{
			//we succeeded
			//TODO: Maybe repeat loop until "enough" areas are cleaned to lessen the GC calls
			break;
		}
	}

	//Swap area Types
	dev->activeArea[target] = dev->activeArea[GARBAGE_BUFFER];
	if(!desperateMode){
		dev->areaMap[favourite_deletion_target].type = GARBAGE_BUFFER;
		initArea(dev, favourite_deletion_target);	//Deletes old areaSummary, too.
	}
	dev->activeArea[GARBAGE_BUFFER] = favourite_deletion_target;

	PAFFS_DBG_S(PAFFS_TRACE_GC_DETAIL, "Garbagecollection freed Area no %u and activated area %u.", favourite_deletion_target, dev->activeArea[target]);

	return PAFFS_OK;
}
