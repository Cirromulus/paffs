/*
 * superblock.c
 *
 *  Created on: 19.10.2016
 *      Author: rooot
 */

#include "superblock.hpp"
#include "paffs_flash.hpp"

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
		r = readSuperPageIndex(dev, lastEntry, &lastIndex, NULL, false);
		if(r != Result::ok)
			return r;
	}

	superIndex new_entry = {0};
	new_entry.no = lastIndex.no+1;
	new_entry.rootNode = rootnode_addr;	//Just 4 Byte are written?
	new_entry.areaMap = dev->areaMap;

	if(trace_mask & PAFFS_TRACE_SUPERBLOCK){
		printf("write Super Index:\n");
		printSuperIndex(dev, &new_entry);
	}

	r = writeSuperIndex(dev, target, &new_entry);
	if(r != Result::ok)
		return r;

	//Handle deletion. TODO: Increase deletion Count before writing new entry
	if(r1 == Result::nf){
		return deleteAnchorBlock(dev, 0, 0);
	}

	if(r1 == Result::nf){
		return deleteAnchorBlock(dev, 0, 1);
	}

	rootnode_dirty = false;

	return Result::ok;
}

Result readSuperIndex(Dev* dev, SummaryEntry **summary_Containers){
	Addr addr;
	Result r = getAddrOfMostRecentSuperIndex(dev, &addr);
	if(r != Result::ok)
		return r;

	PAFFS_DBG_S(PAFFS_TRACE_SUPERBLOCK, "Found Super Index at %u:%u\n", extractLogicalArea(addr), extractPage(addr));

	superIndex index = {0};
	index.areaMap = dev->areaMap;

	r = readSuperPageIndex(dev, addr, &index, summary_Containers,  true);
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

}
