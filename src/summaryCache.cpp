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

AreaSummaryElem::AreaSummaryElem(){
	statusBits = 0;
	clear();
};
AreaSummaryElem::~AreaSummaryElem(){
	clear();
};
void AreaSummaryElem::clear(){
	if(isDirty()){
		PAFFS_DBG(PAFFS_TRACE_BUG, "Tried to clear a dirty cache elem!");
	}
	memset(entry, 0, dataPagesPerArea / 4 + 1);
	statusBits = 0;
	dirtyPages = 0;
	area = 0;
}
SummaryEntry AreaSummaryElem::getStatus(PageOffs page){
	if(page >= dataPagesPerArea){
		PAFFS_DBG(PAFFS_TRACE_BUG, "Tried to read page %" PRIu32 ", but allowed < %" PRIu32 "!",
				page, dataPagesPerArea);
		return SummaryEntry::error;
	}
	if(!isUsed()){
		PAFFS_DBG(PAFFS_TRACE_BUG, "Tried to get Status of unused cache elem!");
		return SummaryEntry::error;
	}
	return static_cast<SummaryEntry>((entry[page/4] & (0b11 << (page % 4)*2)) >> (page % 4)*2);
}
void AreaSummaryElem::setStatus(PageOffs page, SummaryEntry value){
	if(!isUsed()){
		PAFFS_DBG(PAFFS_TRACE_BUG, "Tried to set Status of unused cache elem!");
		return;
	}
	if(page >= dataPagesPerArea){
		PAFFS_DBG(PAFFS_TRACE_BUG, "Tried to set page %" PRIu32 ", but allowed < %" PRIu32 "!",
				page, dataPagesPerArea);
		return;
	}
	entry[page/4] =
	(entry[page/4] & ~(0b11 << (page % 4)*2)) |
				(static_cast<int>(value) << (page % 4) * 2);
	if(value == SummaryEntry::dirty)
		dirtyPages++;
	setDirty();
	setLoadedFromSuperPage(false);
}
bool AreaSummaryElem::isDirty(){
	return statusBits & 0b1;
}
void AreaSummaryElem::setDirty(bool dirty){
	if(dirty){
		statusBits |= 0b1;
	}else{
		statusBits &= ~0b1;
	}
}
bool AreaSummaryElem::isAsWritten(){
	return statusBits & 0b10;
}
void AreaSummaryElem::setAsWritten(bool written){
	if(written){
		statusBits |= 0b10;
	}else{
		statusBits &= ~0b10;
	}
}
/**
 * @brief used to determine, if AS
 * did not change since loaded from SuperPage
 */
bool AreaSummaryElem::isLoadedFromSuperPage(){
	return statusBits & 0b100;
}
void AreaSummaryElem::setLoadedFromSuperPage(bool loaded){
	if(loaded){
		statusBits |= 0b100;
	}else{
		statusBits &= ~0b100;
	}
}
bool AreaSummaryElem::isUsed(){
	return statusBits & 0b1000;
}
void AreaSummaryElem::setUsed(bool used){
	if(used){
		statusBits |= 0b1000;
	}else{
		statusBits &= ~0b1000;
	}
}
PageOffs AreaSummaryElem::getDirtyPages(){
	if(!isUsed()){
		PAFFS_DBG(PAFFS_TRACE_BUG, "Tried to get Dirty Pages of unused cache elem!");
	}
	return dirtyPages;
}
void AreaSummaryElem::setDirtyPages(PageOffs pages){
	if(!isUsed()){
		PAFFS_DBG(PAFFS_TRACE_BUG, "Tried to set Dirty Pages of unused cache elem!");
	}
	dirtyPages = pages;
}

void AreaSummaryElem::setArea(AreaPos areaPos){
	if(isUsed()){
		PAFFS_DBG(PAFFS_TRACE_BUG, "Tried to set areaPos %" PRIu32 ", but "
				"SummaryElem is set to area %" PRIu32 "!", areaPos, area);
		return;
	}
	area = areaPos;
	setUsed();
}

