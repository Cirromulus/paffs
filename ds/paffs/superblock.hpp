/*
 * superblock.h
 *
 *  Created on: 17.10.2016
 *      Author: urinator
 */

#pragma once

#include <stdint.h>
#include "commonTypes.hpp"

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
	uint8_t fsVersion;
	Param param;
	AreaPos jumpPadArea;
	//Todo: Still space free
};

struct JumpPadEntry{
	SerialNo no;
	AreaPos nextArea;
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
	Result commitSuperIndex(SuperIndex* index);
	void printSuperIndex(SuperIndex* ind);

private:

	Result resolveDirectToLogicalPath(AreaPos directPath[jumpPadNo+2],
			AreaPos outPath[jumpPadNo+2]);

	Result findFirstFreeEntryInArea(AreaPos area, PageOffs* out_pos,
			unsigned int required_pages);

	Result findFirstFreeEntryInBlock(AreaPos area, uint8_t block,
			PageOffs* out_pos, unsigned int required_pages);

	Result getAddrOfMostRecentSuperIndex(Addr path[jumpPadNo+2],
			SerialNo indexes[jumpPadNo+2]);
	Result findMostRecentEntryInArea(AreaPos area, Addr* out_pos,
			SerialNo* out_index);
	Result findMostRecentEntryInBlock(AreaPos area, uint8_t block,
			PageOffs* out_pos, SerialNo* out_index);

	/**
	 * This assumes that the area of the Anchor entry does not change.
	 * Changes the serial to zero if a new block is used.
	 * @param logarea may be changed if target was written to a new area
	 * @param area may be changed if target was written to a new area
	 */
	Result insertNewAnchorEntry(AreaPos *logarea, AreaPos *area, AnchorEntry* entry);
	Result writeAnchorEntry(AreaPos logarea, Addr addr, AnchorEntry* entry);
	Result readAnchorEntry(Addr addr, AnchorEntry* entry);

	/**
	 * May call garbage collection for a new SB area.
	 * Changes the serial to zero if a new block is used.
 	 * @param logarea may be changed if target was written to a new area
	 * @param area may be changed if target was written to a new area
	 */
	Result insertNewJumpPadEntry(AreaPos *logarea, AreaPos *area, JumpPadEntry* entry);
	Result writeJumpPadEntry(AreaPos logarea, Addr addr, JumpPadEntry* entry);
	Result readJumpPadEntry(Addr addr, JumpPadEntry* entry);

	/**
	 * May call garbage collection for a new SB area.
	 * Changes the serial to zero if a new block is used.
	 * @param logarea may be changed if target was written to a new area
	 * @param area may be changed if target was written to a new area
	 */
	Result insertNewSuperPage(AreaPos *logarea, AreaPos *area, SuperIndex* entry);
	Result writeSuperPageIndex(AreaPos logarea, Addr addr, SuperIndex* entry);
	Result readSuperPageIndex(Addr addr, SuperIndex* entry, bool withAreaMap);

	Result deleteSuperBlock(AreaPos area, uint8_t block);
};

}
