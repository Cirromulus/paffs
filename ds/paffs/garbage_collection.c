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

area_pos_t findNextBestArea(p_dev* dev, p_areaType target, p_summaryEntry* out_summary, bool* srcAreaContainsData){
	area_pos_t favourite_area = 0;
	uint32_t fav_dirty_pages = 0;
	*srcAreaContainsData = true;
	p_summaryEntry tmp[dev->param.data_pages_per_area];
	p_summaryEntry* curr = out_summary;

	//Look for the most dirty block
	for(area_pos_t i = 0; i < dev->param.areas_no; i++){
		if(dev->areaMap[i].status == CLOSED && (dev->areaMap[i].type == DATAAREA || dev->areaMap[i].type == INDEXAREA)){
			if(dev->areaMap[i].areaSummary == NULL){
				PAFFS_RESULT r = readAreasummary(dev, i, tmp, false);
				if(r != PAFFS_OK){
					PAFFS_DBG(PAFFS_TRACE_BUG,"Could not read areaSummary for GC!");
					paffs_lasterr = r;
					return (area_pos_t)0;
				}
				curr = tmp;
			}else{
				curr = dev->areaMap[i].areaSummary;
			}

			if(paffs_trace_mask & PAFFS_TRACE_VERIFY_AS){
				for(unsigned int j = 0; j < dev->param.data_pages_per_area; j++){
					if(curr[j] > DIRTY)
						PAFFS_DBG(PAFFS_TRACE_BUG, "Summary of %u contains invalid Entries!", j);
				}
			}

			uint32_t dirty_pages = countDirtyPages(dev, curr);
			if (dirty_pages == dev->param.data_pages_per_area){
				//We can't find a block with more dirty pages in it
				favourite_area = i;
				//fav_dirty_pages = dirty_pages; not needed
				*srcAreaContainsData = false;
				memcpy(out_summary, curr, dev->param.data_pages_per_area);
				break;
			}

			if(dev->areaMap[i].type != target)
				continue; 	//We cant change types yet if area is not completely empty

			if(dirty_pages > fav_dirty_pages){
				favourite_area = i;
				fav_dirty_pages = dirty_pages;
				memcpy(out_summary, curr, dev->param.data_pages_per_area);
			}

		}
	}

	return favourite_area;
}

/**
 * @param summary is input and output (with changed DIRTY to FREE)
 */
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
		}else{
			summary[page] = FREE;
		}
	}
	return PAFFS_OK;
}