AreaPos AreaSummaryElem::getArea(){
	if(!isUsed()){
		PAFFS_DBG(PAFFS_TRACE_BUG, "Tried to get areaPos of unused SummaryElem!");
		return 0;
	}
	return area;
}

SummaryCache::SummaryCache(Device* mdev) : dev(mdev){
	if(areaSummaryCacheSize < 3){
		PAFFS_DBG(PAFFS_TRACE_ERROR, "AreaSummaryCacheSize is less than 3!\n"
				"\tThis is not recommended, as Errors can happen.");
	}
	translation.reserve(areaSummaryCacheSize);
}

SummaryCache::~SummaryCache()
{
	for(unsigned i = 0; i < areaSummaryCacheSize; i++)
	{
		if(summaryCache[i].isDirty())
		{
			PAFFS_DBG(PAFFS_TRACE_ERROR, "Clearing Summary cache with uncommitted elem!");
			summaryCache[i].setDirty(0);
		}
	}
}

Result SummaryCache::commitASHard(int &clearedAreaCachePosition){
	PageOffs favDirtyPages = 0;
	AreaPos favouriteArea = 0;
	uint16_t cachePos = 0;
	for(std::pair<AreaPos, uint16_t> it : translation){
			//found a cached element
		cachePos = it.second;
		if(summaryCache[cachePos].isDirty() && summaryCache[cachePos].isAsWritten() &&
			dev->areaMgmt.getStatus(it.first) != AreaStatus::active &&
			(dev->areaMgmt.getType(it.first) == AreaType::data || dev->areaMgmt.getType(it.first) == AreaType::index)){

			PageOffs dirtyPages = countDirtyPages(cachePos);
			PAFFS_DBG_S(PAFFS_TRACE_ASCACHE, "Checking Area %" PRIu32 " "
					"with %" PRIu32 " dirty pages", it.first, dirtyPages);
			if(dirtyPages >= favDirtyPages){
				favouriteArea = it.first;
				clearedAreaCachePosition = it.second;
				favDirtyPages = dirtyPages;
			}
		}else{
			PAFFS_DBG_S(PAFFS_TRACE_ASCACHE, "Ignored Area %" PRIu32 " "
								"at cache pos %" PRIu32, it.first, it.second);
			if(!summaryCache[cachePos].isDirty()){
				PAFFS_DBG_S(PAFFS_TRACE_ASCACHE, "\tnot dirty");
			}
			if(!summaryCache[cachePos].isAsWritten()){
				PAFFS_DBG_S(PAFFS_TRACE_ASCACHE, "\tnot AS written");
			}
			if(dev->areaMgmt.getStatus(it.first) ==AreaStatus::active){
				PAFFS_DBG_S(PAFFS_TRACE_ASCACHE, "\tis active (%s)", areaNames[dev->areaMgmt.getType(it.first)]);
			}
			if(dev->areaMgmt.getType(it.first) != AreaType::data && dev->areaMgmt.getType(it.first) != AreaType::index){
				PAFFS_DBG_S(PAFFS_TRACE_ASCACHE, "\tnot data/index");
			}

		}
	}

	if(favouriteArea == 0){
		PAFFS_DBG(PAFFS_TRACE_BUG, "Could not find any swappable candidats, why?");
		clearedAreaCachePosition = -1;
		return Result::bug;
	}
	if(translation.find(favouriteArea) == translation.end()){
		PAFFS_DBG(PAFFS_TRACE_BUG, "Could not find swapping area in cache?");
		clearedAreaCachePosition = -1;
		return Result::bug;
	}
	cachePos = translation[favouriteArea];

	PAFFS_DBG_S(PAFFS_TRACE_ASCACHE, "Commit Hard swaps GC Area %" PRIu32 " (on %" PRIu32 ")"
			" from %" PRIu32 " (on %" PRIu32 ")",
			dev->activeArea[AreaType::garbageBuffer], dev->areaMgmt.getPos(dev->activeArea[AreaType::garbageBuffer]),
			favouriteArea, dev->areaMgmt.getPos(favouriteArea));

	SummaryEntry summary[dataPagesPerArea];
	unpackStatusArray(cachePos, summary);

	Result r = dev->areaMgmt.gc.moveValidDataToNewArea(
			favouriteArea, dev->activeArea[AreaType::garbageBuffer], summary);

	if(r != Result::ok){
		PAFFS_DBG(PAFFS_TRACE_ERROR, "Could not move Data for AS commit!");
		clearedAreaCachePosition = -1;
		return r;
	}
	dev->areaMgmt.deleteArea(favouriteArea);
	//swap logical position of areas to keep addresses valid
	dev->areaMgmt.swapAreaPosition(favouriteArea, dev->activeArea[AreaType::garbageBuffer]);
	packStatusArray(cachePos, summary);
	//AsWritten gets reset in delete Area, and dont set dirty bc now the AS is not committed, soley in RAM

	return Result::ok;
}

