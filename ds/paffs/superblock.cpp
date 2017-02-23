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
	}

	return rootnode_addr;
}


void Superblock::printSuperIndex(superIndex* ind){
	printf("No:\t\t%d\n", ind->no);
	printf("Rootnode addr.: \t%u:%u\n",
			extractLogicalArea(ind->rootNode),
			extractPage(ind->rootNode));
	printf("areaMap:\n");
	AreaPos asOffs = 0;
	for(AreaPos i = 0; i < 8; i ++){
		printf("\t%d->%d\n", i, ind->areaMap[i].position);
		printf("\tType: %s\n", area_names[ind->areaMap[i].type]);
		if(asOffs < 2 && i == ind->asPositions[asOffs]){
			unsigned int free = 0, used = 0, dirty = 0;
			for(unsigned int j = 0; j < dev->param->dataPagesPerArea; j++){
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


Result Superblock::getAddrOfMostRecentSuperIndex(Addr *out){

	uint32_t pos1, index1;
	Result r1 = findMostRecentEntryInBlock(0, 0, &pos1, &index1);
	if(r1 != Result::ok && r1 != Result::nf)
		return r1;

	uint32_t pos2, index2;
	Result r2 = findMostRecentEntryInBlock(0, 1, &pos2, &index2);
	if(r2 != Result::ok && r2 != Result::nf)
		return r2;

	if(r1 == Result::nf && r2 == Result::nf)
		return Result::nf;

	//Special case where block offset is zero
	*out = index1 >= index2 ? combineAddress(0, pos1) : combineAddress(0, pos2);

	return Result::ok;
}

Result Superblock::commitSuperIndex(superIndex *newIndex){
	unsigned int needed_bytes = sizeof(SerialNo) + sizeof(Addr) +
				dev->param->areasNo * sizeof(Area)
				+ 2 * dev->param->dataPagesPerArea / 8; /* One bit per entry, two entrys for INDEX and DATA section*/
	unsigned int needed_pages = needed_bytes / dataBytesPerPage + 1;
	PAFFS_DBG_S(PAFFS_TRACE_SUPERBLOCK, "Minimum Pages needed to read former SuperIndex: %d (%d bytes, 2 AS'es)", needed_pages, needed_bytes);

	uint32_t rel_page1 = 0;
	Result r1 = findFirstFreeEntryInBlock(0, 0, &rel_page1, needed_pages);
	if(r1 != Result::ok && r1 != Result::nf){
		PAFFS_DBG(PAFFS_TRACE_ERROR, "Error while getting space on first block for new superIndex!");
		return r1;
	}

	uint32_t rel_page2 = 0;
	Result r2 = findFirstFreeEntryInBlock(0, 1, &rel_page2, needed_pages);
	if(r2 != Result::ok && r2 != Result::nf){
		PAFFS_DBG(PAFFS_TRACE_ERROR, "Error while getting space on second block for new superIndex!");
		return r2;
	}

	uint8_t chosen_block = 0;

	if(r1 == Result::ok || r2 == Result::ok){
		//some Blocks contain free space
		if(r1 == Result::ok && r2 == Result::ok){
			//both are contain free blocks
			if(rel_page1 == 0 && rel_page2 == 0){
				//Both completely free, just choose something
				chosen_block = 0;
			}else if(rel_page1 == 0 && rel_page2 != 0){
				//Block 0 wiped, Block 1 is current
				chosen_block = 1;
			}else if(rel_page1 != 0 && rel_page2 == 0){
				//Block 0 is current, Block 1 wiped
				chosen_block = 0;
			}else {
				//Both contain Data!?
				PAFFS_DBG(PAFFS_TRACE_BUG, "Both Superblocks contain Data!");
				return Result::bug;
			}
		}else if(r1 == Result::ok){
			//First is current, second is full
			chosen_block = 0;
		}else{
			//First is full, second is current
			chosen_block = 1;
		}
	}else{
		PAFFS_DBG(PAFFS_TRACE_BUG, "Both Superblocks are full?");
		return Result::bug;
	}

	Addr target = chosen_block == 0 ? combineAddress(0, rel_page1) : combineAddress(0, rel_page2 + dev->param->pagesPerBlock);

	Addr lastEntry;
 	Result r = getAddrOfMostRecentSuperIndex(&lastEntry);
	if(r != Result::ok && r != Result::nf){
		PAFFS_DBG(PAFFS_TRACE_ERROR, "Could not determine Address of last SuperIndex!");
		return r;
	}

	superIndex lastIndex = {0};

	if(r != Result::nf){
		r = readSuperPageIndex(lastEntry, &lastIndex, false);
		if(r != Result::ok)
			return r;
	}

	newIndex->no = lastIndex.no+1;
	newIndex->rootNode = rootnode_addr;
	newIndex->areaMap = dev->areaMap;	//This should already be done in cachefunction

	if(traceMask & PAFFS_TRACE_SUPERBLOCK){
		printf("write Super Index:\n");
		printSuperIndex(newIndex);
	}

	r = writeSuperPageIndex(target, newIndex);
	if(r != Result::ok)
		return r;

	//Handle deletion.
	if(r1 == Result::nf){
		return deleteAnchorBlock(0, 0);
	}

	if(r1 == Result::nf){
		return deleteAnchorBlock(0, 1);
	}

	rootnode_dirty = false;

	return Result::ok;
}

Result Superblock::readSuperIndex(superIndex* index){
	Addr addr;
	Result r = getAddrOfMostRecentSuperIndex(&addr);
	if(r != Result::ok)
		return r;

	PAFFS_DBG_S(PAFFS_TRACE_SUPERBLOCK, "Found Super Index at %u:%u\n", extractLogicalArea(addr), extractPage(addr));

	r = readSuperPageIndex(addr, index, true);
	if(r != Result::ok){
		PAFFS_DBG(PAFFS_TRACE_ERROR, "Could not read Super Index!");
		return r;
	}

	if(traceMask & PAFFS_TRACE_SUPERBLOCK){
		printf("Read Super Index:\n");
		printSuperIndex(index);
	}

	rootnode_addr = index->rootNode;
	rootnode_dirty = false;
	return Result::ok;
}

// Superblock related

Result Superblock::findFirstFreeEntryInBlock(uint32_t area, uint8_t block, uint32_t* out_pos, unsigned int required_pages){
	unsigned int in_a_row = 0;
	uint64_t page_offs = dev->param->pagesPerBlock * block;
	for(unsigned int i = 0; i < dev->param->pagesPerBlock; i++) {
		Addr addr = combineAddress(area, i + page_offs);
		uint32_t no;
		Result r = dev->driver->readPage(getPageNumber(addr, dev), &no, sizeof(uint32_t));
		if(r != Result::ok)
			return r;
		if(no != 0xFFFFFFFF){
			if(in_a_row != 0){
				*out_pos = 0;
				in_a_row = 0;
			}
			continue;
		}

		// Unprogrammed, therefore empty
		*out_pos = i;
		if(++in_a_row == required_pages)
			return Result::ok;
	}
	return Result::nf;
}

Result Superblock::findMostRecentEntryInBlock(uint32_t area, uint8_t block, uint32_t* out_pos, uint32_t* out_index){
	uint32_t* maximum = out_index;
	*maximum = 0;
	*out_pos = 0;
	uint32_t page_offs = dev->param->pagesPerBlock * block;
	for(unsigned int i = 0; i < dev->param->pagesPerBlock; i++) {
		Addr addr = combineAddress(area, i + page_offs);
		uint32_t no;
		Result r = dev->driver->readPage(getPageNumber(addr, dev), &no, sizeof(uint32_t));
		if(r != Result::ok)
			return r;
		if(no == 0xFFFFFFFF){
			// Unprogrammed, therefore empty
			if(*maximum != 0)
				return Result::ok;
			return Result::nf;
		}

		if(no > *maximum){
			*out_pos = i + page_offs;
			*maximum = no;
		}
	}

	return Result::ok;
}


Result Superblock::writeAnchorEntry(Addr addr, AnchorEntry* entry){
	//Currently not implemented to simplify Find-Strategy
	return Result::nimpl;
}
Result Superblock::readAnchorEntry(Addr addr, AnchorEntry* entry){
	//Currently not implemented to simplify Find-Strategy
	return Result::nimpl;
}

Result Superblock::deleteAnchorBlock(uint32_t area, uint8_t block) {
	if(dev->areaMap[area].type != AreaType::superblock){
		PAFFS_DBG(PAFFS_TRACE_BUG, "Tried to delete Block outside of SUPARBLCOKAREA");
		return Result::bug;
	}
	dev->areaMap[area].erasecount++;
	uint32_t block_offs = dev->areaMap[area].position * dev->param->blocksPerArea;
	return dev->driver->eraseBlock(block_offs + block);
}

Result Superblock::writeJumpPadEntry(Addr addr, JumpPadEntry* entry){
	//Currently not implemented to simplify Find-Strategy
	return Result::nimpl;
}

Result Superblock::readJumpPadEntry(Addr addr, JumpPadEntry* entry){
	//Currently not implemented to simplify Find-Strategy
	return Result::nimpl;
}


//todo: Make sure that free space is sufficient!
Result Superblock::writeSuperPageIndex(Addr addr, superIndex* entry){
	if(dev->areaMap[extractLogicalArea(addr)].type != AreaType::superblock){
		PAFFS_DBG(PAFFS_TRACE_BUG, "Tried to write superIndex outside of superblock Area");
		return Result::bug;
	}

	unsigned int neededASes = 0;
	for(unsigned int i = 0; i < 2; i++){
		if(entry->asPositions[i] > 0)
			neededASes++;
	}

	unsigned int needed_bytes = sizeof(SerialNo) + sizeof(Addr) +
		dev->param->areasNo * sizeof(Area)
		+ neededASes * dev->param->dataPagesPerArea / 8; /* One bit per entry, two entrys for INDEX and DATA section*/

	unsigned int needed_pages = needed_bytes / dataBytesPerPage + 1;
	PAFFS_DBG_S(PAFFS_TRACE_SUPERBLOCK, "Minimum Pages needed to write SuperIndex: %d (%d bytes, %d AS'es)", needed_pages, needed_bytes, neededASes);

	unsigned int pointer = 0;
	char buf[needed_bytes];
	memset(buf, 0, needed_bytes);
	memcpy(buf, &entry->no, sizeof(SerialNo));
	pointer += sizeof(SerialNo);
	memcpy(&buf[pointer], &entry->rootNode, sizeof(Addr));
	pointer += sizeof(Addr);
	unsigned char pospos = 0;	//Stupid name

	for(unsigned int i = 0; i < dev->param->areasNo; i++){
		if((entry->areaMap[i].type == AreaType::index || entry->areaMap[i].type == AreaType::data) && entry->areaMap[i].status == AreaStatus::active){
			entry->asPositions[pospos++] = i;
		}

		memcpy(&buf[pointer], &entry->areaMap[i], sizeof(Area));
		pointer += sizeof(Area);
	}

	for(unsigned int i = 0; i < 2; i++){
		if(entry->asPositions[i] <= 0)
			continue;
		for(unsigned int j = 0; j < dev->param->dataPagesPerArea; j++){
			if(entry->areaSummary[i][j] != SummaryEntry::dirty)
				buf[pointer + j/8] |= 1 << j%8;
		}
		pointer += dev->param->dataPagesPerArea / 8;
	}

	PAFFS_DBG_S(PAFFS_TRACE_SUPERBLOCK, "%u bytes have been written to Buffer", pointer);

	pointer = 0;
	uint64_t page_offs = getPageNumber(addr, dev);
	Result r;
	for(unsigned page = 0; page < needed_pages; page++){
		unsigned int btw = pointer + dev->param->dataBytesPerPage < needed_bytes ? dev->param->dataBytesPerPage
							: needed_bytes - pointer;
		r = dev->driver->writePage(page_offs + page, &buf[pointer], btw);
		if(r != Result::ok)
			return r;

		pointer += btw;
	}

	return Result::ok;
}

Result Superblock::readSuperPageIndex(Addr addr, superIndex* entry, bool withAreaMap){
	if(!withAreaMap)
		 return dev->driver->readPage(getPageNumber(addr, dev), entry, sizeof(uint32_t) + sizeof(Addr));

	if(entry->areaMap == NULL)
		return Result::einval;

	unsigned int needed_bytes = sizeof(SerialNo) + sizeof(Addr) +
		dev->param->areasNo * sizeof(Area)
		+ 2 * dev->param->dataPagesPerArea / 8; /* One bit per entry, two entries for INDEX and DATA section. Others dont have summaries*/

	unsigned int needed_pages = needed_bytes / dataBytesPerPage + 1;
	PAFFS_DBG_S(PAFFS_TRACE_SUPERBLOCK, "Maximum Pages needed to read SuperIndex: %d (%d bytes, 2 AS'es)", needed_pages, needed_bytes);

	char buf[needed_bytes];
	memset(buf, 0, needed_bytes);
	uint32_t pointer = 0;
	uint64_t page_offs = getPageNumber(addr, dev);
	Result r;
	for(unsigned page = 0; page < needed_pages; page++){
		unsigned int btr = pointer + dev->param->dataBytesPerPage < needed_bytes ? dev->param->dataBytesPerPage
							: needed_bytes - pointer;
		r = dev->driver->readPage(page_offs + page, &buf[pointer], btr);
		if(r != Result::ok)
			return r;

		pointer += btr;
	}
	//buffer ready
	PAFFS_DBG_S(PAFFS_TRACE_WRITE, "SuperIndex Buffer was filled with %u Bytes.", pointer);

	pointer = 0;
	memcpy(&entry->no, buf, sizeof(SerialNo));
	pointer += sizeof(SerialNo);
	memcpy(&entry->rootNode, &buf[pointer], sizeof(Addr));
	pointer += sizeof(Addr);
	entry->asPositions[0] = 0;
	entry->asPositions[1] = 0;
	unsigned char pospos = 0;	//Stupid name
	for(unsigned int i = 0; i < dev->param->areasNo; i++){
		memcpy(&entry->areaMap[i], &buf[pointer], sizeof(Area));
		pointer += sizeof(Area);
		if((dev->areaMap[i].type == AreaType::data || dev->areaMap[i].type == AreaType::index)
				&& entry->areaMap[i].status == AreaStatus::active)
			entry->asPositions[pospos++] = i;
	}

	unsigned char pagebuf[dataBytesPerPage];
	for(unsigned int i = 0; i < 2; i++){
		if(entry->asPositions[i] <= 0)
			continue;

		for(unsigned int j = 0; j < dev->param->dataPagesPerArea; j++){
			if(buf[pointer + j/8] & 1 << j%8){
				//TODO: Normally, we would check in the OOB for a Checksum or so, which is present all the time
				Addr tmp = combineAddress(entry->asPositions[i], j);
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
					entry->areaSummary[i][j] = SummaryEntry::used;
				else
					entry->areaSummary[i][j] = SummaryEntry::free;
			}else{
				entry->areaSummary[i][j] = SummaryEntry::dirty;
			}
		}
		pointer += dev->param->dataPagesPerArea / 8;
	}

	return Result::ok;
}

}
