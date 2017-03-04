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

SummaryCache::SummaryCache(Device* mdev) : dev(mdev){
	translation.reserve(areaSummaryCacheSize+1);
}

SummaryEntry SummaryCache::getPackedStatus(uint16_t position, uint16_t page){
	return static_cast<SummaryEntry>((summaryCache[position][page/4] & (0b11 << (page % 4)*2)) >> (page % 4)*2);
}

void SummaryCache::setPackedStatus(uint16_t position, uint16_t page, SummaryEntry value){
	summaryCache[position][page/4] =
			(summaryCache[position][page/4] & ~(0b11 << (page % 4)*2)) |
			(static_cast<int>(value) << (page % 4) * 2);
}

bool SummaryCache::isDirty(uint16_t position){
	return summaryCache[position][dataPagesPerArea / 4 + 1] & 0b1;
}

void SummaryCache::setDirty(uint16_t position, bool value){
	if(value)
		summaryCache[position][dataPagesPerArea / 4 + 1] |= 1;
	else
		summaryCache[position][dataPagesPerArea / 4 + 1] &= ~1;
}

bool SummaryCache::wasASWritten(uint16_t position){
	return summaryCache[position][dataPagesPerArea / 4 + 1] & 0b10;
}

void SummaryCache::setASWritten(uint16_t position, bool value){
	if(value)
			summaryCache[position][dataPagesPerArea / 4 + 1] |= 0b10;
		else
			summaryCache[position][dataPagesPerArea / 4 + 1] &= ~0b10;
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
		if(used[it->second] != false){
			PAFFS_DBG(PAFFS_TRACE_BUG, "Multiple Areas point to same location in Cache!");
			return -1;
		}
		used[it->second] = true;
	}
	for(int i = 0; i < areaSummaryCacheSize; i++){
		if(!used[i])
			return i;
	}
	return -1;
}

int SummaryCache::findLeastProbableUsedCacheEntry(){
	return -1;
}

Result SummaryCache::setPageStatus(AreaPos area, uint8_t page, SummaryEntry state){
	if(translation.find(area) == translation.end()){
		Result r = loadUnbufferedArea(area);
		if(r != Result::ok)
			return r;
	}
	setDirty(translation[area]);
	if(page > dev->param->dataPagesPerArea){
		PAFFS_DBG(PAFFS_TRACE_BUG, "Tried to access page out of bounds! (was: %d, should: < %d", page, dev->param->dataPagesPerArea);
	}
	setPackedStatus(translation[area], page, state);
	if(state == SummaryEntry::dirty && traceMask & PAFFS_WRITE_VERIFY_AS){
		char* buf = new char[dev->param->totalBytesPerPage];
		memset(buf, 0xFF, dev->param->dataBytesPerPage);
		memset(&buf[dev->param->dataBytesPerPage], 0x0A, dev->param->oobBytesPerPage);
		Addr addr = combineAddress(area, page);
		dev->driver->writePage(getPageNumber(addr, dev), buf, dev->param->totalBytesPerPage);
		delete buf;
	}
	return Result::ok;
}

SummaryEntry SummaryCache::getPageStatus(AreaPos area, uint8_t page, Result *result){
	if(translation.find(area) == translation.end()){
		Result r = loadUnbufferedArea(area);
		if(r != Result::ok){
			*result = Result::nimpl;
			return SummaryEntry::error;
		}
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
		//TODO: This one does not have to be copied into Cache
		//		Because it is just for a one-shot of Garbage collection looking for the best area
		Result r = loadUnbufferedArea(area);
		if(r != Result::ok)
			return r;
	}
	unpackStatusArray(translation[area], summary);
	return Result::ok;
}