void SummaryCache::unpackStatusArray(uint16_t position, SummaryEntry* arr){
	for(unsigned int i = 0; i < dataPagesPerArea; i++){
		arr[i] = summaryCache[position].getStatus(i);
	}
}

void SummaryCache::packStatusArray(uint16_t position, SummaryEntry* arr){
	for(unsigned int i = 0; i < dataPagesPerArea; i++){
		summaryCache[position].setStatus(i, arr[i]);
	}
}

int SummaryCache::findNextFreeCacheEntry(){
	//from summaryCache to AreaPosition
	for(int i = 0; i < areaSummaryCacheSize; i++){
		if(!summaryCache[i].isUsed())
			return i;
	}
	return -1;
}

Result SummaryCache::setPageStatus(Addr addr, SummaryEntry state){
	return setPageStatus(extractLogicalArea(addr), extractPageOffs(addr), state);
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
	if(state == SummaryEntry::free){
		PAFFS_DBG(PAFFS_TRACE_BUG, "Area %" PRIu32 " was set to empty, "
				"but apperarently not by deleting it!", area);
	}
	if(translation.find(area) == translation.end()){
		Result r = loadUnbufferedArea(area, true);
		if(r != Result::ok){
			PAFFS_DBG_S(PAFFS_TRACE_ERROR, "Could not load AS of area %d!", area);
			return r;
		}
	}
	if(dev->areaMgmt.getType(area) == AreaType::unset){
		PAFFS_DBG(PAFFS_TRACE_BUG, "Tried setting Pagestatus on UNSET area "
				"%" PRIu32 " (on %" PRIu32 ", status %d)",
				area, dev->areaMgmt.getPos(area), dev->areaMgmt.getStatus(area));
		return Result::bug;
	}

	dev->journal.addEvent(journalEntry::summaryCache::SetStatus(area,page,state));

	summaryCache[translation[area]].setStatus(page, state);
	if(state == SummaryEntry::dirty){
		if(traceMask & PAFFS_WRITE_VERIFY_AS){
			char buf[totalBytesPerPage];
			memset(buf, 0xFF, dataBytesPerPage);
			memset(&buf[dataBytesPerPage], 0x0A, oobBytesPerPage);
			Addr addr = combineAddress(area, page);
			dev->driver.writePage(getPageNumber(addr, *dev), buf, totalBytesPerPage);
		}

		if(traceMask & PAFFS_TRACE_VERIFY_AS){
			PageOffs dirtyPagesCheck = countDirtyPages(translation[area]);
			if(dirtyPagesCheck != summaryCache[translation[area]].getDirtyPages()){
				PAFFS_DBG(PAFFS_TRACE_BUG, "DirtyPages differ from actual count! "
						"(Area %" PRIu32 " on %" PRIu32 " was: %" PRIu32 ", thought %" PRIu32 ")",
						area, dev->areaMgmt.getPos(area), dirtyPagesCheck,
						summaryCache[translation[area]].getDirtyPages());
				return Result::fail;
			}
		}

		if(summaryCache[translation[area]].getDirtyPages() == dataPagesPerArea){
			PAFFS_DBG_S(PAFFS_TRACE_ASCACHE, "Area %" PRIu32 " has run full of dirty pages, deleting.", area);
			//This also deletes the summary entry
			dev->areaMgmt.deleteArea(area);
		}
	}
	return Result::ok;
}

