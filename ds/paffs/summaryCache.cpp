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
	if(areaSummaryCacheSize < 3){
		PAFFS_DBG(PAFFS_TRACE_ERROR, "AreaSummaryCacheSize is less than 3!\n"
				"\tThis is not recommended, as Errors can happen.");
	}
	translation.reserve(areaSummaryCacheSize);
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

bool SummaryCache::wasASWrittenByCachePosition(uint16_t position){
	return summaryCache[position][dataPagesPerArea / 4 + 1] & 0b10;
}

void SummaryCache::setASWritten(uint16_t position, bool value){
	if(value)
			summaryCache[position][dataPagesPerArea / 4 + 1] |= 0b10;
		else
			summaryCache[position][dataPagesPerArea / 4 + 1] &= ~0b10;
}

void SummaryCache::unpackStatusArray(uint16_t position, SummaryEntry* arr){
	for(unsigned int i = 0; i < dataPagesPerArea; i++){
		arr[i] = getPackedStatus(position, i);
	}
}

void SummaryCache::packStatusArray(uint16_t position, SummaryEntry* arr){
	for(unsigned int i = 0; i < dataPagesPerArea; i++){
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

Result SummaryCache::setPageStatus(AreaPos area, uint16_t page, SummaryEntry state){
	if(page > dataPagesPerArea){
		PAFFS_DBG(PAFFS_TRACE_BUG, "Tried to access page out of bounds! (was: %d, should: < %d", page, dataPagesPerArea);
		return Result::einval;
	}
	if(translation.find(area) == translation.end()){
		Result r = loadUnbufferedArea(area, true);
		if(r != Result::ok){
			PAFFS_DBG_S(PAFFS_TRACE_ERROR, "Could not load AS of area %d!", area);
			return r;
		}
	}
	setDirty(translation[area]);
	setPackedStatus(translation[area], page, state);
	if(state == SummaryEntry::dirty && traceMask & PAFFS_WRITE_VERIFY_AS){
		char* buf = new char[totalBytesPerPage];
		memset(buf, 0xFF, dataBytesPerPage);
		memset(&buf[dataBytesPerPage], 0x0A, oobBytesPerPage);
		Addr addr = combineAddress(area, page);
		dev->driver->writePage(getPageNumber(addr, dev), buf, totalBytesPerPage);
		delete[] buf;
	}
	return Result::ok;
}

SummaryEntry SummaryCache::getPageStatus(AreaPos area, uint16_t page, Result *result){
	if(page > dataPagesPerArea){
		PAFFS_DBG(PAFFS_TRACE_BUG, "Tried to access page out of bounds! (was: %d, should: < %d", page, dataPagesPerArea);
		*result = Result::einval;
		return SummaryEntry::error;
	}
	if(translation.find(area) == translation.end()){
		Result r = loadUnbufferedArea(area, false);
		if(r == Result::nf){
			//load one-shot AS in read only
			SummaryEntry buf[dataPagesPerArea];
			r = readAreasummary(area, buf, true);
			if(r == Result::ok){
				PAFFS_DBG_S(PAFFS_TRACE_ASCACHE, "Loaded existing AreaSummary of %d without caching", area);
				*result = Result::ok;
				return buf[page];
			}
			else if(r == Result::nf){
				PAFFS_DBG_S(PAFFS_TRACE_ASCACHE, "Loaded free AreaSummary of %d without caching", area);
				*result = Result::ok;
				return SummaryEntry::free;
			}
		}
		if(r != Result::ok){
			*result = r;
			return SummaryEntry::error;
		}
	}
	if(page > dataPagesPerArea){
		PAFFS_DBG(PAFFS_TRACE_BUG, "Tried to access page out of bounds! (was: %d, should: < %d", page, dataPagesPerArea);
		*result = Result::einval;
		return SummaryEntry::error;
	}

	*result = Result::ok;
	return getPackedStatus(translation[area], page);
}

Result SummaryCache::getSummaryStatus(AreaPos area, SummaryEntry* summary, bool complete){
	if(translation.find(area) == translation.end()){
		//This one does not have to be copied into Cache
		//Because it is just for a one-shot of Garbage collection looking for the best area
		Result r = readAreasummary(area, summary, complete);
		if(r == Result::ok){
			PAFFS_DBG_S(PAFFS_TRACE_ASCACHE, "Loaded existing AreaSummary of Area %d", area);
		}
		else if(r == Result::nf){
			PAFFS_DBG_S(PAFFS_TRACE_ASCACHE, "Loaded empty AreaSummary of Area %d", area);
			r = Result::ok;
		}
		return r;
	}
	unpackStatusArray(translation[area], summary);
	return Result::ok;
}

Result SummaryCache::getEstimatedSummaryStatus(AreaPos area, SummaryEntry* summary){
	return getSummaryStatus(area, summary, false);
}

Result SummaryCache::setSummaryStatus(AreaPos area, SummaryEntry* summary){
	//Dont set Dirty, because GC just deleted AS and dirty Pages
	//This area ist most likely to be used soon
	if(translation.find(area) == translation.end()){
		Result r = loadUnbufferedArea(area, true);
		if(r != Result::ok)
			return r;
	}
	packStatusArray(translation[area], summary);
	return Result::ok;
}

Result SummaryCache::deleteSummary(AreaPos area){
	if(translation.find(area) == translation.end()){
		//This is not a bug, because an uncached area can be deleted
		PAFFS_DBG_S(PAFFS_TRACE_ASCACHE, "Tried to delete nonexisting Area %d", area);
		return Result::ok;
	}

	translation.erase(area);
	PAFFS_DBG_S(PAFFS_TRACE_ASCACHE, "Deleted cache entry of area %d", area);
	return Result::ok;
}

//For Garbage collection to consider committed AS-Areas before others
bool SummaryCache::wasASWritten(AreaPos area){
	if(translation.find(area) == translation.end()){
		PAFFS_DBG_S(PAFFS_TRACE_ASCACHE, "Tried to question nonexistent Area %d. This is probably not a bug.", area);
		return false;
	}
	return wasASWrittenByCachePosition(translation[area]);
}

//For Garbage collection that has deleted the AS too
void SummaryCache::resetASWritten(AreaPos area){
	if(translation.find(area) == translation.end()){
		PAFFS_DBG(PAFFS_TRACE_ASCACHE, "Tried to reset AS-Record of nonexistent Area %d. This is probably not a bug.", area);
		return;
	}
	setASWritten(translation[area], false);
}

Result SummaryCache::loadAreaSummaries(){
	//Assumes unused Summary Cache
	for(AreaPos i = 0; i < 2; i++){
		memset(summaryCache[i], 0, dataPagesPerArea / 4 + 1);
	}
	SummaryEntry tmp[2][dataPagesPerArea];
	SuperIndex index;
	memset(&index, 0, sizeof(SuperIndex));
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
	//commit all Areas except the active ones (and maybe some others)
	Result r;
	while(translation.size() > 2){
		r = freeNextBestSummaryCacheEntry(true);
		if(r != Result::ok){
			PAFFS_DBG(PAFFS_TRACE_ERROR, "Could not free a cached AreaSummary.\n"
					"\tThis is ignored, because we have to unmount.");
			break;
		}
	}

	unsigned char pos = 0;
	SummaryEntry tmp[2][dataPagesPerArea];
	SuperIndex index;
	memset(&index, 0, sizeof(SuperIndex));
	index.areaMap = dev->areaMap;
	index.areaSummary[0] = tmp[0];
	index.areaSummary[1] = tmp[1];

	//write the two open AS'es to Superindex
	for (unsigned int i = 0; i < areasNo; i++){
		if((dev->areaMap[i].type == AreaType::data || dev->areaMap[i].type == AreaType::index)
				&& dev->areaMap[i].status == AreaStatus::active){
			if(pos >= 2){
				PAFFS_DBG(PAFFS_TRACE_BUG, "More than two active Areas! This is not handled.");
				return Result::bug;
			}
			index.asPositions[pos] = i;
			unpackStatusArray(translation[i], index.areaSummary[pos++]);
		}
	}
	return dev->superblock.commitSuperIndex(&index);
}

Result SummaryCache::loadUnbufferedArea(AreaPos area, bool urgent){
	Result r = Result::ok;
	int nextEntry = findNextFreeCacheEntry();
	if(nextEntry < 0){
		r = freeNextBestSummaryCacheEntry(urgent);
		if(!urgent && r == Result::nf){
			PAFFS_DBG_S(PAFFS_TRACE_ASCACHE, "Nonurgent Cacheclean did not return free space, activating read-only");
			return Result::nf;
		}
		if(r != Result::ok){
			PAFFS_DBG_S(PAFFS_TRACE_ERROR, "Urgent Cacheclean did not return free space, expect errors");
			return r;
		}
		nextEntry = findNextFreeCacheEntry();
		if(nextEntry < 0){
			PAFFS_DBG(PAFFS_TRACE_BUG, "freeNextBestSummaryCacheEntry did not free space!");
			return Result::bug;
		}
	}
	translation[area] = nextEntry;
	SummaryEntry buf[dataPagesPerArea];
	r = readAreasummary(area, buf, true);
	if(r == Result::ok){
		packStatusArray(translation[area], buf);
		setASWritten(translation[area]);
		setDirty(translation[area], false);
		PAFFS_DBG_S(PAFFS_TRACE_ASCACHE, "Loaded existing AreaSummary of %d to cache", area);
	}
	else if(r == Result::nf){
		memset(summaryCache[translation[area]], 0, dataPagesPerArea / 4 + 1);
		PAFFS_DBG_S(PAFFS_TRACE_ASCACHE, "Loaded new AreaSummary for %d", area);
	}
	else
		return r;

	PAFFS_DBG_S(PAFFS_TRACE_ASCACHE, "Created cache entry for area %d", area);
	return Result::ok;
}

Result SummaryCache::freeNextBestSummaryCacheEntry(bool urgent){
	//from summaryCache to AreaPosition
	bool used[areaSummaryCacheSize] = {0};
	AreaPos pos[areaSummaryCacheSize] = {0};
	int fav = -1;
	uint32_t maxDirtyPages = 0;

	for(auto it = translation.cbegin(), end = translation.cend();
			it != end; ++it){
		if(used[it->second] != false){
			PAFFS_DBG(PAFFS_TRACE_BUG, "Multiple Areas point to same location in Cache!");
			return Result::bug;
		}
		used[it->second] = true;
		pos[it->second] = it->first;
	}

	//Look for unchanged cache entries, the easiest way
	for(int i = 0; i < areaSummaryCacheSize; i++){
		if(used[i]){
			if(!isDirty(i)){
				translation.erase(pos[i]);
				PAFFS_DBG_S(PAFFS_TRACE_ASCACHE, "Deleted non-dirty cache entry of area %d", pos[i]);
				fav = i;
			}
		}else{
			PAFFS_DBG_S(PAFFS_TRACE_ASCACHE, "freeNextBestCache ignored empty pos %d", i);
		}
	}
	if(fav > -1)
		return Result::ok;

	//Look for the least probable Area to be used that has no committed AS
 	for(int i = 0; i < areaSummaryCacheSize; i++){
		if(used[i] && !wasASWrittenByCachePosition(i) && dev->areaMap[pos[i]].status != AreaStatus::active){
			uint32_t tmp = countDirtyPages(i);
			if(tmp > maxDirtyPages){
				fav = i;
				maxDirtyPages = tmp;
			}
		}
	}
	if(fav > -1){
		//Commit AS to Area OOB
		SummaryEntry buf[dataPagesPerArea];
		unpackStatusArray(fav, buf);
		writeAreasummary(pos[fav], buf);
		translation.erase(pos[fav]);
		return Result::ok;
	}

	//TODO: Deteremine if non-urgent abort is better here or before GC
	if(!urgent)
 		return Result::nf;

	PAFFS_DBG_S(PAFFS_TRACE_ASCACHE, "freeNextBestCache found no uncommitted Area, activating Garbage collection");
	Result r = dev->areaMgmt.gc.collectGarbage(AreaType::unset);
	if(r != Result::ok){
		PAFFS_DBG_S(PAFFS_TRACE_ERROR, "Garbage collection could not free any Areas! No possibility to commit AS");
		return r;
	}

	//Look for the least probable Area to be used that has no committed AS
	for(int i = 0; i < areaSummaryCacheSize; i++){
		if(used[i] && !wasASWrittenByCachePosition(i) && dev->areaMap[pos[i]].status != AreaStatus::active){
			uint32_t tmp = countDirtyPages(i);
			if(tmp > maxDirtyPages){
				fav = i;
				maxDirtyPages = tmp;
			}
		}
	}
	if(fav > -1){
		//Commit AS to Area OOB
		SummaryEntry buf[dataPagesPerArea];
		unpackStatusArray(fav, buf);
		writeAreasummary(pos[fav], buf);
		translation.erase(pos[fav]);
		return Result::ok;
	}

	PAFFS_DBG(PAFFS_TRACE_ERROR, "Garbage collection could not free any Areas! No possibility to commit AS");
	return Result::nf;
}

uint32_t SummaryCache::countDirtyPages(uint16_t position){
	uint32_t dirty = 0;
	for(uint32_t i = 0; i < dataPagesPerArea; i++){
		if(getPackedStatus(position, i) != SummaryEntry::used)
			dirty++;
	}
	return dirty;
}

Result SummaryCache::writeAreasummary(AreaPos area, SummaryEntry* summary){
	char buf[areaSummarySize];
	memset(buf, 0, areaSummarySize);
	unsigned int needed_pages = 1 + areaSummarySize / dataBytesPerPage;
	if(needed_pages != totalPagesPerArea - dataPagesPerArea){
		PAFFS_DBG(PAFFS_TRACE_ERROR, "AreaSummary size differs with formatting infos!");
		return Result::fail;
	}
	/*Is it really necessary to save 16 bit while slowing down garbage collection?
	 *TODO: Check how cost reduction scales with bigger flashes.
	 *		AreaSummary is without optimization 2 bit per page. 2 Kib per Page would
	 *		allow roughly 1000 pages per Area. Usually big pages come with big Blocks,
	 *		so a Block would be ~500 pages, so an area would be limited to two Blocks.
	 */
	//TODO: Check if areaOOB is clean, and maybe Verify written data
	PAFFS_DBG_S(PAFFS_TRACE_ASCACHE, "Committing AreaSummary to Area %d", area);

	for(unsigned int j = 0; j < dataPagesPerArea; j++){
		if(summary[j] != SummaryEntry::dirty)
			buf[j/8 +1] |= 1 << j%8;
	}

	uint32_t pointer = 0;
	uint64_t page_offs = getPageNumber(combineAddress(area, dataPagesPerArea), dev);
	Result r;
	for(unsigned int page = 0; page < needed_pages; page++){
		unsigned int btw = pointer + dataBytesPerPage < areaSummarySize ? dataBytesPerPage
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
	if(needed_pages != totalPagesPerArea - dataPagesPerArea){
		PAFFS_DBG(PAFFS_TRACE_ERROR, "AreaSummary size differs with formatting infos!");
		return Result::fail;
	}
	uint32_t pointer = 0;
	uint64_t page_offs = getPageNumber(combineAddress(area, dataPagesPerArea), dev);
	Result r;
	for(unsigned int page = 0; page < needed_pages; page++){
		unsigned int btr = pointer + dataBytesPerPage < areaSummarySize ? dataBytesPerPage
							: areaSummarySize - pointer;
		r = dev->driver->readPage(page_offs + page, &buf[pointer], btr);
		if(r != Result::ok)
			return r;

		pointer += btr;
	}
	//buffer ready
	PAFFS_DBG_S(PAFFS_TRACE_ASCACHE, "AreaSummary Buffer was filled with %u Bytes.", pointer);

	if(buf[0] == 0xFF){
		//Magic marker not here, so no AS present
		PAFFS_DBG_S(PAFFS_TRACE_ASCACHE, "And just found an unset AS.");
		return Result::nf;
	}

	for(unsigned int j = 0; j < dataPagesPerArea; j++){
		if(buf[j/8+1] & 1 << j%8){
			if(complete){
				unsigned char pagebuf[dataBytesPerPage];
				Addr tmp = combineAddress(area, j);
				r = dev->driver->readPage(getPageNumber(tmp, dev), pagebuf, dataBytesPerPage);
				if(r != Result::ok)
					return r;
				bool contains_data = false;
				for(unsigned int byte = 0; byte < dataBytesPerPage; byte++){
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
