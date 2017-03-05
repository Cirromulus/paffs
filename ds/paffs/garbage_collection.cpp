/*
 * garbage_collection.c
 *
 *  Created on: 28.12.2016
 *      Author: urinator
 */

#include "garbage_collection.hpp"
#include "driver/driver.hpp"
#include "summaryCache.hpp"
#include "device.hpp"
#include "dataIO.hpp"
#include "area.hpp"

namespace paffs{

uint32_t GarbageCollection::countDirtyPages(SummaryEntry* summary){
	uint32_t dirty = 0;
	for(uint32_t i = 0; i < dev->param->dataPagesPerArea; i++){
		if(summary[i] != SummaryEntry::used)
			dirty++;
	}
	return dirty;
}

//Special Case 'unset': Find any Type and also extremely favour Areas with committed AS
AreaPos GarbageCollection::findNextBestArea(AreaType target, SummaryEntry* out_summary, bool* srcAreaContainsData){
	AreaPos favourite_area = 0;
	uint32_t fav_dirty_pages = 0;
	*srcAreaContainsData = true;
	SummaryEntry curr[dataPagesPerArea];

	//Look for the most dirty block
	for(AreaPos i = 0; i < dev->param->areasNo; i++){
		if(dev->areaMap[i].status == AreaStatus::closed &&
				(dev->areaMap[i].type == AreaType::data || dev->areaMap[i].type == AreaType::index)){

			Result r = dev->sumCache.getSummaryStatus(i, curr);
			uint32_t dirty_pages = countDirtyPages(curr);
			if(r != Result::ok){
				PAFFS_DBG(PAFFS_TRACE_BUG, "Could not load Summary of Area %d for Garbage collection!", i);
				return 0;
			}
			if (dirty_pages == dev->param->dataPagesPerArea){
				//We can't find a block with more dirty pages in it
				favourite_area = i;
				*srcAreaContainsData = false;
				fav_dirty_pages = dirty_pages;
				memcpy(out_summary, curr, dev->param->dataPagesPerArea);

				if(dev->sumCache.wasASWritten(i))
					//Convenient: we can reset an AS to free up cache space
					break;
			}

			if(target != AreaType::unset){
				//normal case
				if(dev->areaMap[i].type != target)
					continue; 	//We cant change types if area is not completely empty

				if(dirty_pages > fav_dirty_pages ||
						(dev->sumCache.wasASWritten(i) && dirty_pages == fav_dirty_pages)){
					favourite_area = i;
					fav_dirty_pages = dirty_pages;
					memcpy(out_summary, curr, dev->param->dataPagesPerArea);
				}
			}else{
				//Special Case for freeing committed AreaSummaries
				if(dev->sumCache.wasASWritten(i) && dirty_pages > fav_dirty_pages){
					favourite_area = i;
					fav_dirty_pages = dirty_pages;
					memcpy(out_summary, curr, dev->param->dataPagesPerArea);
				}
			}

		}
	}

	return favourite_area;
}

/**
 * @param summary is input and output (with changed SummaryEntry::dirty to SummaryEntry::free)
 */
Result GarbageCollection::moveValidDataToNewArea(AreaPos srcArea, AreaPos dstArea, SummaryEntry* summary){
	PAFFS_DBG_S(PAFFS_TRACE_GC_DETAIL, "Moving valid data from Area %u (on %u) to Area %u (on %u)"
	, srcArea, dev->areaMap[srcArea].position, dstArea, dev->areaMap[dstArea].position);
	for(unsigned long page = 0; page < dev->param->dataPagesPerArea; page++){
		if(summary[page] == SummaryEntry::used){
			uint64_t src = dev->areaMap[srcArea].position * dev->param->totalPagesPerArea + page;
			uint64_t dst = dev->areaMap[dstArea].position * dev->param->totalPagesPerArea + page;

			char buf[dev->param->totalBytesPerPage];
			Result r = dev->driver->readPage(src, buf, dev->param->totalBytesPerPage);
			if(r != Result::ok){
				PAFFS_DBG_S(PAFFS_TRACE_GC, "Could not read page n° %lu!", static_cast<long unsigned> (src));
				return r;
			}
			r = dev->driver->writePage(dst, buf, dev->param->totalBytesPerPage);
			if(r != Result::ok){
				PAFFS_DBG_S(PAFFS_TRACE_GC, "Could not write page n° %lu!", static_cast<long unsigned> (dst));
				return Result::badflash;
			}
		}else{
			summary[page] = SummaryEntry::free;
		}
	}
	return Result::ok;
}

Result GarbageCollection::deleteArea(AreaPos area){
	for(unsigned int i = 0; i < dev->param->blocksPerArea; i++){
		Result r = dev->driver->eraseBlock(dev->areaMap[area].position*dev->param->blocksPerArea + i);
		if(r != Result::ok){
			PAFFS_DBG_S(PAFFS_TRACE_GC, "Could not delete block n° %u (Area %u)!", dev->areaMap[area].position*dev->param->blocksPerArea + i, area);
			dev->areaMgmt.retireArea(area);
			return Result::badflash;
		}
	}
	dev->areaMap[area].erasecount++;
	dev->sumCache.resetASWritten(area);
	return Result::ok;
}

/**
 * TODO: in Worst case scenario, all areas of other areatype contain only
 * one valid page per area and use all remaining space. This way,
 * the other areatype consumes all space without needing it. It would have to
 * be rewritten in terms of crawling through all addresses and changing their target...
 * Costly!
 * Possible solution: Unify INDEX and DATA to be written in a single AreaType
 *
 * Changes active Area to one of the new freed areas.
 * Necessary to not have any get/setPageStatus calls!
 * This could lead to a Cache Flush which could itself cause a call on collectGarbage again
 */
Result GarbageCollection::collectGarbage(AreaType targetType){
	SummaryEntry summary[dev->param->dataPagesPerArea];
	memset(summary, 0xFF, dev->param->dataPagesPerArea);
	bool srcAreaContainsData = false;
	bool desperateMode = dev->activeArea[AreaType::garbageBuffer] == 0;	//If we have no AreaType::garbage_buffer left
	AreaPos deletion_target = 0;
	Result r;

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
		deletion_target = findNextBestArea(targetType, summary, &srcAreaContainsData);
		if(deletion_target == 0){
			PAFFS_DBG_S(PAFFS_TRACE_GC, "Could not find any GC'able pages for type %s!", area_names[targetType]);

			//TODO: Only use this Mode for the "higher needs", i.e. unmounting.
			//might as well be for INDEX also, as tree is cached and needs to be
			//committed even for read operations.

			if(targetType != AreaType::index){
				PAFFS_DBG_S(PAFFS_TRACE_GC, "And we reserve the DESPERATE MODE for INDEX only.");
				return Result::nosp;
			}

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
				dev->areaMap[dev->activeArea[AreaType::garbageBuffer]].type = dev->areaMap[deletion_target].type;
				dev->areaMgmt.initArea(dev->activeArea[AreaType::garbageBuffer]);
				dev->activeArea[dev->areaMap[deletion_target].type] = dev->activeArea[AreaType::garbageBuffer];

				dev->activeArea[AreaType::garbageBuffer] = 0;	//No GC_BUFFER left
				return Result::ok;
			}

			//Resurrect area, fill it with the former summary. In end routine, positions will be swapped.
			dev->areaMap[lastDeletionTarget].type = dev->areaMap[deletion_target].type;
			dev->areaMgmt.initArea(lastDeletionTarget);
			r = dev->sumCache.setSummaryStatus(lastDeletionTarget, summary);
			if(r != Result::ok){
				PAFFS_DBG(PAFFS_TRACE_ERROR, "Could not move former Summary to area %d!", lastDeletionTarget);
				return r;
			}
			deletion_target = lastDeletionTarget;
			break;
		}