SummaryEntry SummaryCache::getPageStatus(Addr addr, Result *result){
	return getPageStatus(extractLogicalArea(addr), extractPageOffs(addr), result);
}

SummaryEntry SummaryCache::getPageStatus(AreaPos area, PageOffs page, Result *result){
	if(page > dataPagesPerArea){
		PAFFS_DBG(PAFFS_TRACE_BUG, "Tried to access page out of bounds! (was: %d, should: < %d", page, dataPagesPerArea);
		*result = Result::einval;
		return SummaryEntry::error;
	}
	if(translation.find(area) == translation.end()){
		Result r = loadUnbufferedArea(area, false);
		if(r == Result::nospace){
			//load one-shot AS in read only
			SummaryEntry buf[dataPagesPerArea];
			r = readAreasummary(area, buf, true);
			if(r == Result::ok || r == Result::biterrorCorrected){
				PAFFS_DBG_S(PAFFS_TRACE_ASCACHE, "Loaded existing AreaSummary of %d without caching", area);
				*result = Result::ok;
				//TODO: Handle biterror.
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
	return summaryCache[translation[area]].getStatus(page);
}

Result SummaryCache::getSummaryStatus(AreaPos area, SummaryEntry* summary, bool complete){
	if(translation.find(area) == translation.end()){
		//This one does not have to be copied into Cache
		//Because it is just for a one-shot of Garbage collection looking for the best area
		Result r = readAreasummary(area, summary, complete);
		if(r == Result::ok || r == Result::biterrorCorrected){
			//TODO: Handle biterror
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
	summaryCache[translation[area]].setDirtyPages(countDirtyPages(translation[area]));
	return Result::ok;
}

Result SummaryCache::deleteSummary(AreaPos area){
	if(translation.find(area) == translation.end()){
		//This is not a bug, because an uncached area can be deleted
		PAFFS_DBG_S(PAFFS_TRACE_ASCACHE, "Tried to delete nonexisting Area %d", area);
		return Result::ok;
	}

	dev->journal.addEvent(journalEntry::summaryCache::Remove(area));

	summaryCache[translation[area]].setDirty(false);
	summaryCache[translation[area]].clear();
	translation.erase(area);
	PAFFS_DBG_S(PAFFS_TRACE_ASCACHE, "Deleted cache entry of area %d", area);
	return Result::ok;
}

//For Garbage collection to consider cached AS-Areas before others
bool SummaryCache::isCached(AreaPos area){
	return translation.find(area) != translation.end();
}

//For Garbage collection to consider committed AS-Areas before others
bool SummaryCache::wasASWritten(AreaPos area){
	if(translation.find(area) == translation.end()){
		if(dev->areaMgmt.getStatus(area) ==AreaStatus::empty)
			return false;
		//If it is not empty, and not in Cache, it has to be containing Data and is not active.
		//It has to have an AS written.
		return true;
	}
	return summaryCache[translation[area]].isAsWritten();
}

//For Garbage collection that has deleted the AS too
void SummaryCache::resetASWritten(AreaPos area){
	if(translation.find(area) == translation.end()){
		PAFFS_DBG(PAFFS_TRACE_ASCACHE, "Tried to reset AS-Record of non-cached Area %d. This is probably not a bug.", area);
		return;
	}
	summaryCache[translation[area]].setAsWritten(false);
}

Result SummaryCache::loadAreaSummaries(){
	//Assumes unused Summary Cache
	for(AreaPos i = 0; i < areaSummaryCacheSize; i++){
		summaryCache[i].clear();
	}

	PAFFS_DBG_S(PAFFS_TRACE_VERBOSE, "cleared summary Cache");
	SummaryEntry tmp[2][dataPagesPerArea];			//High Stack usage, FIXME?
	SuperIndex index;
	memset(&index, 0, sizeof(SuperIndex));
	index.areaMap = dev->areaMgmt.getMap();
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
			summaryCache[i].setArea(index.asPositions[i]);
			packStatusArray(i, index.areaSummary[i]);
			summaryCache[i].setDirtyPages(countDirtyPages(i));
			summaryCache[i].setLoadedFromSuperPage();

			unsigned char as[totalBytesPerPage];
			PAFFS_DBG_S(PAFFS_TRACE_ASCACHE, "Checking for an AS at area %" PRIu32 " (phys. %" PRIu32 ", "
					"abs. page %" PRIu64 ")",
					index.asPositions[i], dev->areaMgmt.getPos(index.asPositions[i]),
					getPageNumber(combineAddress(index.asPositions[i], dataPagesPerArea), *dev));
			r = dev->driver.readPage(getPageNumber(
					combineAddress(index.asPositions[i], dataPagesPerArea), *dev),
					as, totalBytesPerPage);
			if(r != Result::ok && r != Result::biterrorCorrected){
				PAFFS_DBG(PAFFS_TRACE_ERROR, "Could not check if AS was already written on "
						"area %" PRIu32, index.asPositions[i]);
				return r;
			}
			for(unsigned int t = 0; t < totalBytesPerPage; t++){
				if(as[t] != 0xFF){
					summaryCache[i].setAsWritten();
					break;
				}
			}
			if(dev->areaMgmt.getStatus(index.asPositions[i]) ==AreaStatus::active){
				dev->activeArea[dev->areaMgmt.getType(index.asPositions[i])] = index.asPositions[i];
			}
			PAFFS_DBG_S(PAFFS_TRACE_VERBOSE, "Loaded area summary %d on %d", index.asPositions[i], dev->areaMgmt.getPos(index.asPositions[i]));
		}
	}

	return Result::ok;
}

Result SummaryCache::commitAreaSummaries(bool createNew){
	//commit all Areas except two

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

	SummaryEntry tmp[2][dataPagesPerArea]; //FIXME high Stack usage
	SuperIndex index;
	memset(&index, 0, sizeof(SuperIndex));
	index.areaMap = dev->areaMgmt.getMap();
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
		if(!summaryCache[cacheElem.second].isDirty() &&
				summaryCache[cacheElem.second].isAsWritten())
			continue;

		someDirty |= summaryCache[cacheElem.second].isDirty() &&
				!summaryCache[cacheElem.second].isLoadedFromSuperPage();

		index.asPositions[pos] = cacheElem.first;
		unpackStatusArray(cacheElem.second, index.areaSummary[pos++]);
		summaryCache[cacheElem.second].setDirty(false);
		summaryCache[cacheElem.second].clear();
	}

	translation.clear();

	r = dev->superblock.commitSuperIndex(&index, someDirty, createNew);
	if(r != Result::ok)
	{
		PAFFS_DBG(PAFFS_TRACE_ERROR, "Could not commit superindex");
		return r;
	}
	dev->journal.addEvent(journalEntry::Success(JournalEntry::Topic::summaryCache));
	return Result::ok;
}

JournalEntry::Topic
SummaryCache::getTopic(){
	return JournalEntry::Topic::summaryCache;
}


void
SummaryCache::processEntry(JournalEntry& entry){
	if(entry.topic != getTopic()){
		PAFFS_DBG(PAFFS_TRACE_BUG, "Got wrong entry to process!");
		return;
	}
	const journalEntry::SummaryCache* e =
			static_cast<const journalEntry::SummaryCache*>(&entry);
	switch(e->subtype){
	case journalEntry::SummaryCache::Subtype::commit:
	case journalEntry::SummaryCache::Subtype::remove:
		PAFFS_DBG_S(PAFFS_TRACE_ASCACHE, "Deleting cache "
				"entry of area %d", summaryCache[e->area].getArea());
		summaryCache[e->area].setDirty(false);
		translation.erase(summaryCache[e->area].getArea());
		summaryCache[e->area].clear();
		break;
	case journalEntry::SummaryCache::Subtype::setStatus:
	{
		//TODO activate some failsafe that checks for invalid writes during this setPages
		const journalEntry::summaryCache::SetStatus* s =
				static_cast<const journalEntry::summaryCache::SetStatus*>(&entry);
		setPageStatus(s->area, s->page, s->status);
		break;
	}
	}
}

void
SummaryCache::processUncheckpointedEntry(JournalEntry& entry){
	if(entry.topic != getTopic()){
		PAFFS_DBG(PAFFS_TRACE_BUG, "Got wrong entry to process!");
		return;
	}
	const journalEntry::SummaryCache* e =
			static_cast<const journalEntry::SummaryCache*>(&entry);
	switch(e->subtype){
	case journalEntry::SummaryCache::Subtype::commit:
	case journalEntry::SummaryCache::Subtype::remove:
		//TODO: Is it Ok if nothing happens here?
		//B.c. we are overwriting 'used' pages
		break;
	case journalEntry::SummaryCache::Subtype::setStatus:
	{
		//TODO activate some failsafe that checks for invalid writes during this setPages
		const journalEntry::summaryCache::SetStatus* s =
				static_cast<const journalEntry::summaryCache::SetStatus*>(&entry);
		if(s->status == SummaryEntry::used)
		{
			setPageStatus(s->area, s->page, SummaryEntry::dirty);
		}
		if(s->status == SummaryEntry::dirty)
		{
			setPageStatus(s->area, s->page, SummaryEntry::used);
		}
		break;
	}
	}
}

Result SummaryCache::loadUnbufferedArea(AreaPos area, bool urgent){
	Result r = Result::ok;
	int nextEntry = findNextFreeCacheEntry();
	if(nextEntry < 0){
		r = freeNextBestSummaryCacheEntry(urgent);
		if(!urgent && r == Result::nf){
			PAFFS_DBG_S(PAFFS_TRACE_ASCACHE, "Nonurgent Cacheclean did not return free space, activating read-only");
			return Result::nospace;
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
	summaryCache[nextEntry].setArea(area);

	SummaryEntry buf[dataPagesPerArea];
	r = readAreasummary(area, buf, true);
	if(r == Result::ok || r == Result::biterrorCorrected){
		packStatusArray(translation[area], buf);
		summaryCache[nextEntry].setAsWritten();
		summaryCache[nextEntry].setDirty(r == Result::biterrorCorrected);	//Rewrites corrected Bit somewhen
		PAFFS_DBG_S(PAFFS_TRACE_ASCACHE, "Loaded existing AreaSummary of %d to cache", area);
		summaryCache[translation[area]].setDirtyPages(countDirtyPages(translation[area]));
	}
	else if(r == Result::nf){
		PAFFS_DBG_S(PAFFS_TRACE_ASCACHE, "Loaded new AreaSummary for %d", area);
	}
	else
		return r;

	PAFFS_DBG_S(PAFFS_TRACE_ASCACHE, "Created cache entry for area %d", area);
	return Result::ok;
}

Result SummaryCache::freeNextBestSummaryCacheEntry(bool urgent){
	int fav = -1;

	//Look for unchanged cache entries, the easiest way
	for(int i = 0; i < areaSummaryCacheSize; i++){
		if(summaryCache[i].isUsed()){
			if(!summaryCache[i].isDirty() ||
					dev->areaMgmt.getStatus(summaryCache[i].getArea()) ==AreaStatus::empty){
				if(summaryCache[i].isDirty() && dev->areaMgmt.getStatus(summaryCache[i].getArea()) ==AreaStatus::empty){
					//Dirty, but it was not properly deleted?
					PAFFS_DBG(PAFFS_TRACE_BUG, "Area %" PRIu32 " is dirty, but was "
							"not set to an status (Type %s)", summaryCache[i].getArea(),
							areaNames[dev->areaMgmt.getType(summaryCache[i].getArea())]);
				}
				PAFFS_DBG_S(PAFFS_TRACE_ASCACHE, "Deleted non-dirty cache entry "
						"of area %" PRIu32, summaryCache[i].getArea());
				translation.erase(summaryCache[i].getArea());
				summaryCache[i].clear();
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
		if(summaryCache[i].isUsed() && !summaryCache[i].isAsWritten() &&
				dev->areaMgmt.getStatus(summaryCache[i].getArea()) != AreaStatus::active){
			PageOffs tmp = countUnusedPages(i);
			if(tmp >= maxDirtyPages){
				fav = i;
				maxDirtyPages = tmp;
			}
		}
	}
	if(fav > -1){
		return commitAndEraseElem(fav);
	}

	if(!urgent)
		return Result::nf;

	PAFFS_DBG_S(PAFFS_TRACE_ASCACHE, "freeNextBestCache found no uncommitted Area, activating Garbage collection");
	Result r = dev->areaMgmt.gc.collectGarbage(AreaType::unset);


	if(r == Result::ok){
		if(translation.size() < areaSummaryCacheSize){
			//GC freed something
			return Result::ok;
		}
	}
	//GC may have relocated an Area, deleting the committed AS
	//Look for the least probable Area to be used that has no committed AS
	maxDirtyPages = 0;
	for(int i = 0; i < areaSummaryCacheSize; i++){
		if(summaryCache[i].isUsed() && !summaryCache[i].isAsWritten() &&
						dev->areaMgmt.getStatus(summaryCache[i].getArea()) != AreaStatus::active){
			PageOffs tmp = countUnusedPages(i);
			if(tmp >= maxDirtyPages){
				fav = i;
				maxDirtyPages = tmp;
			}
		}
	}
	if(fav > -1){
		return commitAndEraseElem(fav);
	}

	PAFFS_DBG_S(PAFFS_TRACE_ASCACHE, "Garbage collection could not relocate any Areas");
	//Ok, just swap Area-positions, clearing AS
	r = commitASHard(fav);
	if(r != Result::ok){
		PAFFS_DBG(PAFFS_TRACE_ERROR, "Could not free any AS cache elem!");
		return r;
	}


	if(traceMask & PAFFS_TRACE_VERIFY_AS){
		//check for bugs in usage of Garbage collection
		unsigned activeAreas = 0;
		for(AreaPos i = 0; i < areasNo; i++){
			if(dev->areaMgmt.getStatus(i) ==AreaStatus::active &&
					(dev->areaMgmt.getType(i) == AreaType::data || dev->areaMgmt.getType(i) == AreaType::index)){
				activeAreas++;
			}
		}
		if(activeAreas > 2){
			PAFFS_DBG(PAFFS_TRACE_BUG, "More than two active Areas after gc! (%u)", activeAreas);
			return Result::bug;
		}
	}

	if(fav < 0){
		PAFFS_DBG_S(PAFFS_TRACE_ASCACHE, "Garbage collection could not free any Areas");
		//Ok, just swap Area-positions, clearing AS
		r = commitASHard(fav);
		if(r != Result::ok){
			PAFFS_DBG(PAFFS_TRACE_ERROR, "Could not free any AS cache elem!");
			return r;
		}
	}

	if(fav > -1){
		return commitAndEraseElem(fav);
	}

	PAFFS_DBG(PAFFS_TRACE_ERROR, "No area was found to clear!");
	return Result::nf;
}

uint32_t SummaryCache::countDirtyPages(uint16_t position){
	uint32_t dirty = 0;
	for(uint32_t i = 0; i < dataPagesPerArea; i++){
		if(summaryCache[position].getStatus(i) == SummaryEntry::dirty)
			dirty++;
	}
	return dirty;
}

uint32_t SummaryCache::countUsedPages(uint16_t position){
	uint32_t used = 0;
	for(uint32_t i = 0; i < dataPagesPerArea; i++){
		if(summaryCache[position].getStatus(i) == SummaryEntry::used)
			used++;
	}
	return used;
}

PageOffs SummaryCache::countUnusedPages(uint16_t position){
	return dataPagesPerArea - countUsedPages(position);
}

Result SummaryCache::commitAndEraseElem(uint16_t position){
	//Commit AS to Area OOB
	Result r = writeAreasummary(position);
	if(r != Result::ok){
		PAFFS_DBG(PAFFS_TRACE_ERROR, "Could not write AreaSummary");
		return r;
	}
	PAFFS_DBG_S(PAFFS_TRACE_ASCACHE, "Committed and deleted cache "
			"entry of area %d", summaryCache[position].getArea());
	translation.erase(summaryCache[position].getArea());
	summaryCache[position].clear();
	return Result::ok;
}

Result SummaryCache::writeAreasummary(uint16_t pos){
	if(summaryCache[pos].isAsWritten()){
		PAFFS_DBG(PAFFS_TRACE_BUG, "Tried to commit elem with existing AS Commit!");
		return Result::bug;
	}
	char buf[areaSummarySize];
	memset(buf, 0, areaSummarySize);
	unsigned int needed_pages = 1 + areaSummarySize / dataBytesPerPage;
	if(needed_pages != totalPagesPerArea - dataPagesPerArea){
		PAFFS_DBG(PAFFS_TRACE_ERROR, "AreaSummary size differs with formatting infos!");
		return Result::fail;
	}
	//TODO: Check if areaOOB is clean, and maybe Verify written data
	PAFFS_DBG_S(PAFFS_TRACE_ASCACHE, "Committing AreaSummary to Area %d", summaryCache[pos].getArea());

	for(unsigned int j = 0; j < dataPagesPerArea; j++){
		if(summaryCache[pos].getStatus(j) != SummaryEntry::dirty)
			buf[j/8 +1] |= 1 << j%8;
	}

	uint32_t pointer = 0;
	uint64_t page_offs = getPageNumber(
			combineAddress(summaryCache[pos].getArea(), dataPagesPerArea), *dev);
	Result r;
	for(unsigned int page = 0; page < needed_pages; page++){
		unsigned int btw = pointer + dataBytesPerPage < areaSummarySize ? dataBytesPerPage
							: areaSummarySize - pointer;
		if(traceMask & PAFFS_TRACE_VERIFY_AS){
			unsigned char readbuf[totalBytesPerPage];
			r = dev->driver.readPage(page_offs + page, readbuf, totalBytesPerPage);
			for(unsigned int i = 0; i < totalBytesPerPage; i++){
				if(readbuf[i] != 0xFF){
					PAFFS_DBG(PAFFS_TRACE_BUG, "Tried to write AreaSummary over an existing one at "
							"Area %" PRIu32, summaryCache[pos].getArea());
					return Result::bug;
				}
			}

		}
		r = dev->driver.writePage(page_offs + page, &buf[pointer], btw);
		if(r != Result::ok)
			return r;

		pointer += btw;
	}
	dev->journal.addEvent(journalEntry::summaryCache::Commit(summaryCache[pos].getArea()));
	summaryCache[pos].setAsWritten();
	summaryCache[pos].setDirty(false);
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
	bool bitErrorWasCorrected = false;
	uint32_t pointer = 0;
	uint64_t page_offs = getPageNumber(combineAddress(area, dataPagesPerArea), *dev);
	Result r;
	for(unsigned int page = 0; page < needed_pages; page++){
		unsigned int btr = pointer + dataBytesPerPage < areaSummarySize ? dataBytesPerPage
							: areaSummarySize - pointer;
		r = dev->driver.readPage(page_offs + page, &buf[pointer], btr);
		if(r != Result::ok){
			if(r == Result::biterrorCorrected){
				bitErrorWasCorrected = true;
				PAFFS_DBG(PAFFS_TRACE_INFO, "Corrected biterror, triggering dirty areaSummary for rewrite.");
			}else{
				return r;
			}
		}

		pointer += btr;
	}
	//buffer ready
	//PAFFS_DBG_S(PAFFS_TRACE_ASCACHE, "AreaSummary Buffer was filled with %u Bytes.", pointer);

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
				r = dev->driver.readPage(getPageNumber(tmp, *dev), pagebuf, totalBytesPerPage);
				if(r != Result::ok){
					if(r == Result::biterrorCorrected){
						bitErrorWasCorrected = true;
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

	if(bitErrorWasCorrected)
		return Result::biterrorCorrected;
	return Result::ok;
}

}  // namespace paffs
