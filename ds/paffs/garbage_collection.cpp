/*
 * garbage_collection.c
 *
 *  Created on: 28.12.2016
 *      Author: urinator
 */

#include "garbage_collection.hpp"
#include "paffs_flash.hpp"
#include "driver/driver.hpp"

namespace paffs{

/**
 * It could be possible that closed area contains free pages which count as
 * dirty in this case.
 */
uint32_t countDirtyPages(Dev* dev, SummaryEntry* summary){
	uint32_t dirty = 0;
	for(uint32_t i = 0; i < dev->param->data_pages_per_area; i++){
		if(summary[i] != SummaryEntry::used)
			dirty++;
	}
	return dirty;
}

AreaPos findNextBestArea(Dev* dev, AreaType target, SummaryEntry* out_summary, bool* srcAreaContainsData){
	AreaPos favourite_area = 0;
	uint32_t fav_dirty_pages = 0;
	*srcAreaContainsData = true;
	SummaryEntry tmp[dev->param->data_pages_per_area];
	SummaryEntry* curr = out_summary;

	//Look for the most dirty block
	for(AreaPos i = 0; i < dev->param->areas_no; i++){
		if(dev->areaMap[i].status == AreaStatus::closed && (dev->areaMap[i].type == AreaType::dataarea || dev->areaMap[i].type == AreaType::indexarea)){
			if(dev->areaMap[i].areaSummary == NULL){
				Result r = readAreasummary(dev, i, tmp, false);
				if(r != Result::ok){
					PAFFS_DBG(PAFFS_TRACE_BUG,"Could not read areaSummary for GC!");
					lasterr = r;
					return (AreaPos)0;
				}
				curr = tmp;
			}else{
				curr = dev->areaMap[i].areaSummary;
			}

			if(trace_mask & PAFFS_TRACE_VERIFY_AS){
				for(unsigned int j = 0; j < dev->param->data_pages_per_area; j++){
					if(curr[j] > SummaryEntry::dirty)
						PAFFS_DBG(PAFFS_TRACE_BUG, "Summary of %u contains invalid Entries!", j);
				}
			}

			uint32_t dirty_pages = countDirtyPages(dev, curr);
			if (dirty_pages == dev->param->data_pages_per_area){
				//We can't find a block with more dirty pages in it
				favourite_area = i;
				//fav_dirty_pages = dirty_pages; not needed
				*srcAreaContainsData = false;
				memcpy(out_summary, curr, dev->param->data_pages_per_area);
				break;
			}

			if(dev->areaMap[i].type != target)
				continue; 	//We cant change types yet if area is not completely empty

			if(dirty_pages > fav_dirty_pages){
				favourite_area = i;
				fav_dirty_pages = dirty_pages;
				memcpy(out_summary, curr, dev->param->data_pages_per_area);
			}

		}
	}

	return favourite_area;
}

/**
 * @param summary is input and output (with changed SummaryEntry::dirty to SummaryEntry::free)
 */
Result moveValidDataToNewArea(Dev* dev, AreaPos srcArea, AreaPos dstArea, SummaryEntry* summary){
	for(unsigned long page = 0; page < dev->param->data_pages_per_area; page++){
		if(summary[page] == SummaryEntry::used){
			uint64_t src = dev->areaMap[srcArea].position * dev->param->total_pages_per_area + page;
			uint64_t dst = dev->areaMap[dstArea].position * dev->param->total_pages_per_area + page;

			char buf[dev->param->total_bytes_per_page];
			Result r = dev->driver->readPage(src, buf, dev->param->total_bytes_per_page);
			if(r != Result::ok){
				PAFFS_DBG_S(PAFFS_TRACE_GC, "Could not read page n° %lu!", (long unsigned) src);
				return r;
			}
			r = dev->driver->writePage(dst, buf, dev->param->total_bytes_per_page);
			if(r != Result::ok){
				PAFFS_DBG_S(PAFFS_TRACE_GC, "Could not write page n° %lu!", (long unsigned) dst);
				return Result::badflash;
			}
		}else{
			summary[page] = SummaryEntry::free;
		}
	}
	return Result::ok;
}

Result deleteArea(Dev* dev, AreaPos area){
	for(unsigned int i = 0; i < dev->param->blocks_per_area; i++){
		Result r = dev->driver->eraseBlock(dev->areaMap[area].position*dev->param->blocks_per_area + i);
		if(r != Result::ok){
			PAFFS_DBG_S(PAFFS_TRACE_GC, "Could not delete block n° %u (Area %u)!", dev->areaMap[area].position*dev->param->blocks_per_area + i, area);
			retireArea(dev, area);
			return Result::badflash;
		}
	}
	return Result::ok;
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
Result collectGarbage(Dev* dev, AreaType targetType){
	SummaryEntry summary[dev->param->data_pages_per_area];
	memset(summary, 0, dev->param->data_pages_per_area);
	bool srcAreaContainsData = false;
	bool desperateMode = dev->activeArea[AreaType::garbage_buffer] == 0;	//If we have no AreaType::garbage_buffer left
	AreaPos deletion_target = 0;
	Result r;

	if(trace_mask & PAFFS_TRACE_VERIFY_AS){
		memset(summary, 0xFF, dev->param->data_pages_per_area);
	}

	if(desperateMode){
		/*TODO: The last Straw.
		 * If we find a completely dirty block
		 * that can be successfully erased
		 * AND we find another erasable arbitrary block,
		 * we can escape desperate mode restoring a Garbage buffer.
		 */

		PAFFS_DBG(PAFFS_TRACE_GC, "GC is in desperate mode! Recovery is not implemented.");
		return Result::nosp;
	}

	AreaPos lastDeletionTarget = 0;
	while(1){
		deletion_target = findNextBestArea(dev, targetType, summary, &srcAreaContainsData);
		if(deletion_target == 0){
			PAFFS_DBG_S(PAFFS_TRACE_GC, "Could not find any GC'able pages for type %s!", area_names[targetType]);


			if(desperateMode){
				PAFFS_DBG_S(PAFFS_TRACE_GC, "... and additionally we already gave up GC_BUFFER!");
				return Result::nosp;
			}

			//This happens if we couldn't erase former srcArea which was not empty
			//The last resort is using our protected GC_BUFFER block...
			PAFFS_DBG_S(PAFFS_TRACE_GC, "GC did not find next place for GC_BUFFER! Reutilizing BUFFER as last resort.");
			desperateMode = true;

			/* If lastArea contained data, it is already copied to gc_buffer. 'summary' is untouched and valid.
			 * It it did not contain data (or this is the first round), 'summary' contains {SummaryEntry::free}.
			 */
			if(lastDeletionTarget == 0){
				//this is first round, no possible chunks found.
				//Just init and return garbageBuffer.
				dev->areaMap[dev->activeArea[AreaType::garbage_buffer]].type = targetType;
				initArea(dev, dev->activeArea[AreaType::garbage_buffer]);
				dev->activeArea[targetType] = dev->activeArea[AreaType::garbage_buffer];

				dev->activeArea[AreaType::garbage_buffer] = 0;	//No GC_BUFFER left
				return Result::ok;
			}

			//Resurrect area, fill it with the former summary. In end routine, positions will be swapped.
			//TODO: former summary may be incomplete...
			dev->areaMap[lastDeletionTarget].type = targetType;
			initArea(dev, lastDeletionTarget);
			memcpy(dev->areaMap[lastDeletionTarget].areaSummary, summary, dev->param->data_pages_per_area);
			deletion_target = lastDeletionTarget;

			break;
		}

		if(trace_mask & PAFFS_TRACE_VERIFY_AS){
			//Just for debug, in production AS might be invalid and summary may be incomplete
			if(memcmp(summary, dev->areaMap[deletion_target].areaSummary, dev->param->data_pages_per_area) != 0){
				PAFFS_DBG(PAFFS_TRACE_BUG, "Summary of findNextBestArea is different to actual areaSummary");
			}
		}

		if(trace_mask & PAFFS_TRACE_VERIFY_AS){
			for(unsigned int j = 0; j < dev->param->data_pages_per_area; j++){
				if(summary[j] > SummaryEntry::dirty)
					PAFFS_DBG(PAFFS_TRACE_BUG, "Summary of %u contains invalid Entries!", j);
			}
		}

		/*TODO: more Safety switches like comparison of lastDeletion targetType

		if(desperateMode && srcAreaContainsData){
			PAFFS_DBG(PAFFS_TRACE_GC, "GC cant copy valid data in desperate mode! Giving up.");
			return Result::nosp;
		}*/

		lastDeletionTarget = deletion_target;

		if(srcAreaContainsData){
			//still some valid data, copy to new area
			PAFFS_DBG_S(PAFFS_TRACE_GC_DETAIL, "GC found just partially clean area %u on pos %u", deletion_target, dev->areaMap[deletion_target].position);

			r = moveValidDataToNewArea(dev, deletion_target, dev->activeArea[AreaType::garbage_buffer], summary);
			//while(getchar() == EOF);
			if(r != Result::ok){
				PAFFS_DBG_S(PAFFS_TRACE_GC, "Could not copy valid pages from area %u to %u!", deletion_target, dev->activeArea[AreaType::garbage_buffer]);
				//TODO: Handle something, maybe put area in ReadOnly or copy somewhere else..
				//TODO: Maybe copy rest of Pages before quitting
				return r;
			}
			//Copy the updated (no SummaryEntry::dirty pages) summary to the deletion_target (it will be the fresh area!)
			memcpy(dev->areaMap[deletion_target].areaSummary, summary, dev->param->data_pages_per_area);
			//Notify for used Pages
			dev->areaMap[deletion_target].status = AreaStatus::active;	//Safe, because we can assume deletion targetType is same Type as we want (from getNextBestArea)
		}else{
			//This is not necessary because write function handles empty areas by itself
			//				memset(dev->areaMap[deletion_target].areaSummary, 0, dev->param->data_pages_per_area);
			dev->areaMap[deletion_target].status = AreaStatus::empty;
		}

		//Delete old area
		r = deleteArea(dev, deletion_target);
		dev->areaMap[deletion_target].erasecount++;
		if(r == Result::badflash){
			PAFFS_DBG_S(PAFFS_TRACE_GC, "Could not delete block in area %u on position %u! Retiring Area...", deletion_target, dev->areaMap[deletion_target].position);
			if(trace_mask && (PAFFS_TRACE_AREA | PAFFS_TRACE_GC_DETAIL)){
				printf("Info: \n");
				for(unsigned int i = 0; i < dev->param->areas_no; i++){
					printf("\tArea %d on %u as %10s with %u erases\n", i, dev->areaMap[i].position, area_names[dev->areaMap[i].type], dev->areaMap[i].erasecount);
				}
			}
		}else if(r != Result::ok){
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
	AreaPos tmp = dev->areaMap[deletion_target].position;
	dev->areaMap[deletion_target].position = dev->areaMap[dev->activeArea[AreaType::garbage_buffer]].position;
	dev->areaMap[dev->activeArea[AreaType::garbage_buffer]].position = tmp;
	//swap erasecounts to let them point to the physical position
	uint32_t tmp2 = dev->areaMap[deletion_target].erasecount;
	dev->areaMap[deletion_target].erasecount = dev->areaMap[dev->activeArea[AreaType::garbage_buffer]].erasecount;
	dev->areaMap[dev->activeArea[AreaType::garbage_buffer]].erasecount = tmp2;

	if(desperateMode){
		//now former retired section became garbage buffer, retire it officially.
		retireArea(dev, dev->activeArea[AreaType::garbage_buffer]);
		dev->activeArea[AreaType::garbage_buffer] = 0;
		if(trace_mask && (PAFFS_TRACE_AREA | PAFFS_TRACE_GC_DETAIL)){
			printf("Info: \n");
			for(unsigned int i = 0; i < dev->param->areas_no; i++){
				printf("\tArea %d on %u as %10s with %u erases\n", i, dev->areaMap[i].position, area_names[dev->areaMap[i].type], dev->areaMap[i].erasecount);
			}
		}
	}

	dev->activeArea[targetType] = deletion_target;

	PAFFS_DBG_S(PAFFS_TRACE_GC_DETAIL, "Garbagecollection erased pos %u and gave area %u pos %u.", dev->areaMap[dev->activeArea[AreaType::garbage_buffer]].position, dev->activeArea[targetType], dev->areaMap[dev->activeArea[targetType]].position);

	return Result::ok;
}

}
