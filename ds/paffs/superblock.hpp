/*
 * superblock.h
 *
 *  Created on: 17.10.2016
 *      Author: urinator
 */

#pragma once

#include <stdint.h>
#include "commonTypes.hpp"
#include <config.hpp>

namespace paffs{

//This only has to hold as many numbers as there are pages in superblock area
//Values Zero and 0xFF... are reserved.
//Zero to indicate overflow, 0xFF.. to indicate empty page
typedef uint32_t SerialNo;
const SerialNo emptySerial = 0xFFFFFFFF;


/**
 * @file nearly all AreaPos values point to _physical_ areanumbers, not logical!
 */

struct AnchorEntry{
	SerialNo no;
	AreaPos jumpPadArea;	//this needs to be on second place after Serial no
	Param param;
	uint8_t fsVersion;
	//Todo: Still space free
};

struct JumpPadEntry{
	SerialNo no;
	AreaPos nextArea;		//this needs to be on second place after Serial no
};

struct SuperIndex{
	SerialNo no;
	Addr rootNode;
	Area* areaMap;
	AreaPos asPositions[2];
	SummaryEntry* areaSummary[2];
};

class Superblock{
	Device* dev;
	Addr rootnode_addr = 0;
	bool rootnode_dirty = 0;

public:
	Superblock(Device *mdev) : dev(mdev){};
	Result registerRootnode(Addr addr);
	Addr getRootnodeAddr();

	//returns PAFFS_NF if no superindex is in flash
	Result readSuperIndex(SuperIndex* index);
	Result commitSuperIndex(SuperIndex* index, bool createNew = false);
	void printSuperIndex(SuperIndex* ind);

private:
	/**
	 * Worst case O(n) with area count
	 */
	Result resolveDirectToLogicalPath(Addr directPath[superChainElems],
			Addr outPath[superChainElems]);
	/**
	 * Should be constant because at formatting time all superblocks are located at start
	 */
	Result fillPathWithFirstSuperblockAreas(Addr directPath[superChainElems]);

	/**
	 * This assumes that blocks are immediately deleted after starting
	 * a new block inside area
	 * -> returns NF if last block is full, even if other blocks are free
	 */
	Result findFirstFreeEntryInArea(AreaPos area, PageOffs* out_pos,
			unsigned int required_pages);

	Result findFirstFreeEntryInBlock(AreaPos area, uint8_t block,
			PageOffs* out_pos, unsigned int required_pages);

	/**
	 * First elem is anchor
	 */
	Result getAddrOfMostRecentSuperIndex(Addr path[superChainElems],
			SerialNo indexes[superChainElems]);
	Result readMostRecentEntryInArea(AreaPos area, Addr* out_pos,
			SerialNo* out_index, AreaPos* next);
	Result readMostRecentEntryInBlock(AreaPos area, uint8_t block,
			PageOffs* out_pos, SerialNo* out_index, AreaPos* next);

	/**
	 * This assumes that the area of the Anchor entry does not change.
	 * Entry->serialNo needs to be set to appropriate increased number.
	 * Changes the serial to zero if a new block is used.
	 * @param prev is a *logical* addr to the last valid entry
	 * @param area may be changed if target was written to a new area
	 */
	Result insertNewAnchorEntry(Addr prev, AreaPos *area, AnchorEntry* entry);
	Result readAnchorEntry(Addr addr, AnchorEntry* entry);

	/**
	 * May call garbage collection for a new SB area.
	 * Changes the serial to zero if a new block is used.
 	 * @param prev is a *logical* addr to the last valid entry
	 * @param area may be changed if target was written to a new area
	 */
	Result insertNewJumpPadEntry(Addr prev, AreaPos *area, JumpPadEntry* entry);
	Result readJumpPadEntry(Addr addr, JumpPadEntry* entry);

	/**
	 * May call garbage collection for a new SB area.
	 * Changes the serial to zero if a new block is used.
	 * @param prev is a *logical* addr to the last valid entry
	 * @param area may be changed if target was written to a new area
	 */
	Result insertNewSuperIndex(Addr prev, AreaPos *area, SuperIndex* entry);
	Result writeSuperPageIndex(PageAbs pageStart, SuperIndex* entry);
	Result readSuperPageIndex(Addr addr, SuperIndex* entry, bool withAreaMap);

	Result deleteSuperBlock(AreaPos area, uint8_t block);
};

}
