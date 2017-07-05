/*
 * summaryCache.cpp
 *
 *  Created on: 27 Jan 2017
 *      Author: Pascal Pieper
 */

#include "area.hpp"
#include "device.hpp"
#include <inttypes.h>
#include "bitlist.hpp"
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
	memset(summaryCache, 0, areaSummaryCacheSize * areaSummaryEntrySize);
	memset(dirtyPages, 0, areaSummaryCacheSize * sizeof(PageOffs));
}

Result SummaryCache::commitASHard(int &clearedArea){
	PageOffs favDirtyPages = 0;
	AreaPos favouriteArea = 0;
	for(std::pair<AreaPos, uint16_t> it : translation){
			//found a cached element
		uint16_t cachePos = it.second;
		if(isDirty(cachePos) && wasASWrittenByCachePosition(cachePos) &&
			dev->areaMap[it.first].status != AreaStatus::active &&
			(dev->areaMap[it.first].type == AreaType::data || dev->areaMap[it.first].type == AreaType::index)){

			PageOffs _dirtyPages = countDirtyPages(cachePos);

			if(_dirtyPages >= favDirtyPages){
				favouriteArea = it.first;
				favDirtyPages = _dirtyPages;
			}
		}
	}

	if(favouriteArea == 0){
		PAFFS_DBG(PAFFS_TRACE_BUG, "Could not find any swappable candidats, why?");
		return Result::bug;
	}
	if(translation.find(favouriteArea) == translation.end()){
		PAFFS_DBG(PAFFS_TRACE_BUG, "Could not find swapping area in cache?");
		return Result::bug;
	}
	uint16_t cachePos = translation[favouriteArea];

	PAFFS_DBG_S(PAFFS_TRACE_ASCACHE, "Commit Hard swaps GC Area %" PRIu32 " (on %" PRIu32 ")"
			" with %" PRIu32 " (on %" PRIu32 ")",
			dev->activeArea[AreaType::garbageBuffer], dev->areaMap[dev->activeArea[AreaType::garbageBuffer]].position,
			favouriteArea, dev->areaMap[favouriteArea].position);

	SummaryEntry summary[dataPagesPerArea];
	unpackStatusArray(cachePos, summary);

	Result r = dev->areaMgmt.gc.moveValidDataToNewArea(
			favouriteArea, dev->activeArea[AreaType::garbageBuffer], summary);

	if(r != Result::ok){
		PAFFS_DBG(PAFFS_TRACE_ERROR, "Could not move Data for AS commit!");
		return r;
	}

	//swap logical position of areas to keep addresses valid
	AreaPos tmp = dev->areaMap[favouriteArea].position;
	dev->areaMap[favouriteArea].position = dev->areaMap[dev->activeArea[AreaType::garbageBuffer]].position;
	dev->areaMap[dev->activeArea[AreaType::garbageBuffer]].position = tmp;
	//swap erasecounts to let them point to the correct physical position
	PageOffs tmp2 = dev->areaMap[favouriteArea].erasecount;
	dev->areaMap[favouriteArea].erasecount = dev->areaMap[dev->activeArea[AreaType::garbageBuffer]].erasecount;
	dev->areaMap[dev->activeArea[AreaType::garbageBuffer]].erasecount = tmp2;

	packStatusArray(cachePos, summary);
	setDirty(cachePos, false);
	setASWritten(cachePos, false);

	clearedArea = favouriteArea;
	return Result::ok;
}

SummaryEntry SummaryCache::getPackedStatus(uint16_t position, PageOffs page){
	return static_cast<SummaryEntry>((summaryCache[position][page/4] & (0b11 << (page % 4)*2)) >> (page % 4)*2);
}

void SummaryCache::setPackedStatus(uint16_t position, PageOffs page, SummaryEntry value){
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
	BitList<areaSummaryCacheSize> used;
	for(auto it = translation.cbegin(), end = translation.cend();
			it != end; ++it){
		if(used.getBit(it->second)){
			PAFFS_DBG(PAFFS_TRACE_BUG, "Multiple Areas point to same location in Cache!");
			return -1;
		}
		used.setBit(it->second);
	}
	for(int i = 0; i < areaSummaryCacheSize; i++){
		if(!used.getBit(i))
			return i;
	}
	return -1;
}