		if(traceMask & PAFFS_TRACE_VERIFY_AS){
			//Just for debug, in production AS might be invalid and summary may be incomplete
			SummaryEntry tmp[dataPagesPerArea];
			r = dev->sumCache.getSummaryStatus(deletion_target, tmp);
			if(tmp == NULL || r != Result::ok){
				PAFFS_DBG(PAFFS_TRACE_VERIFY_AS, "Could not verify AreaSummary of area %d!", deletion_target);
			}
			if(memcmp(summary, tmp, dev->param->dataPagesPerArea) != 0){
				PAFFS_DBG(PAFFS_TRACE_BUG, "Summary of findNextBestArea is different to actual areaSummary");
			}
		}

		if(traceMask & PAFFS_TRACE_VERIFY_AS){
			for(unsigned int j = 0; j < dev->param->dataPagesPerArea; j++){
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

			r = moveValidDataToNewArea(deletion_target, dev->activeArea[AreaType::garbageBuffer], summary);
			//while(getchar() == EOF);
			if(r != Result::ok){
				PAFFS_DBG_S(PAFFS_TRACE_GC, "Could not copy valid pages from area %u to %u!", deletion_target, dev->activeArea[AreaType::garbageBuffer]);
				//TODO: Handle something, maybe put area in ReadOnly or copy somewhere else..
				//TODO: Maybe copy rest of Pages before quitting
				return r;
			}
			//Copy the updated (no SummaryEntry::dirty pages) summary to the deletion_target (it will be the fresh area!)
			r = dev->sumCache.setSummaryStatus(deletion_target, summary);
			if(r != Result::ok){
				PAFFS_DBG_S(PAFFS_TRACE_ERROR, "Could not remove dirty entries in AS of area %d", deletion_target);
				return r;
			}
			//Notify for used Pages
			if(targetType != AreaType::unset)
				//Safe, because we can assume deletion targetType is same Type as we want (from getNextBestArea)
				dev->areaMap[deletion_target].status = AreaStatus::active;
		}else{
			r = dev->sumCache.deleteSummary(deletion_target);
			if(r != Result::ok){
				PAFFS_DBG_S(PAFFS_TRACE_ERROR, "Could not free AS of area %d", deletion_target);
				return r;
			}
			dev->areaMap[deletion_target].status = AreaStatus::empty;
		}

		//Delete old area
		r = deleteArea(deletion_target);
		if(r == Result::badflash){
			PAFFS_DBG_S(PAFFS_TRACE_GC, "Could not delete block in area %u on position %u! Retired Area.", deletion_target, dev->areaMap[deletion_target].position);
			if(traceMask & (PAFFS_TRACE_AREA | PAFFS_TRACE_GC_DETAIL)){
				printf("Info: \n");
				for(unsigned int i = 0; i < dev->param->areasNo; i++){
					printf("\tArea %d on %u as %10s with %3u erases\n", i, dev->areaMap[i].position, area_names[dev->areaMap[i].type], dev->areaMap[i].erasecount);
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
	dev->areaMap[deletion_target].position = dev->areaMap[dev->activeArea[AreaType::garbageBuffer]].position;
	dev->areaMap[dev->activeArea[AreaType::garbageBuffer]].position = tmp;
	//swap erasecounts to let them point to the physical position
	uint32_t tmp2 = dev->areaMap[deletion_target].erasecount;
	dev->areaMap[deletion_target].erasecount = dev->areaMap[dev->activeArea[AreaType::garbageBuffer]].erasecount;
	dev->areaMap[dev->activeArea[AreaType::garbageBuffer]].erasecount = tmp2;

	if(desperateMode){
		//now former retired section became garbage buffer, retire it officially.
		dev->areaMgmt.retireArea(dev->activeArea[AreaType::garbageBuffer]);
		dev->activeArea[AreaType::garbageBuffer] = 0;
		if(traceMask & (PAFFS_TRACE_AREA | PAFFS_TRACE_GC_DETAIL)){
			printf("Info: \n");
			for(unsigned int i = 0; i < dev->param->areasNo; i++){
				printf("\tArea %d on %u as %10s with %u erases\n", i, dev->areaMap[i].position, area_names[dev->areaMap[i].type], dev->areaMap[i].erasecount);
			}
		}
	}

	if(targetType != AreaType::unset)
		dev->activeArea[targetType] = deletion_target;

	PAFFS_DBG_S(PAFFS_TRACE_GC_DETAIL, "Garbagecollection erased pos %u and gave area %u pos %u.", dev->areaMap[dev->activeArea[AreaType::garbageBuffer]].position, dev->activeArea[targetType], dev->areaMap[dev->activeArea[targetType]].position);

	return Result::ok;
}

}