PAFFS_RESULT deleteArea(p_dev* dev, area_pos_t area){
	for(int i = 0; i < dev->param.blocks_per_area; i++){
		PAFFS_RESULT r = dev->drv.drv_erase_fn(dev, dev->areaMap[area].position*dev->param.blocks_per_area + i);
		if(r != PAFFS_OK){
			PAFFS_DBG_S(PAFFS_TRACE_GC, "Could not delete block n° %u (Area %u)!", dev->areaMap[area].position*dev->param.blocks_per_area + i, area);
			retireArea(dev, area);
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
PAFFS_RESULT collectGarbage(p_dev* dev, p_areaType targetType){
	p_summaryEntry summary[dev->param.data_pages_per_area];
	memset(summary, 0, dev->param.data_pages_per_area);
	bool srcAreaContainsData = false;
	bool desperateMode = dev->activeArea[GARBAGE_BUFFER] == 0;	//If we have no GARBAGE_BUFFER left
	area_pos_t deletion_target = 0;
	PAFFS_RESULT r;

	if(paffs_trace_mask & PAFFS_TRACE_VERIFY_AS){
		memset(summary, 0xFF, dev->param.data_pages_per_area);
	}

	if(desperateMode){
		/*TODO: The last Straw.
		 * If we find a completely dirty block
		 * that can be successfully erased
		 * AND we find another erasable arbitrary block,
		 * we can escape desperate mode restoring a Garbage buffer.
		 */

		PAFFS_DBG(PAFFS_TRACE_GC, "GC is in desperate mode! Recovery is not implemented.");
		return PAFFS_NOSP;
	}

	area_pos_t lastDeletionTarget = 0;
	while(1){
		deletion_target = findNextBestArea(dev, targetType, summary, &srcAreaContainsData);
		if(deletion_target == 0){
			PAFFS_DBG_S(PAFFS_TRACE_GC, "Could not find any GC'able pages for type %s!", area_names[targetType]);

			if(desperateMode){
				PAFFS_DBG_S(PAFFS_TRACE_GC, "... and additionally we already gave up GC_BUFFER!");
				return PAFFS_NOSP;
			}

			//This happens if we couldn't erase former srcArea which was not empty
			//The last resort is using our protected GC_BUFFER block...
			PAFFS_DBG_S(PAFFS_TRACE_GC, "GC did not find next place for GC_BUFFER! Reutilizing BUFFER as last resort.");
			desperateMode = true;

			/* If lastArea contained data, it is already copied to gc_buffer. 'summary' is untouched and valid.
			 * It it did not contain data (or this is the first round), 'summary' contains {FREE}.
			 */
			if(lastDeletionTarget == 0){
				//this is first round, no possible chunks found.
				//Just init and return garbageBuffer.
				dev->areaMap[dev->activeArea[GARBAGE_BUFFER]].type = targetType;
				initArea(dev, dev->activeArea[GARBAGE_BUFFER]);
				dev->activeArea[targetType] = dev->activeArea[GARBAGE_BUFFER];

				dev->activeArea[GARBAGE_BUFFER] = 0;	//No GC_BUFFER left
				return PAFFS_OK;
			}

			//Resurrect area, fill it with the former summary. In end routine, positions will be swapped.
			//TODO: former summary may be incomplete...
			dev->areaMap[lastDeletionTarget].type = targetType;
			initArea(dev, lastDeletionTarget);
			memcpy(dev->areaMap[lastDeletionTarget].areaSummary, summary, dev->param.data_pages_per_area);
			deletion_target = lastDeletionTarget;

			break;
		}

		if(paffs_trace_mask & PAFFS_TRACE_VERIFY_AS){
			//Just for debug, in production AS might be invalid and summary may be incomplete
			if(memcmp(summary, dev->areaMap[deletion_target].areaSummary, dev->param.data_pages_per_area) != 0){
				PAFFS_DBG(PAFFS_TRACE_BUG, "Summary of findNextBestArea is different to actual areaSummary");
			}
		}

		if(paffs_trace_mask & PAFFS_TRACE_VERIFY_AS){
			for(unsigned int j = 0; j < dev->param.data_pages_per_area; j++){
				if(summary[j] > DIRTY)
					PAFFS_DBG(PAFFS_TRACE_BUG, "Summary of %u contains invalid Entries!", j);
			}
		}

		/*TODO: more Safety switches like comparison of lastDeletion targetType

		if(desperateMode && srcAreaContainsData){
			PAFFS_DBG(PAFFS_TRACE_GC, "GC cant copy valid data in desperate mode! Giving up.");
			return PAFFS_NOSP;
		}*/

		lastDeletionTarget = deletion_target;

		if(srcAreaContainsData){
			//still some valid data, copy to new area
			PAFFS_DBG_S(PAFFS_TRACE_GC_DETAIL, "GC found just partially clean area %u on pos %u", deletion_target, dev->areaMap[deletion_target].position);

			r = moveValidDataToNewArea(dev, deletion_target, dev->activeArea[GARBAGE_BUFFER], summary);
			//while(getchar() == EOF);
			if(r != PAFFS_OK){
				PAFFS_DBG_S(PAFFS_TRACE_GC, "Could not copy valid pages from area %u to %u!", deletion_target, dev->activeArea[GARBAGE_BUFFER]);
				//TODO: Handle something, maybe put area in ReadOnly or copy somewhere else..
				//TODO: Maybe copy rest of Pages before quitting
				return r;
			}
			//Copy the updated (no DIRTY pages) summary to the deletion_target (it will be the fresh area!)
			memcpy(dev->areaMap[deletion_target].areaSummary, summary, dev->param.data_pages_per_area);
			//Notify for used Pages
			dev->areaMap[deletion_target].status = ACTIVE;	//Safe, because we can assume deletion targetType is same Type as we want (from getNextBestArea)
		}else{
			//This is not necessary because write function handles empty areas by itself
			//				memset(dev->areaMap[deletion_target].areaSummary, 0, dev->param.data_pages_per_area);
			dev->areaMap[deletion_target].status = EMPTY;
		}

		//Delete old area
		r = deleteArea(dev, deletion_target);
		dev->areaMap[deletion_target].erasecount++;
		if(r == PAFFS_BADFLASH){
			PAFFS_DBG_S(PAFFS_TRACE_GC, "Could not delete block in area %u on position %u! Retiring Area...", deletion_target, dev->areaMap[deletion_target].position);
			if(paffs_trace_mask && (PAFFS_TRACE_AREA | PAFFS_TRACE_GC_DETAIL)){
				printf("Info: \n");
				for(int i = 0; i < dev->param.areas_no; i++){
					printf("\tArea %d on %u as %10s with %u erases\n", i, dev->areaMap[i].position, area_names[dev->areaMap[i].type], dev->areaMap[i].erasecount);
				}
			}
		}else if(r != PAFFS_OK){
			//Something unexpected happened
			//TODO: Clean up
			return r;
		}else{
			//we succeeded
			//TODO: Maybe delete more available blocks. Mark them as UNSET+EMPTY
			break;
		}
	}



	//swap logical position of areas to keep addresses valid
	area_pos_t tmp = dev->areaMap[deletion_target].position;
	dev->areaMap[deletion_target].position = dev->areaMap[dev->activeArea[GARBAGE_BUFFER]].position;
	dev->areaMap[dev->activeArea[GARBAGE_BUFFER]].position = tmp;
	//swap erasecounts to let them point to the physical position
	uint32_t tmp2 = dev->areaMap[deletion_target].erasecount;
	dev->areaMap[deletion_target].erasecount = dev->areaMap[dev->activeArea[GARBAGE_BUFFER]].erasecount;
	dev->areaMap[dev->activeArea[GARBAGE_BUFFER]].erasecount = tmp2;

	if(desperateMode){
		//now former retired section became garbage buffer, retire it officially.
		retireArea(dev, dev->activeArea[GARBAGE_BUFFER]);
		dev->activeArea[GARBAGE_BUFFER] = 0;
		if(paffs_trace_mask && (PAFFS_TRACE_AREA | PAFFS_TRACE_GC_DETAIL)){
			printf("Info: \n");
			for(int i = 0; i < dev->param.areas_no; i++){
				printf("\tArea %d on %u as %10s with %u erases\n", i, dev->areaMap[i].position, area_names[dev->areaMap[i].type], dev->areaMap[i].erasecount);
			}
		}
	}

	dev->activeArea[targetType] = deletion_target;

	PAFFS_DBG_S(PAFFS_TRACE_GC_DETAIL, "Garbagecollection erased pos %u and gave area %u pos %u.", dev->areaMap[dev->activeArea[GARBAGE_BUFFER]].position, dev->activeArea[targetType], dev->areaMap[dev->activeArea[targetType]].position);

	return PAFFS_OK;
}