Result SummaryCache::setPageStatus(AreaPos area, PageOffs page, SummaryEntry state){
	if(dev->readOnly){
		PAFFS_DBG(PAFFS_TRACE_BUG, "Tried setting PageStatus in readOnly mode!");
		return Result::bug;
	}
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
	if(state == SummaryEntry::dirty){
		if(traceMask & PAFFS_WRITE_VERIFY_AS){
			char buf[totalBytesPerPage];
			memset(buf, 0xFF, dataBytesPerPage);
			memset(&buf[dataBytesPerPage], 0x0A, oobBytesPerPage);
			Addr addr = combineAddress(area, page);
			dev->driver->writePage(getPageNumber(addr, dev), buf, totalBytesPerPage);
		}
		++dirtyPages[translation[area]];

		if(traceMask & PAFFS_TRACE_VERIFY_AS){
			PageOffs dirtyPagesCheck = countDirtyPages(translation[area]);
			if(dirtyPagesCheck != dirtyPages[translation[area]]){
				PAFFS_DBG(PAFFS_TRACE_BUG, "DirtyPages differ from actual count! "
						"(Area %" PRIu32 " on %" PRIu32 " was: %" PRIu32 ", thought %" PRIu32 ")",
						area, dev->areaMap[area].position, dirtyPagesCheck, dirtyPages[translation[area]]);
				return Result::fail;
			}
		}

		if(dirtyPages[translation[area]] == dataPagesPerArea){
			PAFFS_DBG_S(PAFFS_TRACE_ASCACHE, "Area %" PRIu32 " has run full of dirty pages, deleting.", area);
			dev->areaMgmt.deleteArea(area);
			//This also resets ASWritten and dirty
			memset(summaryCache[translation[area]], 0, areaSummaryEntrySize);
			dirtyPages[translation[area]] = 0;
		}
	}
	return Result::ok;
}

SummaryEntry SummaryCache::getPageStatus(AreaPos area, PageOffs page, Result *result){
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
	dirtyPages[translation[area]] = countDirtyPages(translation[area]);
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
		memset(summaryCache[i], 0, areaSummaryEntrySize);
		dirtyPages[i] = 0;
	}
	PAFFS_DBG_S(PAFFS_TRACE_VERBOSE, "cleared summary Cache");
	SummaryEntry tmp[2][dataPagesPerArea];			//High Stack usage, FIXME?
	SuperIndex index;
	memset(&index, 0, sizeof(SuperIndex));
	index.areaMap = dev->areaMap;
	index.areaSummary[0] = tmp[0];
	index.areaSummary[1] = tmp[1];

	PAFFS_DBG_S(PAFFS_TRACE_VERBOSE, "Inited SuperIndex");

	Result r = dev->superblock.readSuperIndex(&index);
	if(r != Result::ok){
		PAFFS_DBG_S(PAFFS_TRACE_ERROR, "failed to load Area Summaries!");
		return r;
	}
	dev->usedAreas = index.usedAreas;
	PAFFS_DBG_S(PAFFS_TRACE_VERBOSE, "read superIndex successfully");

	for(int i = 0; i < 2; i++){
		if(index.asPositions[i] > 0){
			translation[index.asPositions[i]] = i;
			packStatusArray(i, index.areaSummary[i]);
			dirtyPages[i] = countDirtyPages(i);
			setDirty(i);	//TODO: This forces a rewrite of superIndex even when we modified nothing
			unsigned char as[totalBytesPerPage];
			PAFFS_DBG(PAFFS_TRACE_ASCACHE, "Checking for an AS at area %" PRIu32 " (phys. %" PRIu32 ", "
					"abs. page %" PRIu64 ")",
					index.asPositions[i], dev->areaMap[index.asPositions[i]].position,
					getPageNumber(combineAddress(index.asPositions[i], dataPagesPerArea), dev));
			r = dev->driver->readPage(getPageNumber(
					combineAddress(index.asPositions[i], dataPagesPerArea), dev),
					as, totalBytesPerPage);
			if(r != Result::ok && r != Result::biterrorCorrected){
				PAFFS_DBG(PAFFS_TRACE_ERROR, "Could not check if AS was already written on "
						"area %" PRIu32, index.asPositions[i]);
				return r;
			}
			for(unsigned int t = 0; t < totalBytesPerPage; t++){
				if(as[t] != 0xFF){
					setASWritten(i);
					break;
				}
			}
			if(dev->areaMap[index.asPositions[i]].status == AreaStatus::active){
				dev->activeArea[dev->areaMap[index.asPositions[i]].type] = dev->areaMap[index.asPositions[i]].position;
			}
			PAFFS_DBG_S(PAFFS_TRACE_VERBOSE, "Loaded area summary %d on %d", index.asPositions[i], dev->areaMap[index.asPositions[i]].position);
		}
	}

	return Result::ok;
}

