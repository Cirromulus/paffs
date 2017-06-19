/*
 * superblock.c
 *
 *  Created on: 19.10.2016
 *      Author: Pascal Pieper
 */

#include "driver/driver.hpp"
#include "superblock.hpp"
#include "device.hpp"
#include "dataIO.hpp"
#include "area.hpp"
#include <inttypes.h>
#include <stdlib.h>

namespace paffs{

Result Superblock::registerRootnode(Addr addr){
	if(addr == 0)
		PAFFS_DBG(PAFFS_TRACE_BUG, "BUG: Tried to set Rootnode to 0");
	rootnode_addr = addr;
	rootnode_dirty = true;
	return Result::ok;
}

Addr Superblock::getRootnodeAddr(){
	if(rootnode_addr == 0){
		PAFFS_DBG(PAFFS_TRACE_ERROR, "rootnode_address is 0! Maybe device not mounted?");
		return 0;
	}

	return rootnode_addr;
}

void Superblock::printSuperIndex(SuperIndex* ind){
	printf("No:\t\t%" PRIu32 "\n", ind->no);
	printf("Rootnode addr.: \t%u:%u\n",
			extractLogicalArea(ind->rootNode),
			extractPage(ind->rootNode));
	printf("Used Areas: %" PRIu32 "\n", ind->usedAreas);
	printf("areaMap:\n");
	AreaPos asOffs = 0;
	for(AreaPos i = 0; i < 8; i ++){
		printf("\t%" PRIu32 "->%" PRIu32 "\n", i, ind->areaMap[i].position);
		printf("\tType: %s\n", areaNames[ind->areaMap[i].type]);
		if(asOffs < 2 && i == ind->asPositions[asOffs]){
			unsigned int free = 0, used = 0, dirty = 0;
			for(unsigned int j = 0; j < dataPagesPerArea; j++){
				if(ind->areaSummary[asOffs][j] == SummaryEntry::free)
					free++;
				if(ind->areaSummary[asOffs][j] == SummaryEntry::used)
					used++;
				if(ind->areaSummary[asOffs][j] == SummaryEntry::dirty)
					dirty++;
			}
			printf("\tFree/Used/Dirty Pages: %u/%u/%u\n", free, used, dirty);
			asOffs++;
		}else{
			printf("\tSummary not present.\n");
		}
		printf("\t----------------\n");
	}
}

void Superblock::setTestmode(bool t){
	testmode = t;
}
/**
 * @brief Ugh, O(n) with areaCount
 */
Result Superblock::resolveDirectToLogicalPath(Addr directPath[superChainElems],
		Addr outPath[superChainElems]){
	AreaPos p = 0;
	int d = 0;
	for(AreaPos i = 0; i < areasNo; i++){
		p = dev->areaMap[i].position;
		for(d = 0; d < superChainElems; d++){
			if(p == extractLogicalArea(directPath[d]))
				outPath[d] = combineAddress(i, extractPage(directPath[d]));
		}
	}
	return Result::ok;
}

Result Superblock::fillPathWithFirstSuperblockAreas(Addr directPath[superChainElems]){
	int foundElems = 0;
	for(AreaPos i = 0; i < areasNo && foundElems <= superChainElems; i++){
		if(dev->areaMap[i].type == AreaType::superblock){
			directPath[foundElems++] = combineAddress(dev->areaMap[i].position, 0);
			PAFFS_DBG_S(PAFFS_TRACE_SUPERBLOCK, "Found new superblock area for chain %d", foundElems);
		}
	}
	if(foundElems != superChainElems){
		PAFFS_DBG(PAFFS_TRACE_ERROR, "Could not find enough superBlocks path! got %d, should %u", foundElems, superChainElems);
		return Result::fail;
	}
	return Result::ok;
}

Result Superblock::commitSuperIndex(SuperIndex* newIndex, bool asDirty, bool createNew){
	unsigned int needed_bytes = sizeof(Addr) + sizeof(AreaPos) +
				areasNo * sizeof(Area)
				+ 2 * dataPagesPerArea / 8; /* One bit per entry, two entrys for INDEX and DATA section*/
	unsigned int needed_pages = needed_bytes / dataBytesPerPage + 1;
	PAFFS_DBG_S(PAFFS_TRACE_SUPERBLOCK, "Minimum Pages needed to read former SuperIndex: %u (%u bytes, 2 AS'es)", needed_pages, needed_bytes);

	Result r;
	if(createNew){
		PAFFS_DBG_S(PAFFS_TRACE_SUPERBLOCK, "Creating new superchain");
		memset(superChainIndexes, 0, superChainElems * sizeof(SerialNo));
		r = fillPathWithFirstSuperblockAreas(pathToSuperIndexDirect);
		if(r != Result::ok){
			PAFFS_DBG(PAFFS_TRACE_ERROR, "Could not init superchain");
			return r;
		}
	}

	if(!asDirty && !rootnode_dirty){
		PAFFS_DBG_S(PAFFS_TRACE_SUPERBLOCK, "Skipping write of superIndex "
				"because nothing is dirty");
		return Result::ok;
	}

	//Get index of last chain elem (SuperEntry) and increase
	newIndex->no = superChainIndexes[jumpPadNo+1]+1;
	newIndex->rootNode = rootnode_addr;
	newIndex->areaMap = dev->areaMap;	//This should already be done in cachefunction

	if(traceMask & PAFFS_TRACE_VERBOSE){
		printf("write Super Index:\n");
		printSuperIndex(newIndex);
	}

	Addr logicalPath[superChainElems];
	r = resolveDirectToLogicalPath(pathToSuperIndexDirect, logicalPath);
	if(r != Result::ok){
		PAFFS_DBG(PAFFS_TRACE_ERROR, "Could not resolve direct to logical path!");
		return r;
	}

	AreaPos directAreas[superChainElems];
	for(int i = 0; i < superChainElems; i++){
		directAreas[i] = extractLogicalArea(pathToSuperIndexDirect[i]);
	}
	AreaPos lastArea = directAreas[jumpPadNo+1];

	r = insertNewSuperIndex(logicalPath[jumpPadNo+1], &directAreas[jumpPadNo+1], newIndex);
	if(r != Result::ok){
		PAFFS_DBG(PAFFS_TRACE_ERROR, "Could not insert new super Page!");
		return r;
	}

	if(!testmode){
		if(!createNew && lastArea == directAreas[jumpPadNo+1]){
			PAFFS_DBG_S(PAFFS_TRACE_SUPERBLOCK, "Committing superindex "
					"at phys. area %" PRIu32 " was enough!", lastArea);
			rootnode_dirty = false;
			return Result::ok;
		}
	}else{
		PAFFS_DBG_S(PAFFS_TRACE_SUPERBLOCK, "comitting jumpPad anyway because of test setting!");
	}

	for(int i = jumpPadNo; i > 0; i--){
		JumpPadEntry e = {superChainIndexes[i]+1, 0, directAreas[i+1]};
		lastArea = directAreas[i];
		r = insertNewJumpPadEntry(logicalPath[i], &directAreas[i], &e);
		if(r != Result::ok){
			PAFFS_DBG(PAFFS_TRACE_ERROR, "Could not insert new JumpPadEntry (Chain %d)!", i);
			return r;
		}
		if(!createNew && lastArea == directAreas[i]){
			PAFFS_DBG_S(PAFFS_TRACE_SUPERBLOCK, "Committing jumpPad no. %d "
					"at phys. area %" PRIu32 "was enough!", i, lastArea);
			rootnode_dirty = false;
			return Result::ok;
		}
	}
	AnchorEntry a = {
			.no = superChainIndexes[0]+1,
			.logPrev = 0, .jumpPadArea = directAreas[1],
			.param = stdParam, .fsVersion = version,
	};
	lastArea = directAreas[1];
	r = insertNewAnchorEntry(logicalPath[0], &directAreas[0], &a);
	if(r != Result::ok){
		PAFFS_DBG(PAFFS_TRACE_ERROR, "Could not insert new AnchorEntry!");
		return r;
	}
	if(lastArea != directAreas[1]){
		PAFFS_DBG(PAFFS_TRACE_BUG, "Anchor entry (%" PRIu32 ") may never "
				"change its previous area (%" PRIu32 ")!", directAreas[1], lastArea);
		return Result::bug;
	}

	rootnode_dirty = false;
	return Result::ok;
}

Result Superblock::readSuperIndex(SuperIndex* index){
	AreaPos logPrev[superChainElems];
	PAFFS_DBG_S(PAFFS_TRACE_SUPERBLOCK, "Reading SuperIndex.");

	Result r = getPathToMostRecentSuperIndex(pathToSuperIndexDirect, superChainIndexes, logPrev);
	if(r != Result::ok){
		PAFFS_DBG(PAFFS_TRACE_ERROR, "could not get addr of most recent Superindex");
		return r;
	}

	//Index of last chain elem (SuperEntry) must not be empty (not found)
	if(superChainIndexes[jumpPadNo+1] == emptySerial){
		PAFFS_DBG(PAFFS_TRACE_ERROR, "Determined Address of last SuperIndex, but its SerialNo was empty!");
		PAFFS_DBG_S(PAFFS_TRACE_ERROR, "Area %" PRIu32 ", page %" PRIu32,
				extractLogicalArea(pathToSuperIndexDirect[jumpPadNo+1]), extractPage(pathToSuperIndexDirect[jumpPadNo+1]));
		return Result::bug;
	}

	AnchorEntry e;
	r = readAnchorEntry(pathToSuperIndexDirect[0], &e);
	if(r != Result::ok){
		PAFFS_DBG(PAFFS_TRACE_ERROR, "Could not read Anchor Entry at %" PRIu32
				":%" PRIu32, extractLogicalArea(pathToSuperIndexDirect[0]), extractPage(pathToSuperIndexDirect[0]));
		return r;
	}
	if(memcmp(&stdParam, &e.param, sizeof(Param)) != 0){
		PAFFS_DBG(PAFFS_TRACE_ERROR, "Device parameter differ with own settings!");
		return Result::fail;
	}else{
		PAFFS_DBG_S(PAFFS_TRACE_VERBOSE, "Formatting infos are matching with our own");
	}
	if(e.fsVersion != version){
		PAFFS_DBG(PAFFS_TRACE_ERROR, "FS Version differs with our own!");
		return Result::fail;
	}


	Addr addr = pathToSuperIndexDirect[superChainElems-1];

	PAFFS_DBG_S(PAFFS_TRACE_SUPERBLOCK, "Found Super Index at %u:%u\n", extractLogicalArea(addr), extractPage(addr));

	r = readSuperPageIndex(addr, index, true);
	if(r != Result::ok){
		PAFFS_DBG(PAFFS_TRACE_ERROR, "Could not read Super Index!");
		return r;
	}
	PAFFS_DBG_S(PAFFS_TRACE_VERBOSE, "Read of SuperPage successful");

	if(traceMask & PAFFS_TRACE_SUPERBLOCK){
		printf("Read Super Index:\n");
		printSuperIndex(index);
	}

	if(index->areaMap[0].position != 0){
		PAFFS_DBG_S(PAFFS_TRACE_ERROR, "Position of area 0 may never be different than 0 "
				"(was %" PRIu32 ")", index->areaMap[0].position);
		return Result::fail;
	}

	if(index->areaMap[0].type != AreaType::superblock){
		PAFFS_DBG_S(PAFFS_TRACE_ERROR, "Type of area 0 may never be different than superblock "
				"(was %d)", static_cast<int>(index->areaMap[0].type));
		return Result::fail;
	}

	for(unsigned int i = 0; i < areasNo; i++){
		if(index->areaMap[i].position > areasNo){
			PAFFS_DBG_S(PAFFS_TRACE_ERROR, "Position of area %u unplausible! (%" PRIu32 ")",
					i, index->areaMap[i].position);
			return Result::fail;
		}
		if(index->areaMap[i].type >= AreaType::no){
			PAFFS_DBG_S(PAFFS_TRACE_ERROR, "Type of area %u unplausible! (%u)",
					i, static_cast<unsigned int>(index->areaMap[i].type));
			return Result::fail;
		}
		if(index->areaMap[i].status > AreaStatus::empty){
			PAFFS_DBG_S(PAFFS_TRACE_ERROR, "Status of area %u unplausible! (%u)",
					i, static_cast<unsigned int>(index->areaMap[i].status));
			return Result::fail;
		}
	}

	if(index->areaMap[extractLogicalArea(index->rootNode)].type != AreaType::index){
		PAFFS_DBG_S(PAFFS_TRACE_ERROR, "Rootnode address does not point to index area"
				" (%u, %u)",
				extractLogicalArea(index->rootNode), extractPage(index->rootNode));
		return Result::fail;
	}

	//Anchor entry is ignored, should never change
	if(logPrev[0] != 0){
		PAFFS_DBG_S(PAFFS_TRACE_ERROR, "Anchor Area stated it would have been "
				"moved to log. %" PRIu32 ", which is not allowed.", logPrev[0]);
		return Result::fail;
	}
	//Reverse order, because the changes were committed from
	//SuperIndex (last) to Anchor (first entry)
	for(int i = superChainElems-1; i >= 0; i--){
		if(logPrev[i] != 0){
			PAFFS_DBG_S(PAFFS_TRACE_SUPERBLOCK, "Chain Area %d (phys. %" PRIu32 ") changed its location to log. %" PRIu32, i,
					extractLogicalArea(pathToSuperIndexDirect[i]), logPrev[i]);
			AreaPos directNew = extractLogicalArea(pathToSuperIndexDirect[i]);
			AreaPos logNew = 0;
			//This is O(n) with AreasNo
			for(AreaPos a = 0; a < areasNo; a++){
				if(directNew == index->areaMap[a].position){
					logNew = a;
					break;
				}
			}
			index->areaMap[logPrev[i]].status = AreaStatus::empty;
			index->areaMap[logPrev[i]].type = AreaType::unset;
			index->areaMap[logNew].status = AreaStatus::active;
			index->areaMap[logNew].type = AreaType::superblock;
		}
	}

	rootnode_addr = index->rootNode;
	rootnode_dirty = false;
	return Result::ok;
}

Result Superblock::findFirstFreeEntryInArea(AreaPos area, PageOffs* out_pos,
		unsigned int required_pages){
	PageOffs pageOffs[blocksPerArea];
	int ffBlock = -1;
	Result r;
	for(int block = 0; block < blocksPerArea; block++){
		r = findFirstFreeEntryInBlock(area, block, &pageOffs[block], required_pages);
		if(r == Result::nf){
			if (block+1 == blocksPerArea){
				//We are last entry, no matter what previous blocks contain or not, this is full
				return Result::nf;
			}
		}else if(r != Result::ok){
			PAFFS_DBG(PAFFS_TRACE_ERROR, "Could not find free Entry in phys. Area %" PRIu32, area);
			return r;
		}else{
			if(ffBlock < 0 || pageOffs[block] != 0)
				ffBlock = block;
		}
	}
	if (ffBlock < 0)
		return Result::nf;//this should never be reached

	*out_pos = ffBlock * pagesPerBlock + pageOffs[ffBlock];
	return Result::ok;
}

//out_pos shall point to the first free page
Result Superblock::findFirstFreeEntryInBlock(AreaPos area, uint8_t block,
							PageOffs* out_pos, unsigned int required_pages){
	unsigned int inARow = 0;
	PageOffs pageOffs = pagesPerBlock * (area * blocksPerArea + block);
	for(unsigned int i = 0; i < pagesPerBlock; i++) {
		PageAbs page = i + pageOffs;
		SerialNo no;
		Result r = dev->driver->readPage(page, &no, sizeof(SerialNo));
		//Ignore corrected bits b.c. This function is used to write new Entry
		if(r != Result::ok && r != Result::biterrorCorrected)
			return r;
		if(no != emptySerial){
			if(inARow != 0){
				*out_pos = 0;
				inARow = 0;
			}
			continue;
		}
		// Unprogrammed, therefore empty

		if(inARow == 0)
			*out_pos = i;	//We shall point to the first free page in this row

		if(++inARow == required_pages)
			return Result::ok;
	}
	return Result::nf;
}
/**
 * @param path returns the *direct* addresses to each found Entry up to SuperEntry
 */
Result Superblock::getPathToMostRecentSuperIndex(Addr path[superChainElems],
		SerialNo indexes[superChainElems],  AreaPos logPrev[superChainElems]){
	AreaPos areaPath[superChainElems+1] = {0};
	Result r;
	for(int i = 0; i < superChainElems; i++){
		r = readMostRecentEntryInArea(areaPath[i], &path[i],
				&indexes[i], &areaPath[i+1], &logPrev[i]);
		if(r != Result::ok){
			PAFFS_DBG(PAFFS_TRACE_ERROR, "Could not find a Superpage in Area %" PRIu32,
					extractLogicalArea(path[i]));
			return r;
		}
		if(i > 0 && extractLogicalArea(path[i]) == 0){
			PAFFS_DBG(PAFFS_TRACE_ERROR, "A non-anchor chain elem is located in Area 0!");
			return Result::fail;
		}
		PAFFS_DBG_S(PAFFS_TRACE_SUPERBLOCK, "Found Chain Elem %d at phys. area "
				"%" PRIu32 " with index %" PRIu32,
				i, extractLogicalArea(path[i]), indexes[i]);
		if(i < superChainElems - 1){
			PAFFS_DBG_S(PAFFS_TRACE_SUPERBLOCK, "\tpointing to phys. area %" PRIu32, areaPath[i+1]);
		}
		if(logPrev[i] != 0){
			PAFFS_DBG_S(PAFFS_TRACE_SUPERBLOCK, "The previously used log. area was %" PRIu32,
					logPrev[i]);
		}
	}

	return Result::ok;
}

/**
 * @param area : *physical* Area in which to look
 * @param out_pos : offset in pages starting from area front where Entry was found
 * @param out_index : The index of the elem found
 * @param next : The area of the next chain elem as read from current
 *
 * Assumes, that if a block contains a valid Entry, no other block contains
 * another entry. This assumption is correct,
 * because the block has to be deleted, if the cursor jumps to next Block.
 */
Result Superblock::readMostRecentEntryInArea(AreaPos area, Addr* out_pos,
		SerialNo* out_index, AreaPos* next, AreaPos* logPrev){
	Result r;
	for(int i = 0; i < blocksPerArea; i++){
		PageOffs pos = 0;
		r = readMostRecentEntryInBlock(area, i, &pos, out_index, next, logPrev);
		if(r == Result::ok){
			PAFFS_DBG_S(PAFFS_TRACE_SUPERBLOCK, "Found most recent entry in "
					"phys. area %" PRIu32 " block %d (abs page %" PRIu32 ")", area, i, pos);
			*out_pos = combineAddress(pos / totalPagesPerArea, pos % totalPagesPerArea);
			return Result::ok;
		}
		if(r != Result::nf)
			return r;
	}
	return Result::nf;
}

/**
 * @param area : *physical* Area in which to look
 * @param block: Which block to check
 * @param out_pos : offset in pages starting from area front where Entry was found
 * @param out_index : The index of the elem found
 */
Result Superblock::readMostRecentEntryInBlock(AreaPos area, uint8_t block,
		PageOffs* out_pos, SerialNo* out_index, AreaPos* next, AreaPos* logPrev){
	SerialNo* maximum = out_index;
	*maximum = 0;
	*out_pos = 0;
	bool overflow = false;
	PageOffs page_offs = pagesPerBlock * (block + area*blocksPerArea);
	for(unsigned int i = 0; i < pagesPerBlock; i++) {
		PageAbs page = i + page_offs;
		char buf[sizeof(SerialNo) + sizeof(AreaPos) + sizeof(AreaPos)];
		Result r = dev->driver->readPage(page, buf,
				sizeof(SerialNo) + sizeof(AreaPos) + sizeof(AreaPos));
		if(r != Result::ok){
			if(r == Result::biterrorCorrected){
				//TODO trigger SB rewrite. AS may be invalid at this point.
				PAFFS_DBG(PAFFS_TRACE_ALWAYS, "Corrected biterror, but we do not yet write corrected version back to flash.");
			}else{
				return r;
			}
		}
		SerialNo *no = reinterpret_cast<SerialNo*>(buf);
		//PAFFS_DBG_S(PAFFS_TRACE_VERBOSE, "Read Page %" PRIu64 " successful", getPageNumber(addr, dev));
		if(*no == emptySerial){
			// Unprogrammed, therefore empty
			if(*maximum != 0 || overflow)
				return Result::ok;
			return Result::nf;
		}

		if(*no > *maximum || *no == 0){		//==0 if overflow occured
			overflow = *no == 0;
			*out_pos = i + page_offs;
			*maximum = *no;
			memcpy(logPrev, &buf[sizeof(SerialNo)], sizeof(AreaPos));
			memcpy(next, &buf[sizeof(SerialNo) + sizeof(SerialNo)], sizeof(AreaPos));
		}
	}

	return Result::ok;
}

/**
 * This assumes that the area of the Anchor entry does not change.
 */
Result Superblock::insertNewAnchorEntry(Addr logPrev, AreaPos *directArea, AnchorEntry* entry){
	if(dev->areaMap[extractLogicalArea(logPrev)].position != *directArea){
		PAFFS_DBG(PAFFS_TRACE_BUG, "Logical (log: %d->%d) and direct Address (%d) differ!",
				extractLogicalArea(logPrev), dev->areaMap[extractLogicalArea(logPrev)].position,
				*directArea);
		return Result::bug;
	}

	if(dev->areaMap[extractLogicalArea(logPrev)].type != AreaType::superblock){
		PAFFS_DBG(PAFFS_TRACE_BUG, "Tried to write superIndex outside of superblock Area");
		return Result::bug;
	}
	if(sizeof(AnchorEntry) > dataBytesPerPage){
		PAFFS_DBG(PAFFS_TRACE_ERROR, "Anchor entry (%zu) is bigger than page (%d)!", sizeof(AnchorEntry), dataBytesPerPage);
		return Result::fail;
	}
	entry->logPrev = 0; //In Anchor entry, this is always zero
	PageOffs page;
	Result r = findFirstFreeEntryInArea(*directArea, &page, 1);
	if(r == Result::nf){
		PAFFS_DBG_S(PAFFS_TRACE_SUPERBLOCK, "Cycled Anchor Area");
		//just start at first page again, we do not look for other areas as Anchor is always at 0
		page = 0;
	}else if(r != Result::ok){
		PAFFS_DBG(PAFFS_TRACE_ERROR, "Could not insert new Anchor Entry!");
		return r;
	}

	r = handleBlockOverflow(*directArea * totalPagesPerArea + page, logPrev, &entry->no);
	if(r != Result::ok){
		PAFFS_DBG(PAFFS_TRACE_ERROR, "Could not handle Block overflow!");
		return r;
	}
	PAFFS_DBG_S(PAFFS_TRACE_SUPERBLOCK, "Writing Anchor to phys. Area %" PRIu32 ", "
			"page %" PRIu32 " pointing to area %" PRIu32, *directArea, page, entry->jumpPadArea);
	return dev->driver->writePage(*directArea * totalPagesPerArea+ page, entry, sizeof(AnchorEntry));
}

Result Superblock::readAnchorEntry(Addr addr, AnchorEntry* entry){
	if(sizeof(AnchorEntry) > dataBytesPerPage){
		PAFFS_DBG(PAFFS_TRACE_BUG, "AnchorEntry bigger than dataBytes per Page! (%zu, %u)",
				sizeof(AnchorEntry), dataBytesPerPage);
		return Result::nimpl;
	}
	if(traceMask & PAFFS_TRACE_SUPERBLOCK && extractLogicalArea(addr) != 0){
		PAFFS_DBG(PAFFS_TRACE_BUG, "Read Anchor entry at phys. area %" PRIu32 ", "
				"but must only be in area 0!", extractLogicalArea(addr));
		return Result::bug;
	}
	PAFFS_DBG_S(PAFFS_TRACE_SUPERBLOCK, "Reading Anchor entry at phys. area %" PRIu32 " page %" PRIu32,
			extractLogicalArea(addr), extractPage(addr));
	//No check of areaType because we may not have an AreaMap
	Result r = dev->driver->readPage(
			getPageNumberFromDirect(addr), entry, sizeof(AnchorEntry));

	if(r == Result::biterrorCorrected){
		//TODO trigger SB rewrite. AS may be invalid at this point.
		PAFFS_DBG(PAFFS_TRACE_ALWAYS, "Corrected biterror, but we do not yet write corrected version back to flash.");
		return Result::ok;
	}
	return r;
}

Result Superblock::insertNewJumpPadEntry(Addr logPrev, AreaPos *directArea, JumpPadEntry* entry){
	if(dev->areaMap[extractLogicalArea(logPrev)].position != *directArea){
		PAFFS_DBG(PAFFS_TRACE_BUG, "Logical (log: %d->%d) and direct Address (%d) differ!",
				extractLogicalArea(logPrev), dev->areaMap[extractLogicalArea(logPrev)].position,
				*directArea);
		return Result::bug;
	}
	if(*directArea == 0){
		PAFFS_DBG(PAFFS_TRACE_BUG, "Tried to write not-anchor chain Elem to area 0!");
		return Result::bug;
	}
	if(dev->areaMap[extractLogicalArea(logPrev)].type != AreaType::superblock){
		PAFFS_DBG(PAFFS_TRACE_BUG, "Tried to write superIndex outside of superblock Area");
		return Result::bug;
	}
	if(sizeof(AnchorEntry) > dataBytesPerPage){
		PAFFS_DBG(PAFFS_TRACE_ERROR, "Anchor entry (%zu) is bigger than page (%d)!", sizeof(AnchorEntry), dataBytesPerPage);
		return Result::fail;
	}
	PageOffs page;
	Result r = findFirstFreeEntryInArea(*directArea, &page, 1);
	entry->logPrev = 0;
	if(r == Result::nf){
		AreaPos p = findBestNextFreeArea(extractLogicalArea(logPrev));
		if(p != extractLogicalArea(logPrev)){
			entry->logPrev = extractLogicalArea(logPrev);
			PAFFS_DBG_S(PAFFS_TRACE_SUPERBLOCK, "Moving JumpPad area from "
					"log. %" PRIu32 " to log. %" PRIu32, entry->logPrev, p);
			*directArea = dev->areaMap[p].position;
		}else{
			PAFFS_DBG_S(PAFFS_TRACE_SUPERBLOCK, "Warning: reusing JumpPad area.");
		}
		page = 0;
	}else if(r != Result::ok){
		PAFFS_DBG(PAFFS_TRACE_ERROR, "Could not insert new JumpPad!");
		return r;
	}

	r = handleBlockOverflow(*directArea * totalPagesPerArea + page, logPrev, &entry->no);
	if(r != Result::ok){
		PAFFS_DBG(PAFFS_TRACE_ERROR, "Could not handle Block overflow!");
		return r;
	}
	PAFFS_DBG_S(PAFFS_TRACE_SUPERBLOCK, "Writing jumpPad to phys. Area %" PRIu32 ", "
			"page %" PRIu32 " pointing to area %" PRIu32, *directArea, page, entry->nextArea);
	return dev->driver->writePage(*directArea * totalPagesPerArea + page, entry, sizeof(JumpPadEntry));
}

/*Result Superblock::readJumpPadEntry(Addr addr, JumpPadEntry* entry){
	if(sizeof(JumpPadEntry) > dataBytesPerPage){
		PAFFS_DBG(PAFFS_TRACE_BUG, "JumpPadEntry bigger than dataBytes per Page! (%zu, %u)",
				sizeof(JumpPadEntry), dataBytesPerPage);
		return Result::nimpl;
	}
	PAFFS_DBG_S(PAFFS_TRACE_SUPERBLOCK, "Reading JumpPad entry at phys. area %" PRIu32 " page %" PRIu32,
			extractLogicalArea(addr), extractPage(addr));
	//No check of areaType because we may not have an AreaMap
	return dev->driver->readPage(getPageNumberFromDirect(addr), entry, sizeof(JumpPadEntry));
}*/

Result Superblock::insertNewSuperIndex(Addr logPrev, AreaPos *directArea, SuperIndex* entry){
	if(dev->areaMap[extractLogicalArea(logPrev)].position != *directArea){
		PAFFS_DBG(PAFFS_TRACE_BUG, "Logical (log: %d->%d) and direct Address (%d) differ!",
				extractLogicalArea(logPrev), dev->areaMap[extractLogicalArea(logPrev)].position,
				*directArea);
		return Result::bug;
	}

	if(dev->areaMap[extractLogicalArea(logPrev)].type != AreaType::superblock){
		PAFFS_DBG(PAFFS_TRACE_BUG, "Tried to write superIndex outside of superblock Area");
		return Result::bug;
	}
	if(sizeof(AnchorEntry) > dataBytesPerPage){
		PAFFS_DBG(PAFFS_TRACE_ERROR, "Anchor entry (%zu) is bigger than page (%d)!", sizeof(AnchorEntry), dataBytesPerPage);
		return Result::fail;
	}
	PageOffs page;
	unsigned int neededASes = 0;
	for(unsigned int i = 0; i < 2; i++){
		if(entry->asPositions[i] > 0)
			neededASes++;
	}
	unsigned int needed_bytes = sizeof(Addr) + sizeof(AreaPos) +
		areasNo * sizeof(Area)
		+ neededASes * dataPagesPerArea / 8; /* One bit per entry, two entrys for INDEX and DATA section*/
	if(dataPagesPerArea % 8 != 0)
		needed_bytes++;

	//Every page needs its serial Number
	unsigned int needed_pages = needed_bytes / (dataBytesPerPage - sizeof(SerialNo)) + 1;

	Result r = findFirstFreeEntryInArea(*directArea, &page, needed_pages);
	entry->logPrev = 0;
	if(r == Result::nf){
		AreaPos p = findBestNextFreeArea(extractLogicalArea(logPrev));
		if(p != extractLogicalArea(logPrev)){
			entry->logPrev = extractLogicalArea(logPrev);
			PAFFS_DBG_S(PAFFS_TRACE_SUPERBLOCK, "Moving Superindex area from "
					"log. %" PRIu32 " to log. %" PRIu32, entry->logPrev, p);
			*directArea = dev->areaMap[p].position;
		}else{
			PAFFS_DBG_S(PAFFS_TRACE_SUPERBLOCK, "Warning: reusing SuperIndex area.");
		}
		page = 0;
	}else if(r != Result::ok){
		PAFFS_DBG(PAFFS_TRACE_ERROR, "Could not insert new superIndex!");
		return r;
	}

	r = handleBlockOverflow(*directArea * totalPagesPerArea + page, logPrev, &entry->no);
	if(r != Result::ok){
		PAFFS_DBG(PAFFS_TRACE_ERROR, "Could not handle Block overflow!");
		return r;
	}

	PAFFS_DBG_S(PAFFS_TRACE_SUPERBLOCK, "Writing superIndex to phys. Area %" PRIu32 ", page %" PRIu32, *directArea, page);
	return writeSuperPageIndex(*directArea * totalPagesPerArea + page, entry);
}

//warn: Make sure that free space is sufficient!
Result Superblock::writeSuperPageIndex(PageAbs pageStart, SuperIndex* entry){
	unsigned int neededASes = 0;
	for(unsigned int i = 0; i < 2; i++){
		if(entry->asPositions[i] > 0)
			neededASes++;
	}

	//note: Serial number is inserted on the first bytes for every page later on.
	unsigned int needed_bytes = sizeof(Addr) + sizeof(AreaPos) +
		areasNo * sizeof(Area)
		+ neededASes * dataPagesPerArea / 8; /* One bit per entry, two entrys for INDEX and DATA section*/
	if(dataPagesPerArea % 8 != 0)
		needed_bytes++;

	//Every page needs its serial Number
	unsigned int needed_pages = needed_bytes / (dataBytesPerPage - sizeof(SerialNo)) + 1;
	PAFFS_DBG_S(PAFFS_TRACE_SUPERBLOCK, "Minimum Pages needed to write SuperIndex: %d (%d bytes, %d AS'es)", needed_pages, needed_bytes, neededASes);

	unsigned int pointer = 0;
	char buf[needed_bytes];
	memset(buf, 0, needed_bytes);
	memcpy(&buf[pointer], &entry->logPrev, sizeof(AreaPos));
	pointer += sizeof(AreaPos);
	memcpy(&buf[pointer], &entry->rootNode, sizeof(Addr));
	pointer += sizeof(Addr);
	unsigned char pospos = 0;	//Stupid name

	for(unsigned int i = 0; i < areasNo; i++){
		if((entry->areaMap[i].type == AreaType::index || entry->areaMap[i].type == AreaType::data) && entry->areaMap[i].status == AreaStatus::active){
			entry->asPositions[pospos++] = i;
		}

		memcpy(&buf[pointer], &entry->areaMap[i], sizeof(Area));
		pointer += sizeof(Area);
	}

	//Collect area summaries and pack them
	for(unsigned int i = 0; i < 2; i++){
		if(entry->asPositions[i] <= 0)
			continue;
		for(unsigned int j = 0; j < dataPagesPerArea; j++){
			if(entry->areaSummary[i][j] != SummaryEntry::dirty)
				buf[pointer + j/8] |= 1 << j%8;
		}
		pointer += dataPagesPerArea / 8;
	}

	PAFFS_DBG_S(PAFFS_TRACE_SUPERBLOCK, "%u bytes have been written to Buffer", pointer);

	pointer = 0;
	Result r;
	char pagebuf[dataBytesPerPage];
	for(unsigned page = 0; page < needed_pages; page++){
		unsigned int btw =
				pointer + dataBytesPerPage - sizeof(SerialNo) < needed_bytes ?
						dataBytesPerPage - sizeof(SerialNo) : needed_bytes - pointer;
		//This inserts the serial number at the first Bytes in every page
		memcpy(pagebuf, &entry->no, sizeof(SerialNo));
		memcpy(&pagebuf[sizeof(SerialNo)], &buf[pointer], btw);
		r = dev->driver->writePage(pageStart + page, pagebuf, btw + sizeof(SerialNo));
		if(r != Result::ok)
			return r;
		pointer += btw;
	}

	return Result::ok;
}

Result Superblock::readSuperPageIndex(Addr addr, SuperIndex* entry, bool withAreaMap){
	Result r;
	if(!withAreaMap){
		r = dev->driver->readPage(
				 getPageNumberFromDirect(addr), entry, sizeof(SerialNo) + sizeof(Addr));
		if(r == Result::biterrorCorrected){
			//TODO trigger SB rewrite. AS may be invalid at this point.
			PAFFS_DBG(PAFFS_TRACE_ALWAYS, "Corrected biterror, but we do not yet write corrected version back to flash.");
			return Result::ok;
		}
		return r;
	}
	if(entry->areaMap == NULL)
		return Result::einval;

	if(extractPage(addr) > totalPagesPerArea){
		PAFFS_DBG(PAFFS_TRACE_BUG, "Read SuperPage at page %" PRIu32 " of area %" PRIu32 ", "
				"but an area is only %" PRIu32 " pages wide!",
				extractPage(addr), extractLogicalArea(addr), totalPagesPerArea);
	}

	PAFFS_DBG_S(PAFFS_TRACE_SUPERBLOCK, "Reading SuperIndex at phys. area %" PRIu32 " page %" PRIu32,
			extractLogicalArea(addr), extractPage(addr));
	//TODO: Just read the appropiate number of area Summaries
	//when dynamic ASses are allowed.

	//note: Serial number is inserted on the first bytes for every page later on.
	unsigned int needed_bytes = sizeof(Addr) + sizeof(AreaPos) +
		areasNo * sizeof(Area)
		+ 2 * dataPagesPerArea / 8; /* One bit per entry, two entries for INDEX and DATA section. Others dont have summaries*/

	unsigned int needed_pages = needed_bytes / (dataBytesPerPage - sizeof(SerialNo)) + 1;
	PAFFS_DBG_S(PAFFS_TRACE_SUPERBLOCK, "Maximum Pages needed to read SuperIndex: %d (%d bytes, 2 AS'es)", needed_pages, needed_bytes);

	char buf[needed_bytes];
	memset(buf, 0, needed_bytes);
	uint32_t pointer = 0;
	PageAbs pageBase = getPageNumberFromDirect(addr);
	entry->no = emptySerial;
	unsigned char pagebuf[dataBytesPerPage];
	SerialNo localSerialTmp;
	for(unsigned int page = 0; page < needed_pages; page++){
		unsigned int btr =
				pointer + dataBytesPerPage - sizeof(SerialNo) < needed_bytes ?
				dataBytesPerPage - sizeof(SerialNo) : needed_bytes - pointer;
		r = dev->driver->readPage(pageBase + page, pagebuf, btr + sizeof(SerialNo));
		if(r != Result::ok){
			if(r == Result::biterrorCorrected){
				//TODO trigger SB rewrite. AS may be invalid at this point.
				PAFFS_DBG(PAFFS_TRACE_ALWAYS, "Corrected biterror, but we do not yet write corrected version back to flash.");
				return Result::ok;
			}
			return r;
		}

		memcpy(&localSerialTmp, pagebuf, sizeof(SerialNo));
		if(localSerialTmp == emptySerial){
			PAFFS_DBG(PAFFS_TRACE_ERROR, "Got empty SerialNo during SuperPage read! "
					"PageOffs: %" PRIu64 ", page: %u", pageBase, page);
			return Result::bug;
		}
		if(entry->no != emptySerial && localSerialTmp != entry->no){
			PAFFS_DBG(PAFFS_TRACE_ERROR, "Got different Serials during SuperPage read! "
					"Was: %" PRIu32 ", should %" PRIu32, localSerialTmp, entry->no);
			return Result::bug;
		}
		if(entry->no == emptySerial){
			entry->no = localSerialTmp;
		}

		memcpy(&buf[pointer], &pagebuf[sizeof(SerialNo)], btr);
		pointer += btr;
	}
	//buffer ready
	PAFFS_DBG_S(PAFFS_TRACE_WRITE, "SuperIndex Buffer was filled with %" PRIu32 " Bytes.", pointer);

	pointer = 0;
	memcpy(&entry->logPrev, &buf[pointer], sizeof(AreaPos));
	pointer += sizeof(AreaPos);
	memcpy(&entry->rootNode, &buf[pointer], sizeof(Addr));
	pointer += sizeof(Addr);
	entry->asPositions[0] = 0;
	entry->asPositions[1] = 0;
	unsigned char pospos = 0;	//Stupid name
	for(unsigned int i = 0; i < areasNo; i++){
		memcpy(&entry->areaMap[i], &buf[pointer], sizeof(Area));
		pointer += sizeof(Area);
		if((dev->areaMap[i].type == AreaType::data || dev->areaMap[i].type == AreaType::index)
				&& entry->areaMap[i].status == AreaStatus::active)
			entry->asPositions[pospos++] = i;
	}

	for(unsigned int i = 0; i < 2; i++){
		if(entry->asPositions[i] <= 0)
			continue;

		for(unsigned int j = 0; j < dataPagesPerArea; j++){
			if(buf[pointer + j/8] & 1 << j%8){
				//TODO: Normally, we would check in the OOB for a Checksum or so, which is present all the time
				Addr tmp = combineAddress(entry->asPositions[i], j);
				r = dev->driver->readPage(getPageNumberFromDirect(tmp), pagebuf, dataBytesPerPage);
				if(r != Result::ok){
					if(r == Result::biterrorCorrected){
						//TODO trigger SB rewrite. AS may be invalid at this point.
						PAFFS_DBG(PAFFS_TRACE_ALWAYS, "Corrected biterror, but we do not yet write corrected version back to flash.");
						return Result::ok;
					}
					return r;
				}
				bool contains_data = false;
				for(unsigned int byte = 0; byte < dataBytesPerPage; byte++){
					if(pagebuf[byte] != 0xFF){
						contains_data = true;
						break;
					}
				}
				if(contains_data)
					entry->areaSummary[i][j] = SummaryEntry::used;
				else
					entry->areaSummary[i][j] = SummaryEntry::free;
			}else{
				entry->areaSummary[i][j] = SummaryEntry::dirty;
			}
		}
		pointer += dataPagesPerArea / 8;
	}

	return Result::ok;
}

Result Superblock::handleBlockOverflow(PageAbs newPage, Addr logPrev, SerialNo *serial){
	BlockAbs newblock = newPage / pagesPerBlock;
	if(newblock != getBlockNumber(logPrev, dev)){
		//reset serial no if we start a new block
		PAFFS_DBG_S(PAFFS_TRACE_SUPERBLOCK, "Deleting phys. Area %d, block %d"
				" (abs: %d, new abs on %d) for chain Entry",
				extractLogicalArea(logPrev), extractPage(logPrev) / pagesPerBlock,
				getBlockNumber(logPrev, dev), newblock);
		*serial = 0;
		Result r = deleteSuperBlock(extractLogicalArea(logPrev), extractPage(logPrev) / pagesPerBlock);
		if(r != Result::ok){
			PAFFS_DBG(PAFFS_TRACE_ERROR, "Could not delete block of chain Entry! BlockAbs: %" PRIu32, getBlockNumber(logPrev, dev));
			return r;
		}
	}
	return Result::ok;
}

Result Superblock::deleteSuperBlock(AreaPos area, uint8_t block) {
	if(dev->areaMap[area].type != AreaType::superblock){
		PAFFS_DBG(PAFFS_TRACE_BUG, "Tried to delete Block outside of SUPARBLCOKAREA");
		return Result::bug;
	}
	//blocks are deleted sequentially, erasecount is for whole area erases
	if(block == blocksPerArea)
		dev->areaMap[area].erasecount++;

	BlockAbs block_offs = dev->areaMap[area].position * blocksPerArea;
	return dev->driver->eraseBlock(block_offs + block);
}

AreaPos Superblock::findBestNextFreeArea(AreaPos logPrev){
	PAFFS_DBG_S(PAFFS_TRACE_SUPERBLOCK, "log. Area %" PRIu32 " is full, finding new one...", logPrev);
	for(AreaPos i = 1; i < areasNo; i++){
		if(dev->areaMap[i].status == AreaStatus::empty){
			// Following changes to areaMap may not be persistent if SuperIndex was already written
			dev->areaMap[i].status = AreaStatus::active;
			/**
			 * The area will be empty after the next handleBlockOverflow
			 * This allows other SuperIndex areas to switch to this one if flushed in same commit.
			 * This should be OK given the better performance in low space environments
			 * and that the replacing Area will be a higher order and
			 * thus less frequently written to.
			 */
			dev->areaMap[logPrev].status = AreaStatus::empty;
			//Omitting ..type because it will be overwritten upon mount
			PAFFS_DBG_S(PAFFS_TRACE_SUPERBLOCK, "Found log. %" PRIu32, i);
			return i;
		}
	}
	PAFFS_DBG(PAFFS_TRACE_ERROR, "Warning: Using same area (log. %" PRIu32 ") for new cycle!", logPrev);
	return logPrev;
}
}
