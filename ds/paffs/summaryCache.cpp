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

SummaryCache::SummaryCache(Device* dev) : dev(dev){
/*	printf("Ayerg: %d\n", dataPagesPerArea / 4 + 1);
	memset(summaryCache[0], 0, dataPagesPerArea / 4 + 1);
	printf("Status: %d\n", (int)getPackedStatus(0, 0));
	setPackedStatus(0, 0, SummaryEntry::used);
	printf("Status: %d\n", (int)getPackedStatus(0, 0));
	setPackedStatus(0, 0, SummaryEntry::dirty);
	printf("Status: %d\n", (int)getPackedStatus(0, 0));
	setPackedStatus(0, 0, SummaryEntry::error);
	printf("Status: %d\n", (int)getPackedStatus(0, 0));
	setPackedStatus(0, 0, SummaryEntry::free);
	printf("Status: %d\n", (int)getPackedStatus(0, 0));

	for(unsigned int i = 0; i < dataPagesPerArea; i++){
		setPackedStatus(0, i, SummaryEntry::error);
	}

	setDirty(0);
	printf("dirty: %d\n", isDirty(0));*/
	}

SummaryEntry SummaryCache::getPackedStatus(uint16_t position, uint16_t page){
	return (SummaryEntry)((summaryCache[position][page/4] & (0b11 << (page % 4)*2)) >> (page % 4)*2);
}

void SummaryCache::setPackedStatus(uint16_t position, uint16_t page, SummaryEntry value){
	summaryCache[position][page/4] =
			(summaryCache[position][page/4] & ~(0b11 << (page % 4)*2)) |
			((int)value << (page % 4) * 2);
}

bool SummaryCache::isDirty(uint16_t position){
	return summaryCache[position][dataPagesPerArea / 4 + 1] & 0b1;
}

void SummaryCache::setDirty(uint16_t position, bool value){
	if(value)
		summaryCache[position][dataPagesPerArea / 4 + 1] |= 1;
	else
		summaryCache[position][dataPagesPerArea / 4 + 1] &= 0;
}

void SummaryCache::unpackStatusArray(uint16_t position, SummaryEntry* arr){
	for(unsigned int i = 0; i < dev->param->dataPagesPerArea; i++){
		arr[i] = getPackedStatus(position, i);
	}
}

void SummaryCache::packStatusArray(uint16_t position, SummaryEntry* arr){
	for(unsigned int i = 0; i < dev->param->dataPagesPerArea; i++){
		setPackedStatus(position, i, arr[i]);
	}
}

int SummaryCache::findNextFreeCacheEntry(){
	//from summaryCache to AreaPosition
	bool used[areaSummaryCacheSize] = {0};
	for(auto it = translation.cbegin(), end = translation.cend();
			it != end; ++it){
		used[it->second] = true;
	}
	for(int i = 0; i < areaSummaryCacheSize; i++){
		if(!used[i])
			return i;
	}
	return -1;
}

Result SummaryCache::setPageStatus(AreaPos area, uint8_t page, SummaryEntry state){
	if(translation.find(area) == translation.end()){
		int nextEntry = findNextFreeCacheEntry();
		if(nextEntry < 0){
			PAFFS_DBG(PAFFS_TRACE_ERROR, "Could not find free Cache Entry for summaryCache, "
					"and flush is not supported yet");
			return Result::nimpl;
		}
		translation[area] = nextEntry;
		memset(summaryCache[translation[area]], 0, dev->param->dataPagesPerArea / 4 + 1);
		PAFFS_DBG_S(PAFFS_TRACE_CACHE, "Created cache entry for area %d", area);
	}
	if(page > dev->param->dataPagesPerArea){
		PAFFS_DBG(PAFFS_TRACE_BUG, "Tried to access page out of bounds! (was: %d, should: < %d", page, dev->param->dataPagesPerArea);
	}
	setPackedStatus(translation[area], page, state);
	if(state == SummaryEntry::dirty && traceMask & PAFFS_WRITE_VERIFY_AS){
		char* buf = (char*) malloc(dev->param->totalBytesPerPage);
		memset(buf, 0xFF, dev->param->dataBytesPerPage);
		memset(&buf[dev->param->dataBytesPerPage], 0xA0, dev->param->oobBytesPerPage);
		Addr addr = combineAddress(area, page);
		dev->driver->writePage(getPageNumber(addr, dev), buf, dev->param->totalBytesPerPage);
		free(buf);
	}
	return Result::ok;
}