Result SummaryCache::commitAreaSummaries(bool createNew){
	//commit all Areas except the active ones (and maybe some others)
	if(dev->readOnly){
		PAFFS_DBG(PAFFS_TRACE_BUG, "Tried committing AreaSummaries in readOnly mode!");
		return Result::bug;
	}

	Result r;	//TODO: Maybe commit closed Areas?
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
	index.usedAreas = dev->usedAreas;
	bool someDirty = false;

	//write the open/uncommitted AS'es to Superindex
	for(std::pair<AreaPos, uint16_t> cacheElem : translation){
		//Clean Areas are not committed unless they are active
		if(pos >= 2){
			PAFFS_DBG(PAFFS_TRACE_ERROR, "Too many uncommitted AreaSummaries.\n"
								"\tskipping lossy, because we have to unmount.");
			break;
		}
		if(!isDirty(cacheElem.second) &&
				dev->activeArea[AreaType::data] != cacheElem.first &&
				dev->activeArea[AreaType::index] != cacheElem.first)
			continue;

		someDirty |= isDirty(cacheElem.second);
		index.asPositions[pos] = cacheElem.first;
		unpackStatusArray(cacheElem.second, index.areaSummary[pos++]);
	}

	return dev->superblock.commitSuperIndex(&index, someDirty, createNew);
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
	dirtyPages[translation[area]] = 0;
	SummaryEntry buf[dataPagesPerArea];
	r = readAreasummary(area, buf, true);
	if(r == Result::ok){
		packStatusArray(translation[area], buf);
		setASWritten(translation[area]);
		setDirty(translation[area], false);
		PAFFS_DBG_S(PAFFS_TRACE_ASCACHE, "Loaded existing AreaSummary of %d to cache", area);
		dirtyPages[translation[area]] = countDirtyPages(translation[area]);
	}
	else if(r == Result::nf){
		memset(summaryCache[translation[area]], 0, areaSummaryEntrySize);
		PAFFS_DBG_S(PAFFS_TRACE_ASCACHE, "Loaded new AreaSummary for %d", area);
	}
	else
		return r;

	PAFFS_DBG_S(PAFFS_TRACE_ASCACHE, "Created cache entry for area %d", area);
	return Result::ok;
}

