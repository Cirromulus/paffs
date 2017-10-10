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
#include <inttypes.h>

namespace paffs{

PageOffs GarbageCollection::countDirtyPages(SummaryEntry* summary){
	PageOffs dirty = 0;
	for(PageOffs i = 0; i < dataPagesPerArea; i++){
		if(summary[i] != SummaryEntry::used)
			dirty++;
	}
	return dirty;
}

//Special Case 'unset': Find any Type and also extremely favour Areas with committed AS
AreaPos GarbageCollection::findNextBestArea(AreaType target, SummaryEntry* out_summary, bool* srcAreaContainsData){
	AreaPos favourite_area = 0;
	PageOffs fav_dirty_pages = 0;
	*srcAreaContainsData = true;
	SummaryEntry curr[dataPagesPerArea];

	//Look for the most dirty area.
	//This ignores unset (free) areas, if we look for data or index areas.
	for(AreaPos i = 0; i < areasNo; i++){
		if(dev->areaMap[i].status != AreaStatus::active &&
				(dev->areaMap[i].type == AreaType::data || dev->areaMap[i].type == AreaType::index)){

			Result r = dev->sumCache.getSummaryStatus(i, curr);
			PageOffs dirty_pages = countDirtyPages(curr);
			if(r != Result::ok){
				PAFFS_DBG(PAFFS_TRACE_BUG, "Could not load Summary of Area %d for Garbage collection!", i);
				return 0;
			}
			if (dirty_pages == dataPagesPerArea){
				//We can't find a block with more dirty pages in it
				favourite_area = i;
				*srcAreaContainsData = false;
				fav_dirty_pages = dirty_pages;
				memcpy(out_summary, curr, dataPagesPerArea);
				if(dev->sumCache.wasASWritten(i))
					//Convenient: we can reset an AS to free up cache space
					break;
			}

			if(target != AreaType::unset){
				//normal case
				if(dev->areaMap[i].type != target)
					continue; 	//We cant change types if area is not completely empty

				if(dirty_pages > fav_dirty_pages ||
						(dev->sumCache.wasASWritten(i) &&
								dirty_pages != 0 && dirty_pages == fav_dirty_pages)){
					favourite_area = i;
					fav_dirty_pages = dirty_pages;
					memcpy(out_summary, curr, dataPagesPerArea);
				}
			}else{
				//Special Case for freeing committed AreaSummaries
				if(dev->sumCache.isCached(i) && dev->sumCache.wasASWritten(i)
						&& dirty_pages >= fav_dirty_pages){
					favourite_area = i;
					fav_dirty_pages = dirty_pages;
					memcpy(out_summary, curr, dataPagesPerArea);
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
	for(unsigned long page = 0; page < dataPagesPerArea; page++){
		if(summary[page] == SummaryEntry::used){
			uint64_t src = dev->areaMap[srcArea].position * totalPagesPerArea + page;
			uint64_t dst = dev->areaMap[dstArea].position * totalPagesPerArea + page;

			char buf[totalBytesPerPage];
			Result r = dev->driver.readPage(src, buf, totalBytesPerPage);
			//Any Biterror gets corrected here by being moved
			if(r != Result::ok && r != Result::biterrorCorrected){
				PAFFS_DBG_S(PAFFS_TRACE_GC, "Could not read page n° %lu!", static_cast<long unsigned> (src));
				return r;
			}
			r = dev->driver.writePage(dst, buf, totalBytesPerPage);
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

/**
 * Changes active Area to one of the new freed areas.
 * Necessary to not have any get/setPageStatus calls!
 * This could lead to a Cache Flush which could itself cause a call on collectGarbage again
 */
Result GarbageCollection::collectGarbage(AreaType targetType){
	SummaryEntry summary[dataPagesPerArea];
	memset(summary, 0xFF, dataPagesPerArea);
	bool srcAreaContainsData = false;
	AreaPos deletion_target = 0;
	Result r;
	AreaPos lastDeletionTarget = 0;

	if(traceMask & PAFFS_TRACE_VERIFY_AS){
		unsigned char buf[totalBytesPerPage];
		for(unsigned i = 0; i < totalPagesPerArea; i++){
			Addr addr = combineAddress(dev->activeArea[AreaType::garbageBuffer], i);
			dev->driver.readPage(getPageNumber(addr, *dev), buf, totalBytesPerPage);
			for(unsigned j = 0; j < totalBytesPerPage; j++){
				if(buf[j] != 0xFF){
					PAFFS_DBG(PAFFS_TRACE_BUG, "Garbage buffer "
							"on %" PRIu32 " is not empty!", dev->areaMap[dev->activeArea[AreaType::garbageBuffer]].position);
					return Result::bug;
				}
			}
		}
	}
	while(1){
		deletion_target = findNextBestArea(targetType, summary, &srcAreaContainsData);
		if(deletion_target == 0){
			PAFFS_DBG_S(PAFFS_TRACE_GC, "Could not find any GC'able pages for type %s!", areaNames[targetType]);

			//TODO: Only use this Mode for the "higher needs", i.e. unmounting.
			//might as well be for INDEX also, as tree is cached and needs to be
			//committed even for read operations.

			if(targetType != AreaType::index){
				PAFFS_DBG_S(PAFFS_TRACE_GC, "And we use reserved Areas for INDEX only.");
				return Result::nospace;
			}

			if(dev->usedAreas <= areasNo){
				PAFFS_DBG_S(PAFFS_TRACE_GC, "and have no reserved Areas left.");
				return Result::nospace;
			}

			//This happens if we couldn't erase former srcArea which was not empty
			//The last resort is using our protected GC_BUFFER block...
			PAFFS_DBG_S(PAFFS_TRACE_GC, "GC did not find next place for GC_BUFFER! "
					"Using reserved Areas.");

			AreaPos nextPos;
			for(nextPos = 0; nextPos < areasNo; nextPos++){
				if(dev->areaMap[nextPos].status == AreaStatus::empty){
					dev->areaMap[nextPos].type = targetType;
					dev->areaMgmt.initArea(nextPos);
					PAFFS_DBG_S(PAFFS_TRACE_AREA, "Found empty Area %u", nextPos);
				}
			}
			if(nextPos == areasNo){
				PAFFS_DBG(PAFFS_TRACE_BUG, "Used Areas said we had space left (%" PRIu32 " areas), "
						"but no empty area was found!", dev->usedAreas);
				return Result::bug;
			}

			/* If lastArea contained data, it is already copied to gc_buffer. 'summary' is untouched and valid.
			 * It it did not contain data (or this is the first round), 'summary' contains {SummaryEntry::free}.
			 */
			if(lastDeletionTarget == 0){
				//this is first round, without having something deleted.
				//Just init and return nextPos.
				dev->activeArea[AreaType::index] = nextPos;
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
			if(r != Result::ok){
				PAFFS_DBG(PAFFS_TRACE_VERIFY_AS, "Could not verify AreaSummary of area %d!", deletion_target);
			}
			if(memcmp(summary, tmp, dataPagesPerArea) != 0){
				PAFFS_DBG(PAFFS_TRACE_BUG, "Summary of findNextBestArea is different to actual areaSummary");
			}
		}

		if(traceMask & PAFFS_TRACE_VERIFY_AS){
			for(unsigned int j = 0; j < dataPagesPerArea; j++){
				if(summary[j] > SummaryEntry::dirty)
					PAFFS_DBG(PAFFS_TRACE_BUG, "Summary of %u contains invalid Entries!", j);
			}
		}

		//TODO: more Safety switches like comparison of lastDeletion targetType

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
			dev->areaMgmt.deleteAreaContents(deletion_target);
			//Copy the updated (no SummaryEntry::dirty pages) summary to the deletion_target (it will be the fresh area!)
			r = dev->sumCache.setSummaryStatus(deletion_target, summary);
			if(r != Result::ok){
				PAFFS_DBG_S(PAFFS_TRACE_ERROR, "Could not remove dirty entries in AS of area %d", deletion_target);
				return r;
			}
			//Notify for used Pages
			if(targetType != AreaType::unset){
				//Safe, because we can assume deletion targetType is same Type as we want (from getNextBestArea)
				dev->areaMap[deletion_target].status = AreaStatus::active;
			}
		}else{
			dev->areaMgmt.deleteArea(deletion_target);
		}

		//TODO: Maybe delete more available blocks. Mark them as UNSET+EMPTY
		break;
	}

	//swap logical position of areas to keep addresses valid
	AreaPos tmp = dev->areaMap[deletion_target].position;
	dev->areaMap[deletion_target].position = dev->areaMap[dev->activeArea[AreaType::garbageBuffer]].position;
	dev->areaMap[dev->activeArea[AreaType::garbageBuffer]].position = tmp;
	//swap erasecounts to let them point to the physical position
	PageOffs tmp2 = dev->areaMap[deletion_target].erasecount;
	dev->areaMap[deletion_target].erasecount = dev->areaMap[dev->activeArea[AreaType::garbageBuffer]].erasecount;
	dev->areaMap[dev->activeArea[AreaType::garbageBuffer]].erasecount = tmp2;

	if(targetType != AreaType::unset){
		//This assumes that current activearea is closed...
		if(dev->activeArea[targetType] != 0){
			PAFFS_DBG(PAFFS_TRACE_BUG, "old active Area (%" PRIu32 " on %" PRIu32 ") is not closed!",
					dev->activeArea[targetType], dev->areaMap[dev->activeArea[targetType]].position);
			return Result::bug;
		}
		dev->activeArea[targetType] = deletion_target;
	}

	PAFFS_DBG_S(PAFFS_TRACE_GC_DETAIL, "Garbagecollection erased pos %u and gave area %u pos %u.", dev->areaMap[dev->activeArea[AreaType::garbageBuffer]].position, dev->activeArea[targetType], dev->areaMap[dev->activeArea[targetType]].position);

	return Result::ok;
}

}
