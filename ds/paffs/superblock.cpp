/*
 * superblock.c
 *
 *  Created on: 19.10.2016
 *      Author: rooot
 */

#include "superblock.hpp"
#include "driver/driver.hpp"
#include "area.hpp"
#include <stdlib.h>
#include "dataIO.hpp"

namespace paffs{

static Addr rootnode_addr = 0;
static bool rootnode_dirty = 0;

Result registerRootnode(Dev* dev, Addr addr){
	if(addr == 0)
		PAFFS_DBG(PAFFS_TRACE_BUG, "BUG: Tried to set Rootnode to 0");
	rootnode_addr = addr;
	rootnode_dirty = true;
	return Result::ok;
}

Addr getRootnodeAddr(Dev* dev){
	if(rootnode_addr == 0){
		PAFFS_DBG(PAFFS_TRACE_ERROR, "rootnode_address is 0! Maybe device not mounted?");
	}

	return rootnode_addr;
}


void printSuperIndex(Dev* dev, superIndex* ind){
	printf("No:\t\t%d\n", ind->no);
	printf("Roonode addr.: \t%u:%u\n", extractLogicalArea(ind->rootNode), extractPage(ind->rootNode));
	printf("areaMap: (first eight entrys)\n");
	for(int i = 0; i < 8; i ++){
		printf("\t%d->%d\n", i, ind->areaMap[i].position);
		printf("\tType: %s\n", area_names[ind->areaMap[i].type]);
		if(ind->areaMap[i].has_areaSummary){
			unsigned int free = 0, used = 0, dirty = 0;
			for(unsigned int j = 0; j < dev->param->total_pages_per_area; j++){
				if(ind->areaMap[i].areaSummary[j] == SummaryEntry::free)
					free++;
				if(ind->areaMap[i].areaSummary[j] == SummaryEntry::used)
					used++;
				if(ind->areaMap[i].areaSummary[j] == SummaryEntry::dirty)
					dirty++;
			}
			printf("\tFree/Used/Dirty Pages: %u/%u/%u\n", free, used, dirty);
		}else{
			printf("\tSummary not present.\n");
		}
		printf("\t----------------\n");
	}
}


Result getAddrOfMostRecentSuperIndex(Dev* dev, Addr *out){

	uint32_t pos1, index1;
	Result r1 = findMostRecentEntryInBlock(dev, 0, 0, &pos1, &index1);
	if(r1 != Result::ok && r1 != Result::nf)
		return r1;

	uint32_t pos2, index2;
	Result r2 = findMostRecentEntryInBlock(dev, 0, 1, &pos2, &index2);
	if(r2 != Result::ok && r2 != Result::nf)
		return r2;

	if(r1 == Result::nf && r2 == Result::nf)
		return Result::nf;

	//Special case where block offset is zero
	*out = index1 >= index2 ? combineAddress(0, pos1) : combineAddress(0, pos2);

	return Result::ok;
}

Result commitSuperIndex(Dev* dev){
	unsigned int needed_bytes = sizeof(uint32_t) + sizeof(Addr) +
			dev->param->areas_no * (sizeof(Area) - sizeof(SummaryEntry*)) + // AreaMap without SummaryEntry pointer
			2 * dev->param->data_pages_per_area / 8 /* One bit per entry, two entrys for INDEX and DATA section*/;
	unsigned int needed_pages = needed_bytes / BYTES_PER_PAGE + 1;
	PAFFS_DBG_S(PAFFS_TRACE_SUPERBLOCK, "Minimum Pages needed for SuperIndex: %d (%d bytes)", needed_pages, needed_bytes);

	uint32_t rel_page1 = 0;
	Result r1 = findFirstFreeEntryInBlock(dev, 0, 0, &rel_page1, needed_pages);
	if(r1 != Result::ok && r1 != Result::nf){
		PAFFS_DBG(PAFFS_TRACE_ERROR, "Error while getting space on first block for new superIndex!");
		return r1;
	}

	uint32_t rel_page2 = 0;
	Result r2 = findFirstFreeEntryInBlock(dev, 0, 1, &rel_page2, needed_pages);
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

	Addr target = chosen_block == 0 ? combineAddress(0, rel_page1) : combineAddress(0, rel_page2 + dev->param->pages_per_block);

	Addr lastEntry;
 	Result r = getAddrOfMostRecentSuperIndex(dev, &lastEntry);
	if(r != Result::ok && r != Result::nf){
		PAFFS_DBG(PAFFS_TRACE_ERROR, "Could not determine Address of last SuperIndex!");
		return r;
	}

	superIndex lastIndex = {0};

	if(r != Result::nf){
		r = readSuperPageIndex(dev, lastEntry, &lastIndex, false);
		if(r != Result::ok)
			return r;
	}

	superIndex new_entry = {0};
	new_entry.no = lastIndex.no+1;
	new_entry.rootNode = rootnode_addr;
	new_entry.areaMap = dev->areaMap;

	if(trace_mask & PAFFS_TRACE_SUPERBLOCK){
		printf("write Super Index:\n");
		printSuperIndex(dev, &new_entry);
	}

	r = writeSuperIndex(dev, target, &new_entry);
	if(r != Result::ok)
		return r;

	//Handle deletion.
	if(r1 == Result::nf){
		return deleteAnchorBlock(dev, 0, 0);
	}

	if(r1 == Result::nf){
		return deleteAnchorBlock(dev, 0, 1);
	}

	rootnode_dirty = false;

	return Result::ok;
}

Result readSuperIndex(Dev* dev){
	Addr addr;
	Result r = getAddrOfMostRecentSuperIndex(dev, &addr);
	if(r != Result::ok)
		return r;

	PAFFS_DBG_S(PAFFS_TRACE_SUPERBLOCK, "Found Super Index at %u:%u\n", extractLogicalArea(addr), extractPage(addr));

	superIndex index = {0};
	index.areaMap = dev->areaMap;

	r = readSuperPageIndex(dev, addr, &index, true);
	if(r != Result::ok){
		PAFFS_DBG(PAFFS_TRACE_ERROR, "Could not read Super Index!");
		return r;
	}

	if(trace_mask & PAFFS_TRACE_SUPERBLOCK){
		printf("Read Super Index:\n");
		printSuperIndex(dev, &index);
	}

	rootnode_addr = index.rootNode;
	rootnode_dirty = false;
	return Result::ok;
}

// Superblock related

Result findFirstFreeEntryInBlock(Dev* dev, uint32_t area, uint8_t block, uint32_t* out_pos, unsigned int required_pages){
	unsigned int in_a_row = 0;
	uint64_t page_offs = dev->param->pages_per_block * block;
	for(unsigned int i = 0; i < dev->param->pages_per_block; i++) {
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

Result findMostRecentEntryInBlock(Dev* dev, uint32_t area, uint8_t block, uint32_t* out_pos, uint32_t* out_index){
	uint32_t* maximum = out_index;
	*maximum = 0;
	*out_pos = 0;
	uint32_t page_offs = dev->param->pages_per_block * block;
	for(unsigned int i = 0; i < dev->param->pages_per_block; i++) {
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


Result writeAnchorEntry(Dev* dev, Addr addr, AnchorEntry* entry){
	//Currently not implemented to simplify Find-Strategy
	return Result::nimpl;
}
Result readAnchorEntry(Dev* dev, Addr addr, AnchorEntry* entry){
	//Currently not implemented to simplify Find-Strategy
	return Result::nimpl;
}

Result deleteAnchorBlock(Dev* dev, uint32_t area, uint8_t block) {
	if(dev->areaMap[area].type != AreaType::superblockarea){
		PAFFS_DBG(PAFFS_TRACE_BUG, "Tried to delete Block outside of SUPARBLCOKAREA");
		return Result::bug;
	}
	dev->areaMap[area].erasecount++;
	uint32_t block_offs = dev->areaMap[area].position * dev->param->blocks_per_area;
	return dev->driver->eraseBlock(block_offs + block);
}

Result writeJumpPadEntry(Dev* dev, Addr addr, JumpPadEntry* entry){
	//Currently not implemented to simplify Find-Strategy
	return Result::nimpl;
}

Result readJumpPadEntry(Dev* dev, Addr addr, JumpPadEntry* entry){
	//Currently not implemented to simplify Find-Strategy
	return Result::nimpl;
}


//Make sure that free space is sufficient!
Result writeSuperIndex(Dev* dev, Addr addr, superIndex* entry){
	if(dev->areaMap[extractLogicalArea(addr)].type != AreaType::superblockarea){
		PAFFS_DBG(PAFFS_TRACE_BUG, "Tried to write superIndex outside of superblock Area");
		return Result::bug;
	}

	unsigned int needed_bytes = sizeof(uint32_t) + sizeof(Addr) +
		dev->param->areas_no * (sizeof(Area) - sizeof(SummaryEntry*))+ // AreaMap without summaryEntry pointer
		2 * dev->param->data_pages_per_area / 8 /* One bit per entry, two entrys for INDEX and DATA section*/;

	unsigned int needed_pages = needed_bytes / BYTES_PER_PAGE + 1;

	unsigned int pointer = 0;
	char buf[needed_bytes];
	memset(buf, 0, needed_bytes);
	memcpy(buf, &entry->no, sizeof(uint32_t));
	pointer += sizeof(uint32_t);
	memcpy(&buf[pointer], &entry->rootNode, sizeof(Addr));
	pointer += sizeof(Addr);
	long areaSummaryPositions[2];
	areaSummaryPositions[0] = -1;
	areaSummaryPositions[1] = -1;
	unsigned char pospos = 0;	//Stupid name

	for(unsigned int i = 0; i < dev->param->areas_no; i++){
		if((entry->areaMap[i].type == AreaType::indexarea || entry->areaMap[i].type == AreaType::dataarea) && entry->areaMap[i].status == AreaStatus::active){
			areaSummaryPositions[pospos++] = i;
			entry->areaMap[i].has_areaSummary = true;
		}else{
			entry->areaMap[i].has_areaSummary = false;
		}

		memcpy(&buf[pointer], &entry->areaMap[i], sizeof(Area) - sizeof(SummaryEntry*));
		((Area*)&buf[pointer])->areaSummary = 0;		//Pointer to old memory should be invalid
		//TODO: Optimize bitusage, currently wasting 1,25 Bytes per Entry
		pointer += sizeof(Area) - sizeof(SummaryEntry*);
	}

	for(unsigned int i = 0; i < 2; i++){
		if(areaSummaryPositions[i] < 0)
			continue;
		for(unsigned int j = 0; j < dev->param->data_pages_per_area; j++){
			if(entry->areaMap[areaSummaryPositions[i]].areaSummary[j] != SummaryEntry::dirty)
				buf[pointer + j/8] |= 1 << j%8;
		}
		pointer += dev->param->data_pages_per_area / 8;
	}

	PAFFS_DBG_S(PAFFS_TRACE_WRITE, "%u bytes have been written to Buffer", pointer);

	pointer = 0;
	uint64_t page_offs = getPageNumber(addr, dev);
	Result r;
	for(unsigned page = 0; page < needed_pages; page++){
		unsigned int btw = pointer + dev->param->data_bytes_per_page < needed_bytes ? dev->param->data_bytes_per_page
							: needed_bytes - pointer;
		r = dev->driver->writePage(page_offs + page, &buf[pointer], btw);
		if(r != Result::ok)
			return r;

		pointer += btw;
	}

	return Result::ok;
}

Result readSuperPageIndex(Dev* dev, Addr addr, superIndex* entry, bool withAreaMap){
	if(!withAreaMap)
		 return dev->driver->readPage(getPageNumber(addr, dev), entry, sizeof(uint32_t) + sizeof(Addr));

	if(entry->areaMap == NULL)
		return Result::einval;

	unsigned int needed_bytes = sizeof(uint32_t) + sizeof(Addr) +
		dev->param->areas_no * (sizeof(Area) - sizeof(SummaryEntry*))+ // AreaMap without summaryEntry pointer
		16 * dev->param->data_pages_per_area / 8 /* One bit per entry, two entries for INDEX and DATA section. Others dont have summaries*/;

	unsigned int needed_pages = needed_bytes / BYTES_PER_PAGE + 1;

	char buf[needed_bytes];
	memset(buf, 0, needed_bytes);
	uint32_t pointer = 0;
	uint64_t page_offs = getPageNumber(addr, dev);
	Result r;
	for(unsigned page = 0; page < needed_pages; page++){
		unsigned int btr = pointer + dev->param->data_bytes_per_page < needed_bytes ? dev->param->data_bytes_per_page
							: needed_bytes - pointer;
		r = dev->driver->readPage(page_offs + page, &buf[pointer], btr);
		if(r != Result::ok)
			return r;

		pointer += btr;
	}
	//buffer ready
	PAFFS_DBG_S(PAFFS_TRACE_WRITE, "SuperIndex Buffer was filled with %u Bytes.", pointer);

	pointer = 0;
	memcpy(&entry->no, buf, sizeof(uint32_t));
	pointer += sizeof(uint32_t);
	memcpy(&entry->rootNode, &buf[pointer], sizeof(Addr));
	pointer += sizeof(Addr);
	long areaSummaryPositions[2];
	areaSummaryPositions[0] = -1;
	areaSummaryPositions[1] = -1;
	unsigned char pospos = 0;	//Stupid name
	for(unsigned int i = 0; i < dev->param->areas_no; i++){
		memcpy(&entry->areaMap[i], &buf[pointer], sizeof(Area) - sizeof(SummaryEntry*));
		pointer += sizeof(Area) - sizeof(SummaryEntry*);
		entry->areaMap[i].areaSummary = 0;		//Do not take invalid pointer from old state with us
		if(entry->areaMap[i].has_areaSummary)
			areaSummaryPositions[pospos++] = i;
	}

	unsigned char pagebuf[BYTES_PER_PAGE];
	for(unsigned int i = 0; i < 2; i++){
		if(areaSummaryPositions[i] < 0)
			continue;
		if(entry->areaMap[areaSummaryPositions[i]].areaSummary == 0){
			entry->areaMap[areaSummaryPositions[i]].areaSummary = (SummaryEntry*) malloc(
					sizeof(SummaryEntry)
					* dev->param->blocks_per_area
					* dev->param->pages_per_block);
		}

		for(unsigned int j = 0; j < dev->param->data_pages_per_area; j++){
			if(buf[pointer + j/8] & 1 << j%8){
				//TODO: Normally, we would check in the OOB for a Checksum or so, which is present all the time
				Addr tmp = combineAddress(areaSummaryPositions[i], j);
				r = dev->driver->readPage(getPageNumber(tmp, dev), pagebuf, dev->param->data_bytes_per_page);
				if(r != Result::ok)
					return r;
				bool contains_data = false;
				for(unsigned int byte = 0; byte < dev->param->data_bytes_per_page; byte++){
					if(pagebuf[byte] != 0xFF){
						contains_data = true;
						break;
					}
				}
				if(contains_data)
					entry->areaMap[areaSummaryPositions[i]].areaSummary[j] = SummaryEntry::used;
				else
					entry->areaMap[areaSummaryPositions[i]].areaSummary[j] = SummaryEntry::free;
			}else{
				entry->areaMap[areaSummaryPositions[i]].areaSummary[j] = SummaryEntry::dirty;
			}
		}
		pointer += dev->param->data_pages_per_area / 8;
	}

	return Result::ok;
}

}