Result SummaryCache::setSummaryStatus(AreaPos area, SummaryEntry* summary){
	setDirty(translation[area]);
	if(translation.find(area) == translation.end()){
		Result r = loadUnbufferedArea(area);
		if(r != Result::ok)
			return r;
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
	PAFFS_DBG_S(PAFFS_TRACE_ASCACHE, "Deleted cache entry of area %d", area);
	return Result::ok;
}

//For Garbage collection to consider committed AS-Areas before others
bool SummaryCache::wasASWritten(AreaPos area){
	if(translation.find(area) == translation.end()){
		PAFFS_DBG(PAFFS_TRACE_ASCACHE, "Tried to question nonexistent Area %d. This is probably not a bug.", area);
		return false;
	}
	return wasASWritten(translation[area]);
}

//For Garbage collection that has deleted the AS too
void SummaryCache::resetASWritten(AreaPos area){
	if(translation.find(area) == translation.end()){
		PAFFS_DBG(PAFFS_TRACE_ASCACHE, "Tried to reset AS-Record of nonexistent Area %d. This _is_ probably a bug.", area);
		return;
	}
	setASWritten(translation[area], false);
}

Result SummaryCache::loadAreaSummaries(){
	for(AreaPos i = 0; i < 2; i++){
		memset(summaryCache[i], 0, dev->param->dataPagesPerArea / 4 + 1);
	}
	SummaryEntry tmp[2][dataPagesPerArea];
	SuperIndex index = {};
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
	SuperIndex index = {};
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

Result SummaryCache::loadUnbufferedArea(AreaPos area){
	Result r;
	int nextEntry = findNextFreeCacheEntry();
	if(nextEntry < 0){
		r = freeNextBestSummaryCacheEntry();
		if(r != Result::ok)
			return r;
		nextEntry = findNextFreeCacheEntry();
		if(nextEntry < 0){
			PAFFS_DBG(PAFFS_TRACE_BUG, "freeNextBestSummaryCacheEntry did not free space!");
			return Result::bug;
		}
	}
	//TODO: Abstractify loading of AreaSumary to give the ability
	//		to load into other destinations than Cache
	translation[area] = nextEntry;
	SummaryEntry buf[dataPagesPerArea];
	r = readAreasummary(area, buf, true);
	if(r == Result::ok){
		packStatusArray(translation[area], buf);
		PAFFS_DBG_S(PAFFS_TRACE_AREA, "Loaded existing AreaSummary of %d to cache", area);
	}
	else if(r == Result::nf){
		memset(summaryCache[translation[area]], 0, dev->param->dataPagesPerArea / 4 + 1);
		PAFFS_DBG_S(PAFFS_TRACE_AREA, "Loaded new AreaSummary for %d", area);
	}
	else
		return r;

	PAFFS_DBG_S(PAFFS_TRACE_ASCACHE, "Created cache entry for area %d", area);
	return Result::ok;
}

Result SummaryCache::freeNextBestSummaryCacheEntry(){
	//from summaryCache to AreaPosition
	bool used[areaSummaryCacheSize] = {0};
	AreaPos pos[areaSummaryCacheSize] = {0};
	bool deletedSomething = false;
	for(auto it = translation.cbegin(), end = translation.cend();
			it != end; ++it){
		if(used[it->second] != false){
			PAFFS_DBG(PAFFS_TRACE_BUG, "Multiple Areas point to same location in Cache!");
			return Result::bug;
		}
		used[it->second] = true;
		pos[it->second] = it->first;
	}
	for(int i = 0; i < areaSummaryCacheSize; i++){
		if(used[i] && !isDirty(i)){
			translation.erase(pos[i]);
			PAFFS_DBG_S(PAFFS_TRACE_ASCACHE, "Deleted cache entry of area %d", pos[i]);
			deletedSomething = true;
		}
	}
	if(deletedSomething)
		return Result::ok;
	PAFFS_DBG(PAFFS_TRACE_ERROR, "Could not find free Cache Entry for summaryCache, "
			"and flush is not supported yet");

	return Result::nimpl;
}

Result SummaryCache::writeAreasummary(AreaPos area, SummaryEntry* summary){
	char buf[areaSummarySize];
	memset(buf, 0, areaSummarySize);
	unsigned int needed_pages = 1 + areaSummarySize / dataBytesPerPage;
	if(needed_pages != dev->param->totalPagesPerArea - dev->param->dataPagesPerArea){
		PAFFS_DBG(PAFFS_TRACE_ERROR, "AreaSummary size differs with formatting infos!");
		return Result::fail;
	}
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
			buf[j/8 +1] |= 1 << j%8;
	}

	uint32_t pointer = 0;
	uint64_t page_offs = getPageNumber(combineAddress(area, dev->param->dataPagesPerArea), dev);
	Result r;
	for(unsigned int page = 0; page < needed_pages; page++){
		unsigned int btw = pointer + dev->param->dataBytesPerPage < areaSummarySize ? dev->param->dataBytesPerPage
							: areaSummarySize - pointer;
		r = dev->driver->writePage(page_offs + page, &buf[pointer], btw);
		if(r != Result::ok)
			return r;

		pointer += btw;
	}
	return Result::ok;
}

Result SummaryCache::readAreasummary(AreaPos area, SummaryEntry* out_summary, bool complete){
	unsigned char buf[areaSummarySize];
	memset(buf, 0, areaSummarySize);
	unsigned int needed_pages = 1 + areaSummarySize / dataBytesPerPage;
	if(needed_pages != dev->param->totalPagesPerArea - dev->param->dataPagesPerArea){
		PAFFS_DBG(PAFFS_TRACE_ERROR, "AreaSummary size differs with formatting infos!");
		return Result::fail;
	}
	uint32_t pointer = 0;
	uint64_t page_offs = getPageNumber(combineAddress(area, dev->param->dataPagesPerArea), dev);
	Result r;
	for(unsigned int page = 0; page < needed_pages; page++){
		unsigned int btr = pointer + dev->param->dataBytesPerPage < areaSummarySize ? dev->param->dataBytesPerPage
							: areaSummarySize - pointer;
		r = dev->driver->readPage(page_offs + page, &buf[pointer], btr);
		if(r != Result::ok)
			return r;

		pointer += btr;
	}
	//buffer ready
	PAFFS_DBG_S(PAFFS_TRACE_AREA, "AreaSummary Buffer was filled with %u Bytes.", pointer);

	if(buf[0] == 0xFF){
		//Magic marker not here, so no AS present
		PAFFS_DBG_S(PAFFS_TRACE_AREA, "And just found an unset AS.");
		return Result::nf;
	}

	for(unsigned int j = 0; j < dev->param->dataPagesPerArea; j++){
		if(buf[j/8+1] & 1 << j%8){
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