SummaryEntry SummaryCache::getPageStatus(AreaPos area, uint8_t page, Result *result){
	if(translation.find(area) == translation.end()){
		int nextEntry = findNextFreeCacheEntry();
		if(nextEntry < 0){
			PAFFS_DBG(PAFFS_TRACE_ERROR, "Could not find free Cache Entry for summaryCache, "
					"and flush is not supported yet");
			*result = Result::nimpl;
			return SummaryEntry::error;
		}
		translation[area] = nextEntry;
		memset(summaryCache[translation[area]], 0, dev->param->dataPagesPerArea / 4 + 1);
		PAFFS_DBG_S(PAFFS_TRACE_CACHE, "Created cache entry for area %d", area);
	}
	if(page > dev->param->dataPagesPerArea){
		PAFFS_DBG(PAFFS_TRACE_BUG, "Tried to access page out of bounds! (was: %d, should: < %d", page, dev->param->dataPagesPerArea);
		*result = Result::einval;
		return SummaryEntry::error;
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
	SummaryEntry ergebnis = getPackedStatus(translation[area], page);
	return ergebnis;
}

Result SummaryCache::getSummaryStatus(AreaPos area, SummaryEntry* summary){
	if(translation.find(area) == translation.end()){
		int nextEntry = findNextFreeCacheEntry();
		if(nextEntry < 0){
			PAFFS_DBG(PAFFS_TRACE_ERROR, "Could not find free Cache Entry for summaryCache, "
					"and flush is not supported yet");
			return Result::nimpl;
		}
		translation[area] = nextEntry;
		memset(summaryCache[translation[area]], 0, dev->param->dataPagesPerArea / 4 + 1);
		PAFFS_DBG_S(PAFFS_TRACE_CACHE, "Created cache entry for area %d", area);
	}
	unpackStatusArray(translation[area], summary);
	return Result::ok;
}

Result SummaryCache::setSummaryStatus(AreaPos area, SummaryEntry* summary){
	if(translation.find(area) == translation.end()){
		int nextEntry = findNextFreeCacheEntry();
		if(nextEntry < 0){
			PAFFS_DBG(PAFFS_TRACE_ERROR, "Could not find free Cache Entry for summaryCache, "
					"and flush is not supported yet");
			return Result::nimpl;
		}
		translation[area] = nextEntry;
		memset(summaryCache[translation[area]], 0, dev->param->dataPagesPerArea / 4 + 1);
		PAFFS_DBG_S(PAFFS_TRACE_CACHE, "Created cache entry for area %d", area);
	}
	packStatusArray(translation[area], summary);
	return Result::ok;
}

Result SummaryCache::deleteSummary(AreaPos area){
	if(translation.find(area) == translation.end()){
		PAFFS_DBG(PAFFS_TRACE_BUG, "Tried to delete nonexisting Area %d", area);
		return Result::einval;
	}

	translation.erase(area);
	PAFFS_DBG_S(PAFFS_TRACE_CACHE, "Deleted cache entry of area %d", area);
	return Result::ok;
}

Result SummaryCache::loadAreaSummaries(){
	for(AreaPos i = 0; i < 2; i++){
		memset(summaryCache[i], 0, dev->param->dataPagesPerArea*sizeof(SummaryEntry));
	}
	SummaryEntry tmp[2][dataPagesPerArea];
	superIndex index = {0};
	index.areaMap = dev->areaMap;
	index.areaSummary[0] = tmp[0];
	index.areaSummary[1] = tmp[1];
	Result r = dev->superblock.readSuperIndex(&index);
	if(r != Result::ok){
		PAFFS_DBG(PAFFS_TRACE_ERROR, "failed to load Area Summaries!");
		return r;
	}

	for(int i = 0; i < 2; i++){
		if(index.asPositions[i] > 0){
			translation[index.asPositions[i]] = i;
			packStatusArray(i, index.areaSummary[i]);
			dev->activeArea[dev->areaMap[index.asPositions[i]].type] = dev->areaMap[index.asPositions[i]].position;
		}
	}

	return Result::ok;
}

Result SummaryCache::commitAreaSummaries(){
	//TODO: commit all Areas except two of the emptiest


	unsigned char pos = 0;
	SummaryEntry tmp[2][dataPagesPerArea];
	superIndex index = {0};
	index.areaMap = dev->areaMap;
	index.areaSummary[0] = tmp[0];
	index.areaSummary[1] = tmp[1];

	//write the two open AS'es to Superindex
	for (unsigned int i = 0; i < dev->param->areasNo; i++){
		if((dev->areaMap[i].type == AreaType::data || dev->areaMap[i].type == AreaType::index)
				&& dev->areaMap[i].status == AreaStatus::active){
			if(pos >= 2){
				PAFFS_DBG(PAFFS_TRACE_BUG, "More than two active Areas! This is not handled.");
				return Result::nimpl;
			}
			index.asPositions[pos] = i;
			unpackStatusArray(translation[i], index.areaSummary[pos++]);
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
