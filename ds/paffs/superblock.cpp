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

/**
 * @brief Ugh, O(n) with areaCount
 */
Result Superblock::resolveDirectToLogicalPath(AreaPos directPath[jumpPadNo+2],
		AreaPos outPath[jumpPadNo+2]){
	AreaPos p = 0;
	int d = 0;
	for(AreaPos i = 0; i < areasNo; i++){
		p = dev->areaMap[i].position;
		for(d = 0; d < jumpPadNo+2; d++){
			if(p == directPath[d])
				outPath[d] = i;
		}
	}
	return Result::ok;
}

Result Superblock::commitSuperIndex(SuperIndex *newIndex){
	unsigned int needed_bytes = sizeof(SerialNo) + sizeof(Addr) +
				areasNo * sizeof(Area)
				+ 2 * dataPagesPerArea / 8; /* One bit per entry, two entrys for INDEX and DATA section*/
	unsigned int needed_pages = needed_bytes / dataBytesPerPage + 1;
	PAFFS_DBG_S(PAFFS_TRACE_SUPERBLOCK, "Minimum Pages needed to read former SuperIndex: %u (%u bytes, 2 AS'es)", needed_pages, needed_bytes);

	Addr path[jumpPadNo+2];
	SerialNo indexes[jumpPadNo+2];
	Result r = getAddrOfMostRecentSuperIndex(path, indexes);

	if(r == Result::nf){
		PAFFS_DBG(PAFFS_TRACE_ERROR, "Could not find Superpage, maybe it was not written.");
		PAFFS_DBG_S(PAFFS_TRACE_ERROR, "Formatting does not yet build a superpath.");
		//TODO: just fill "path" with the first SuperBlocks we find
		return Result::nimpl;
	}

	if(r != Result::ok){
		PAFFS_DBG(PAFFS_TRACE_ERROR, "Could not determine Address of last SuperIndex!");
		return r;
	}

	//Index of last chain elem (SuperEntry) must not be empty (not found)
	if(indexes[jumpPadNo+1] == emptySerial){
		PAFFS_DBG(PAFFS_TRACE_ERROR, "Determined Address of last SuperIndex, but its SerialNo was empty!");
		PAFFS_DBG_S(PAFFS_TRACE_ERROR, "Area %" PRIu32 ", page %" PRIu32,
				extractLogicalArea(path[jumpPadNo+1]), extractPage(path[jumpPadNo+1]));
		return Result::bug;
	}

	//Get index of last chain elem (SuperEntry) and increase
	newIndex->no = indexes[jumpPadNo+1]+1;
	newIndex->rootNode = rootnode_addr;
	newIndex->areaMap = dev->areaMap;	//This should already be done in cachefunction

	if(traceMask & PAFFS_TRACE_SUPERBLOCK){
		printf("write Super Index:\n");
		printSuperIndex(newIndex);
	}

	AreaPos directAreas[jumpPadNo+2];
	AreaPos logicalAreas[jumpPadNo+2];
	for(int i = 0; i < jumpPadNo+2; i++){
		directAreas[i] = extractLogicalArea(path[i]);
	}


	r = resolveDirectToLogicalPath(directAreas, logicalAreas);
	if(r != Result::ok){
		PAFFS_DBG(PAFFS_TRACE_ERROR, "Could not resolve direct to logical path!");
		return r;
	}

	r = insertNewSuperPage(&logicalAreas[jumpPadNo+1], &directAreas[jumpPadNo+1], newIndex);
	if(r != Result::ok){
		PAFFS_DBG(PAFFS_TRACE_ERROR, "Could not insert new super Page!");
		return r;
	}

	for(int i = jumpPadNo; i > 0; i--){
		JumpPadEntry e = {indexes[i], directAreas[i+1]};
		r = insertNewJumpPadEntry(&logicalAreas[i], &directAreas[i], &e);
		if(r != Result::ok){
			PAFFS_DBG(PAFFS_TRACE_ERROR, "Could not insert new JumpPadEntry (Chain %d)!", i);
			return r;
		}
	}

	AnchorEntry a = {indexes[0], version, stdParam, directAreas[1]};
	r = insertNewAnchorEntry(&logicalAreas[0], &directAreas[0], &a);
	if(r != Result::ok){
		PAFFS_DBG(PAFFS_TRACE_ERROR, "Could not insert new AnchorEntry!");
		return r;
	}

	rootnode_dirty = false;

	return Result::ok;
}

