/*
 * summaryCache.cpp
 *
 *  Created on: 27 Jan 2017
 *      Author: Pascal Pieper
 */

#include "area.hpp"
#include "device.hpp"
#include "superblock.hpp"
#include "summaryCache.hpp"
#include "driver/driver.hpp"

namespace paffs {

int SummaryCache::findNextFreeCacheEntry(){
	//from summaryCache to AreaPosition
	bool used[areaSummaryCacheSize] = {0};
	for(int i = 0; i < areaSummaryCacheSize; i++){
		if(translation[i] >= 0)
			used[translation[i]] = true;
	}
	for(int i = 0; i < areaSummaryCacheSize; i++){
		if(!used[i])
			return i;
	}
	return -1;
}

Result SummaryCache::setPageStatus(AreaPos area, uint8_t page, SummaryEntry state){
	if(areaSummaryCacheSize < area){
		PAFFS_DBG(PAFFS_TRACE_BUG, "Areasummarycache is too small for current implementation");
		return Result::nimpl;
	}
	if(translation[area] <= -1){
		translation[area] = findNextFreeCacheEntry();
		if(translation[area] < 0){
			PAFFS_DBG_S(PAFFS_TRACE_ERROR, "Could not find free Cache Entry for summaryCache, "
					"and flush is not supported yet");
			return Result::nimpl;
		}
		memset(summaryCache[translation[area]], 0, dev->param->dataPagesPerArea*sizeof(SummaryEntry));
		PAFFS_DBG_S(PAFFS_TRACE_CACHE, "Created cache entry for area %d", area);
	}
	if(page > dev->param->dataPagesPerArea){
		PAFFS_DBG(PAFFS_TRACE_BUG, "Tried to access page out of bounds! (was: %d, should: < %d", page, dev->param->dataPagesPerArea);
	}
	summaryCache[translation[area]][page] = state;
	return Result::ok;
}

SummaryEntry SummaryCache::getPageStatus(AreaPos area, uint8_t page, Result *result){
	if(areaSummaryCacheSize < area){
		PAFFS_DBG(PAFFS_TRACE_BUG, "Areasummarycache is too small for current implementation");
		*result = Result::nimpl;
		return SummaryEntry::dirty;
	}
	if(translation[area] <= -1){
		translation[area] = findNextFreeCacheEntry();
		if(translation[area] < 0){
			PAFFS_DBG_S(PAFFS_TRACE_ERROR, "Could not find free Cache Entry for summaryCache, "
					"and flush is not supported yet");
			*result = Result::nimpl;
			return SummaryEntry::dirty;
		}
		memset(summaryCache[translation[area]], 0, dev->param->dataPagesPerArea*sizeof(SummaryEntry));
		PAFFS_DBG_S(PAFFS_TRACE_CACHE, "Created cache entry for area %d", area);
	}
	if(page > dev->param->dataPagesPerArea){
		PAFFS_DBG(PAFFS_TRACE_BUG, "Tried to access page out of bounds! (was: %d, should: < %d", page, dev->param->dataPagesPerArea);
	}
/*
	if(trace_mask & PAFFS_TRACE_VERIFY_AS){
		for(unsigned int j = 0; j < dev->param->data_pages_per_area; j++){
			if(curr[j] > SummaryEntry::dirty)
				PAFFS_DBG(PAFFS_TRACE_BUG, "Summary of %u contains invalid Entries!", j);
		}
	}
*/

	*result = Result::ok;
	return summaryCache[translation[area]][page];
}

SummaryEntry* SummaryCache::getSummaryStatus(AreaPos area, Result *result){
	if(areaSummaryCacheSize < area){
		PAFFS_DBG(PAFFS_TRACE_BUG, "Areasummarycache is too small for current implementation");
		*result = Result::nimpl;
		return NULL;
	}
	if(translation[area] <= -1){
		translation[area] = findNextFreeCacheEntry();
		if(translation[area] < 0){
			PAFFS_DBG_S(PAFFS_TRACE_ERROR, "Could not find free Cache Entry for summaryCache, "
					"and flush is not supported yet");
			*result = Result::nimpl;
			return NULL;
		}
		memset(summaryCache[translation[area]], 0, dev->param->dataPagesPerArea*sizeof(SummaryEntry));
		PAFFS_DBG_S(PAFFS_TRACE_CACHE, "Created cache entry for area %d", area);
	}
	*result = Result::ok;
	return summaryCache[translation[area]];
}

Result SummaryCache::setSummaryStatus(AreaPos area, SummaryEntry* summary){
	if(areaSummaryCacheSize < area){
		PAFFS_DBG(PAFFS_TRACE_BUG, "Areasummarycache is too small for current implementation");
		return Result::nimpl;
	}
	if(translation[area] <= -1){
		translation[area] = findNextFreeCacheEntry();
		if(translation[area] < 0){
			PAFFS_DBG_S(PAFFS_TRACE_ERROR, "Could not find free Cache Entry for summaryCache, "
					"and flush is not supported yet");
			return Result::nimpl;
		}
		memset(summaryCache[translation[area]], 0, dev->param->dataPagesPerArea*sizeof(SummaryEntry));
		PAFFS_DBG_S(PAFFS_TRACE_CACHE, "Created cache entry for area %d", area);
	}
	memcpy(summaryCache[translation[area]], summary, dev->param->dataPagesPerArea*sizeof(SummaryEntry));
	return Result::ok;
}

Result SummaryCache::deleteSummary(AreaPos area){
	if(areaSummaryCacheSize < area){
		PAFFS_DBG(PAFFS_TRACE_BUG, "Areasummarycache is too small for current implementation");
		return Result::nimpl;
	}
	if(translation[area] <= -1){
		PAFFS_DBG(PAFFS_TRACE_BUG, "Tried to delete nonexisting Area %d", area);
		return Result::einval;
	}

	translation[area] = -1;
	PAFFS_DBG_S(PAFFS_TRACE_CACHE, "Deleted cache entry of area %d", area);
	return Result::ok;
}

Result SummaryCache::loadAreaSummaries(){
	for(AreaPos i = 0; i < 2; i++){
		memset(summaryCache[i], 0, dev->param->dataPagesPerArea*sizeof(SummaryEntry));
	}
	superIndex index = {0};
	index.areaMap = dev->areaMap;
	index.areaSummary[0] = summaryCache[0];
	index.areaSummary[1] = summaryCache[1];
	Result r = dev->superblock.readSuperIndex(&index);
	if(r != Result::ok){
		PAFFS_DBG(PAFFS_TRACE_ERROR, "failed to load Area Summaries!");
		return r;
	}

	for(int i = 0; i < 2; i++){
		if(index.asPositions[i] > 0){
			translation[index.asPositions[i]] = i;
			dev->activeArea[dev->areaMap[index.asPositions[i]].type] = dev->areaMap[index.asPositions[i]].position;
		}
	}

	return Result::ok;
}

Result SummaryCache::commitAreaSummaries(){
	//TODO: commit all Areas except two of the emptiest


	unsigned char pos = 0;
	superIndex index = {0};
	index.areaMap = dev->areaMap;
	//write the two open AS'es to Superindex
	for (unsigned int i = 0; i < dev->param->areasNo; i++){
		if((dev->areaMap[i].type == AreaType::data || dev->areaMap[i].type == AreaType::index)
				&& dev->areaMap[i].status == AreaStatus::active){
			if(pos >= 2){
				PAFFS_DBG(PAFFS_TRACE_BUG, "More than two active Areas! This is not handled.");
				return Result::nimpl;
			}
			index.asPositions[pos] = i;
			index.areaSummary[pos++] = summaryCache[translation[i]];
		}
	}
	return dev->superblock.commitSuperIndex(&index);
}

Result SummaryCache::writeAreasummary(AreaPos area, SummaryEntry* summary){
	unsigned int needed_bytes = 1 + dev->param->dataPagesPerArea / 8;
	unsigned int needed_pages = 1 + needed_bytes / dev->param->dataBytesPerPage;
	if(needed_pages != dev->param->totalPagesPerArea - dev->param->dataPagesPerArea){
		PAFFS_DBG(PAFFS_TRACE_ERROR, "AreaSummary size differs with formatting infos!");
		return Result::fail;
	}

	char buf[needed_bytes];
	memset(buf, 0, needed_bytes);

	/*Is it really necessary to save 16 bit while slowing down garbage collection?
	 *TODO: Check how cost reduction scales with bigger flashes.
	 *		AreaSummary is without optimization 2 bit per page. 2 Kib per Page would
	 *		allow roughly 1000 pages per Area. Usually big pages come with big Blocks,
	 *		so a Block would be ~500 pages, so an area would be limited to two Blocks.
	 *		Not good.
	 *
	 *		Thought 2: GC just cares if dirty or not. Areasummary says exactly that.
	 *		Win.
	 */
	for(unsigned int j = 0; j < dev->param->dataPagesPerArea; j++){
		if(summary[j] != SummaryEntry::dirty)
			buf[j/8] |= 1 << j%8;
	}

	uint32_t pointer = 0;
	uint64_t page_offs = getPageNumber(combineAddress(area, dev->param->dataPagesPerArea), dev);
	Result r;
	for(unsigned int page = 0; page < needed_pages; page++){
		unsigned int btw = pointer + dev->param->dataBytesPerPage < needed_bytes ? dev->param->dataBytesPerPage
							: needed_bytes - pointer;
		r = dev->driver->writePage(page_offs + page, &buf[pointer], btw);
		if(r != Result::ok)
			return r;

		pointer += btw;
	}
	return Result::ok;
}

//FIXME: readAreasummary is untested, b/c areaSummaries remain in RAM during unmount
Result SummaryCache::readAreasummary(AreaPos area, SummaryEntry* out_summary, bool complete){
	unsigned int needed_bytes = 1 + dev->param->dataPagesPerArea / 8 /* One bit per entry*/;

	unsigned int needed_pages = 1 + needed_bytes / dev->param->dataBytesPerPage;
	if(needed_pages != dev->param->totalPagesPerArea - dev->param->dataPagesPerArea){
		PAFFS_DBG(PAFFS_TRACE_ERROR, "AreaSummary size differs with formatting infos!");
		return Result::fail;
	}

	char buf[needed_bytes];
	memset(buf, 0, needed_bytes);
	uint32_t pointer = 0;
	uint64_t page_offs = getPageNumber(combineAddress(area, dev->param->dataPagesPerArea), dev);
	Result r;
	for(unsigned int page = 0; page < needed_pages; page++){
		unsigned int btr = pointer + dev->param->dataBytesPerPage < needed_bytes ? dev->param->dataBytesPerPage
							: needed_bytes - pointer;
		r = dev->driver->readPage(page_offs + page, &buf[pointer], btr);
		if(r != Result::ok)
			return r;

		pointer += btr;
	}
	//buffer ready
	PAFFS_DBG_S(PAFFS_TRACE_SUPERBLOCK, "SuperIndex Buffer was filled with %u Bytes.", pointer);


	for(unsigned int j = 0; j < dev->param->dataPagesPerArea; j++){
		if(buf[j/8] & 1 << j%8){
			if(complete){
				unsigned char pagebuf[dataBytesPerPage];
				Addr tmp = combineAddress(area, j);
				r = dev->driver->readPage(getPageNumber(tmp, dev), pagebuf, dev->param->dataBytesPerPage);
				if(r != Result::ok)
					return r;
				bool contains_data = false;
				for(unsigned int byte = 0; byte < dev->param->dataBytesPerPage; byte++){
					if(pagebuf[byte] != 0xFF){
						contains_data = true;
						break;
					}
				}
				if(contains_data)
					out_summary[j] = SummaryEntry::used;
				else
					out_summary[j] = SummaryEntry::free;
			}else{
				//This is just a guess b/c we are in incomplete mode.
				out_summary[j] = SummaryEntry::used;
			}
		}else{
			out_summary[j] = SummaryEntry::dirty;
		}
	}

	return Result::ok;
}

}  // namespace paffs
