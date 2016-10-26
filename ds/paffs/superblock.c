/*
 * superblock.c
 *
 *  Created on: 19.10.2016
 *      Author: rooot
 */

#include "superblock.h"
#include "paffs_flash.h"


static p_addr rootnode_addr = 0;

PAFFS_RESULT registerRootnode(p_dev* dev, p_addr addr){
	if(addr == 0)
		PAFFS_DBG(PAFFS_TRACE_BUG, "BUG: Tried to set Rootnode to 0");
	rootnode_addr = addr;

	return PAFFS_OK;
}

p_addr getRootnodeAddr(p_dev* dev){
	if(rootnode_addr == 0){
		PAFFS_DBG(PAFFS_TRACE_ERROR, "rootnode_address is 0! Maybe device not mounted?");
	}

	return rootnode_addr;
}


PAFFS_RESULT getAddrOfMostRecentSuperIndex(p_dev* dev, p_addr *out){

	uint32_t pos1, index1;
	PAFFS_RESULT r1 = findMostRecentEntryInBlock(dev, 0, 0, &pos1, &index1);
	if(r1 != PAFFS_OK && r1 != PAFFS_NF)
		return r1;

	uint32_t pos2, index2;
	PAFFS_RESULT r2 = findMostRecentEntryInBlock(dev, 0, 1, &pos2, &index2);
	if(r2 != PAFFS_OK && r2 != PAFFS_NF)
		return r2;

	if(r1 == PAFFS_NF && r2 == PAFFS_NF)
		return PAFFS_NF;

	//Special case where block offset is zero
	*out = index1 >= index2 ? combineAddress(0, pos1) : combineAddress(0, pos2);

	return PAFFS_OK;
}

PAFFS_RESULT commitSuperIndex(p_dev* dev){
	unsigned int needed_bytes = sizeof(uint32_t) + sizeof(p_addr) +
			dev->param.areas_no * (sizeof(p_area) - sizeof(p_summaryEntry*)) + // AreaMap without summaryEntry pointer
			2 * dev->param.pages_per_area / 8 /* One bit per entry, two entrys for INDEX and DATA section*/;
	unsigned int needed_pages = needed_bytes / BYTES_PER_PAGE + 1;
	PAFFS_DBG_S(PAFFS_TRACE_SUPERBLOCK, "Pages needed for SuperIndex: %d (%d bytes)", needed_pages, needed_bytes);


	uint32_t rel_page1 = 0;
	PAFFS_RESULT r1 = findFirstFreeEntryInBlock(dev, 0, 0, &rel_page1, needed_pages);
	if(r1 != PAFFS_OK && r1 != PAFFS_NF){
		PAFFS_DBG(PAFFS_TRACE_ERROR, "Error while getting space on first block for new superIndex!");
		return r1;
	}

	uint32_t rel_page2 = 0;
	PAFFS_RESULT r2 = findFirstFreeEntryInBlock(dev, 0, 1, &rel_page2, needed_pages);
	if(r2 != PAFFS_OK && r2 != PAFFS_NF){
		PAFFS_DBG(PAFFS_TRACE_ERROR, "Error while getting space on second block for new superIndex!");
		return r2;
	}

	uint8_t chosen_block = 0;

	if(r1 == PAFFS_OK || r2 == PAFFS_OK){
		//some Blocks contain free space
		if(r1 == PAFFS_OK && r2 == PAFFS_OK){
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
				return PAFFS_BUG;
			}
		}else if(r1 == PAFFS_OK){
			//First is current, second is full
			chosen_block = 0;
		}else{
			//First is full, second is current
			chosen_block = 1;
		}

	}else{
		PAFFS_DBG(PAFFS_TRACE_BUG, "Both Superblocks are full?");
		return PAFFS_BUG;
	}

	p_addr target = chosen_block == 0 ? combineAddress(0, rel_page1) : combineAddress(0, rel_page2);

	p_addr lastEntry;
	PAFFS_RESULT r = getAddrOfMostRecentSuperIndex(dev, &lastEntry);
	if(r != PAFFS_OK && r != PAFFS_NF){
		PAFFS_DBG(PAFFS_TRACE_ERROR, "Could not determine Address of last SuperIndex!");
		return r;
	}

	superIndex lastIndex = {0};

	if(r != PAFFS_NF){
		r = readSuperPageIndex(dev, lastEntry, &lastIndex, false);
		if(r != PAFFS_OK)
			return r;
	}

	superIndex new_entry;
	new_entry.no = lastIndex.no+1;
	new_entry.rootNode = rootnode_addr;
	new_entry.areaMap = dev->areaMap;

	r = writeSuperIndex(dev, target, &new_entry);
	if(r != PAFFS_OK)
		return r;

	//Handle deletion
	if(r1 == PAFFS_NF){
		return deleteAnchorBlock(dev, 0, 0);
	}

	if(r1 == PAFFS_NF){
		return deleteAnchorBlock(dev, 0, 1);
	}

	return PAFFS_OK;
}

PAFFS_RESULT readSuperIndex(p_dev* dev, superIndex *out_index){
	p_addr addr;
	PAFFS_RESULT r = getAddrOfMostRecentSuperIndex(dev, &addr);
	if(r != PAFFS_OK)
		return r;

	r = readSuperPageIndex(dev, addr, out_index, true);	//todo: Switch to "true"
	if(r != PAFFS_OK){
		PAFFS_DBG(PAFFS_TRACE_ERROR, "Could not read Super Index!");
		return r;
	}
	return PAFFS_OK;
}