Result Superblock::readSuperIndex(SuperIndex* index){
	Addr path[jumpPadNo+2];
	SerialNo indexes[jumpPadNo+2];
	Result r = getAddrOfMostRecentSuperIndex(path, indexes);
	if(r != Result::ok){
		PAFFS_DBG(PAFFS_TRACE_ERROR, "could not get addr of most recent Superindex");
		return r;
	}
	AnchorEntry e;
	r = readAnchorEntry(path[0], &e);
	if(r != Result::ok){
		PAFFS_DBG(PAFFS_TRACE_ERROR, "Could not read Anchor Entry at %" PRIu32
				":%" PRIu32, extractLogicalArea(path[0]), extractPage(path[0]));
		return r;
	}
	if(memcmp(&stdParam, &e.param, sizeof(Param)) != 0){
		PAFFS_DBG(PAFFS_TRACE_ERROR, "Device parameter differ with own settings!");
		return Result::fail;
	}


	Addr addr = path[jumpPadNo+1];

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

	rootnode_addr = index->rootNode;
	rootnode_dirty = false;
	return Result::ok;
}

Result Superblock::findFirstFreeEntryInArea(AreaPos area, PageOffs* out_pos,
		unsigned int required_pages){
	return Result::nimpl;
}

//out_pos shall point to the first free page
Result Superblock::findFirstFreeEntryInBlock(AreaPos area, uint8_t block,
							PageOffs* out_pos, unsigned int required_pages){
	unsigned int inARow = 0;
	PageOffs pageOffs = pagesPerBlock * block;
	for(unsigned int i = 0; i < pagesPerBlock; i++) {
		PageAbs page = area*totalPagesPerArea + i + pageOffs;
		SerialNo no;
		Result r = dev->driver->readPage(page, &no, sizeof(SerialNo));
		if(r != Result::ok)
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
Result Superblock::getAddrOfMostRecentSuperIndex(Addr path[jumpPadNo+2], SerialNo indexes[jumpPadNo+2]){
	path[0] = 0;
	Result r;
	for(int i = 1; i <= jumpPadNo; i++){
		r = findMostRecentEntryInArea(
				extractLogicalArea(path[i-1]), &path[i], &indexes[i]
		    );
		if(r != Result::ok){
			PAFFS_DBG(PAFFS_TRACE_ERROR, "Could not find a Superpage in Area %" PRIu32,
					extractLogicalArea(path[i-1]));
			return r;
		}
	}

	return Result::ok;
}

/**
 * @param area : *physical* Area in which to look
 * @param out_pos : offset in pages starting from area front where Entry was found
 * @param out_index : The index of the elem found
 *
 * Assumes, that if a block contains a valid Entry, no other block contains
 * another entry. This assumption is correct,
 * because the block has to be deleted, if the cursor jumps to next Block.
 */
Result Superblock::findMostRecentEntryInArea(AreaPos area, Addr* out_pos, SerialNo* out_index){
	if(dev->areaMap[area].type != AreaType::superblock){
		PAFFS_DBG(PAFFS_TRACE_ERROR, "Tried to find Superblock entry in non-"
				"Superblock area! (Tried %" PRIu32 " but was of Type %s)",
				area, areaNames[static_cast<int>(dev->areaMap[area].type)]);
	}
	Result r;
	for(int i = 0; i < blocksPerArea; i++){
		SerialNo serial = 0;
		PageOffs pos = 0;
		r = findMostRecentEntryInBlock(area, i, &pos, &serial);
		if(r == Result::ok){
			*out_pos = combineAddress(area, pos);
			*out_index = serial;
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
Result Superblock::findMostRecentEntryInBlock(AreaPos area, uint8_t block, PageOffs* out_pos, SerialNo* out_index){
	SerialNo* maximum = out_index;
	*maximum = 0;
	*out_pos = 0;
	bool overflow = false;
	PageOffs page_offs = pagesPerBlock * block;
	for(unsigned int i = 0; i < pagesPerBlock; i++) {
		PageAbs page = area*totalPagesPerArea + i + page_offs;
		SerialNo no;
		Result r = dev->driver->readPage(page, &no, sizeof(SerialNo));
		if(r != Result::ok)
			return r;
		//PAFFS_DBG_S(PAFFS_TRACE_VERBOSE, "Read Page %" PRIu64 " successful", getPageNumber(addr, dev));
		if(no == emptySerial){
			// Unprogrammed, therefore empty
			if(*maximum != 0 || overflow)
				return Result::ok;
			return Result::nf;
		}

		if(no > *maximum || no == 0){		//==0 if overflow occured
			overflow = no == 0;
			*out_pos = i + page_offs;
			*maximum = no;
		}
	}

	return Result::ok;
}

/**
 * This assumes that the area of the Anchor entry does not change.
 */
Result Superblock::insertNewAnchorEntry(AreaPos *logarea, AreaPos *directArea, AnchorEntry* entry){
	if(dev->areaMap[*logarea].position != *directArea){
		PAFFS_DBG(PAFFS_TRACE_BUG, "Logical and direct Address differ!");
		return Result::bug;
	}

	if(dev->areaMap[*logarea].type != AreaType::superblock){
		PAFFS_DBG(PAFFS_TRACE_BUG, "Tried to write superIndex outside of superblock Area");
		return Result::bug;
	}
	(void) entry;
	return Result::nimpl;
}

Result Superblock::writeAnchorEntry(AreaPos logarea, Addr addr, AnchorEntry* entry){
	if(sizeof(AnchorEntry) > dataBytesPerPage){
		PAFFS_DBG(PAFFS_TRACE_BUG, "AnchorEntry bigger than dataBytes per Page! (%zu, %u)",
				sizeof(JumpPadEntry), dataBytesPerPage);
		return Result::nimpl;
	}
	if(dev->areaMap[logarea].position != extractLogicalArea(addr)){
		PAFFS_DBG(PAFFS_TRACE_BUG, "Logical and direct Address differ!");
		return Result::bug;
	}
	if(dev->areaMap[logarea].type != AreaType::superblock){
		PAFFS_DBG(PAFFS_TRACE_BUG, "Tried to write superIndex outside of superblock Area");
		return Result::bug;
	}
	return dev->driver->writePage(getPageNumberFromDirect(addr), entry, sizeof(AnchorEntry));
}

Result Superblock::readAnchorEntry(Addr addr, AnchorEntry* entry){
	if(sizeof(AnchorEntry) > dataBytesPerPage){
		PAFFS_DBG(PAFFS_TRACE_BUG, "JumpPadEntry bigger than dataBytes per Page! (%zu, %u)",
				sizeof(AnchorEntry), dataBytesPerPage);
		return Result::nimpl;
	}
	//No check of areaType because we may not have an AreaMap
	return dev->driver->readPage(getPageNumberFromDirect(addr), entry, sizeof(AnchorEntry));
}

Result Superblock::insertNewJumpPadEntry(AreaPos *logarea, AreaPos *directArea, JumpPadEntry* entry){
	if(dev->areaMap[*logarea].position != *directArea){
		PAFFS_DBG(PAFFS_TRACE_BUG, "Logical and direct Address differ!");
		return Result::bug;
	}

	if(dev->areaMap[*logarea].type != AreaType::superblock){
		PAFFS_DBG(PAFFS_TRACE_BUG, "Tried to write superIndex outside of superblock Area");
		return Result::bug;
	}
	(void) entry;
	return Result::nimpl;
}

Result Superblock::writeJumpPadEntry(AreaPos logarea, Addr addr, JumpPadEntry* entry){
	if(sizeof(JumpPadEntry) > dataBytesPerPage){
		PAFFS_DBG(PAFFS_TRACE_BUG, "JumpPadEntry bigger than dataBytes per Page! (%zu, %u)",
				sizeof(JumpPadEntry), dataBytesPerPage);
		return Result::nimpl;
	}
	if(dev->areaMap[logarea].position != extractLogicalArea(addr)){
		PAFFS_DBG(PAFFS_TRACE_BUG, "Logical and direct Address differ!");
		return Result::bug;
	}
	if(dev->areaMap[logarea].type != AreaType::superblock){
		PAFFS_DBG(PAFFS_TRACE_BUG, "Tried to write superIndex outside of superblock Area");
		return Result::bug;
	}
	return dev->driver->writePage(getPageNumberFromDirect(addr), entry, sizeof(JumpPadEntry));
}

Result Superblock::readJumpPadEntry(Addr addr, JumpPadEntry* entry){
	if(sizeof(JumpPadEntry) > dataBytesPerPage){
		PAFFS_DBG(PAFFS_TRACE_BUG, "JumpPadEntry bigger than dataBytes per Page! (%zu, %u)",
				sizeof(JumpPadEntry), dataBytesPerPage);
		return Result::nimpl;
	}
	//No check of areaType because we may not have an AreaMap
	return dev->driver->readPage(getPageNumberFromDirect(addr), entry, sizeof(JumpPadEntry));
}

Result Superblock::insertNewSuperPage(AreaPos *logarea, AreaPos *directArea, SuperIndex* entry){
	if(dev->areaMap[*logarea].position != *directArea){
		PAFFS_DBG(PAFFS_TRACE_BUG, "Logical and direct Address differ!");
		return Result::bug;
	}

	if(dev->areaMap[*logarea].type != AreaType::superblock){
		PAFFS_DBG(PAFFS_TRACE_BUG, "Tried to write superIndex outside of superblock Area");
		return Result::bug;
	}

	return Result::nimpl;
}
//warn: Make sure that free space is sufficient!
Result Superblock::writeSuperPageIndex(AreaPos logarea, Addr addr, SuperIndex* entry){
	if(dev->areaMap[logarea].position != extractLogicalArea(addr)){
		PAFFS_DBG(PAFFS_TRACE_BUG, "Logical and direct Address differ!");
		return Result::bug;
	}

	if(dev->areaMap[logarea].type != AreaType::superblock){
		PAFFS_DBG(PAFFS_TRACE_BUG, "Tried to write superIndex outside of superblock Area");
		return Result::bug;
	}

	unsigned int neededASes = 0;
	for(unsigned int i = 0; i < 2; i++){
		if(entry->asPositions[i] > 0)
			neededASes++;
	}

	//note: Serial number is inserted on the first bytes for every page later on.
	unsigned int needed_bytes = sizeof(Addr) +
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
	uint64_t page_offs = getPageNumberFromDirect(addr);
	Result r;
	char pagebuf[dataBytesPerPage];
	for(unsigned page = 0; page < needed_pages; page++){
		unsigned int btw =
				pointer + dataBytesPerPage - sizeof(SerialNo) < needed_bytes ?
						dataBytesPerPage - sizeof(SerialNo) : needed_bytes - pointer;
		//This inserts the serial number at the first Bytes in every page
		memcpy(pagebuf, &entry->no, sizeof(SerialNo));
		memcpy(&pagebuf[sizeof(SerialNo)], &buf[pointer], btw);
		r = dev->driver->writePage(page_offs + page, pagebuf, btw + sizeof(SerialNo));
		if(r != Result::ok)
			return r;
		pointer += btw;
	}

	return Result::ok;
}

Result Superblock::readSuperPageIndex(Addr addr, SuperIndex* entry, bool withAreaMap){
	if(!withAreaMap)
		 return dev->driver->readPage(getPageNumberFromDirect(addr), entry, sizeof(SerialNo) + sizeof(Addr));

	if(entry->areaMap == NULL)
		return Result::einval;

	//TODO: Just read the appropiate number of area Summaries
	//when dynamic asses are allowed.

	//note: Serial number is inserted on the first bytes for every page later on.
	unsigned int needed_bytes = sizeof(Addr) +
		areasNo * sizeof(Area)
		+ 2 * dataPagesPerArea / 8; /* One bit per entry, two entries for INDEX and DATA section. Others dont have summaries*/

	unsigned int needed_pages = needed_bytes / (dataBytesPerPage - sizeof(SerialNo)) + 1;
	PAFFS_DBG_S(PAFFS_TRACE_SUPERBLOCK, "Maximum Pages needed to read SuperIndex: %d (%d bytes, 2 AS'es)", needed_pages, needed_bytes);

	char buf[needed_bytes];
	memset(buf, 0, needed_bytes);
	uint32_t pointer = 0;
	PageAbs pageBase = getPageNumberFromDirect(addr);
	Result r;
	entry->no = emptySerial;
	unsigned char pagebuf[dataBytesPerPage];
	SerialNo localSerialTmp;
	for(unsigned int page = 0; page < needed_pages; page++){
		unsigned int btr =
				pointer + dataBytesPerPage - sizeof(SerialNo) < needed_bytes ?
				dataBytesPerPage - sizeof(SerialNo) : needed_bytes - pointer;
		r = dev->driver->readPage(pageBase + page, pagebuf, btr + sizeof(SerialNo));
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

		if(r != Result::ok)
			return r;
	}
	//buffer ready
	PAFFS_DBG_S(PAFFS_TRACE_WRITE, "SuperIndex Buffer was filled with %" PRIu32 " Bytes.", pointer);

	pointer = 0;
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

Result Superblock::deleteSuperBlock(AreaPos area, uint8_t block) {
	if(dev->areaMap[area].type != AreaType::superblock){
		PAFFS_DBG(PAFFS_TRACE_BUG, "Tried to delete Block outside of SUPARBLCOKAREA");
		return Result::bug;
	}
	//blocks are deleted sequentially, erasecount is for whole area erases
	if(block == blocksPerArea)
		dev->areaMap[area].erasecount++;

	uint32_t block_offs = dev->areaMap[area].position * blocksPerArea;
	return dev->driver->eraseBlock(block_offs + block);
}

}