Result SummaryCache::freeNextBestSummaryCacheEntry(bool urgent){
	//from summaryCache to AreaPosition
	bool used[areaSummaryCacheSize];
	AreaPos pos[areaSummaryCacheSize];
	memset(used, false, sizeof(bool) * areaSummaryCacheSize);
	memset(pos, 0, sizeof(AreaPos) * areaSummaryCacheSize);
	int fav = -1;

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
	uint32_t maxDirtyPages = 0;
 	for(int i = 0; i < areaSummaryCacheSize; i++){
		if(used[i] && !wasASWrittenByCachePosition(i) &&
				dev->areaMap[pos[i]].status != AreaStatus::active){
			PageOffs tmp = countUnusedPages(i);
			if(tmp >= maxDirtyPages){
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
		PAFFS_DBG_S(PAFFS_TRACE_ASCACHE, "Committed and deleted cache entry of area %d", pos[fav]);
		return Result::ok;
	}

	//TODO: Deteremine if non-urgent abort is better here or before GC
	if(!urgent)
 		return Result::nf;

	PAFFS_DBG_S(PAFFS_TRACE_ASCACHE, "freeNextBestCache found no uncommitted Area, activating Garbage collection");
	Result r = dev->areaMgmt.gc.collectGarbage(AreaType::unset);
	if(r != Result::ok){
		PAFFS_DBG_S(PAFFS_TRACE_ERROR, "Garbage collection could not free any Areas");
	}

	//Ok, just swap Area-positions, clearing AS
	r = commitASHard(fav);
	if(r != Result::ok){
		PAFFS_DBG(PAFFS_TRACE_ERROR, "Could not free any AS cache elem!");
		return r;
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
		if(getPackedStatus(position, i) == SummaryEntry::dirty)
			dirty++;
	}
	return dirty;
}

uint32_t SummaryCache::countUsedPages(uint16_t position){
	uint32_t used = 0;
	for(uint32_t i = 0; i < dataPagesPerArea; i++){
		if(getPackedStatus(position, i) == SummaryEntry::used)
			used++;
	}
	return used;
}

PageOffs SummaryCache::countUnusedPages(uint16_t position){
	return dataPagesPerArea - countUsedPages(position);
}

Result SummaryCache::writeAreasummary(AreaPos area, SummaryEntry* summary){
	char buf[areaSummarySize];
	memset(buf, 0, areaSummarySize);
	unsigned int needed_pages = 1 + areaSummarySize / dataBytesPerPage;
	if(needed_pages != totalPagesPerArea - dataPagesPerArea){
		PAFFS_DBG(PAFFS_TRACE_ERROR, "AreaSummary size differs with formatting infos!");
		return Result::fail;
	}
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
		if(traceMask & PAFFS_TRACE_VERIFY_AS){
			unsigned char readbuf[totalBytesPerPage];
			r = dev->driver->readPage(page_offs + page, readbuf, totalBytesPerPage);
			for(unsigned int i = 0; i < totalBytesPerPage; i++){
				if(readbuf[i] != 0xFF){
					PAFFS_DBG(PAFFS_TRACE_BUG, "Tried to write AreaSummary over an existing one at "
							"Area %" PRIu32, area);
					return Result::bug;
				}
			}

		}
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
		PAFFS_DBG(PAFFS_TRACE_ERROR, "AreaSummary size differs with formatting infos!\n"
				"needed pages: %u, total-dataPagesPerArea: %u", needed_pages, totalPagesPerArea - dataPagesPerArea);
		return Result::fail;
	}
	uint32_t pointer = 0;
	uint64_t page_offs = getPageNumber(combineAddress(area, dataPagesPerArea), dev);
	Result r;
	for(unsigned int page = 0; page < needed_pages; page++){
		unsigned int btr = pointer + dataBytesPerPage < areaSummarySize ? dataBytesPerPage
							: areaSummarySize - pointer;
		r = dev->driver->readPage(page_offs + page, &buf[pointer], btr);
		if(r != Result::ok){
			if(r == Result::biterrorCorrected){
				setDirty(translation[area]);
				PAFFS_DBG(PAFFS_TRACE_INFO, "Corrected biterror, triggering dirty areaSummary for rewrite.");
			}else{
				return r;
			}
		}

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
				unsigned char pagebuf[totalBytesPerPage];
				Addr tmp = combineAddress(area, j);
				r = dev->driver->readPage(getPageNumber(tmp, dev), pagebuf, totalBytesPerPage);
				if(r != Result::ok){
					if(r == Result::biterrorCorrected){
						setDirty(translation[area]);
						PAFFS_DBG(PAFFS_TRACE_INFO, "Corrected biterror, triggering dirty areaSummary for "
								"rewrite by Garbage collection.\n\t(Hopefully it runs before an additional bitflip happens)");
					}else{
						return r;
					}
				}
				bool contains_data = false;
				for(unsigned int byte = 0; byte < totalBytesPerPage; byte++){
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
