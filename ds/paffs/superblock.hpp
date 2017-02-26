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

typedef uint32_t SerialNo;

typedef struct AnchorEntry{
	SerialNo no;	//This only has to hold as many numbers as there are pages in superblock area
					//Value Zero and 0xFF... is reserved.
	uint8_t fs_version;
	Param param;
	uint32_t jump_pad_area;
	//Todo: Still space free
} anchorEntry;

typedef struct JumpPadEntry{
	SerialNo no;	//This only has to hold as many numbers as there are pages in superblock area
					//Value Zero and 0xFF... is reserved.
	uint32_t nextArea;
} jumpPadEntry;

typedef struct superIndex{
	SerialNo no;	//This only has to hold as many numbers as there are pages in superblock area.
					//Values Zero and 0xFF... are reserved.
					//Zero to indicate overflow, 0xFF.. to indicate empty page
	Addr rootNode;
	Area* areaMap;	//Size can be calculated via dev->param
	AreaPos asPositions[2];
	SummaryEntry* areaSummary[2];
} superIndex;

class Superblock{
	Device* dev;
	Addr rootnode_addr = 0;
	bool rootnode_dirty = 0;

public:
	Superblock(Device *dev) : dev(dev){};
	Result registerRootnode(Addr addr);
	Addr getRootnodeAddr();

	//returns PAFFS_NF if no superindex is in flash
	Result readSuperIndex(superIndex* index);
	Result commitSuperIndex(superIndex* index);
	void printSuperIndex(superIndex* ind);

private:

	//returns PAFFS_NF if no superindex is in flash
	Result getAddrOfMostRecentSuperIndex(Addr *out);

	Result findFirstFreeEntryInBlock(uint32_t area, uint8_t block, uint32_t* out_pos, unsigned int required_pages);
	Result findMostRecentEntryInBlock(uint32_t area, uint8_t block, uint32_t* out_pos, uint32_t* out_index);

	Result writeAnchorEntry(Addr _addr, AnchorEntry* entry);
	Result readAnchorEntry(Addr addr, AnchorEntry* entry);
	Result deleteAnchorBlock(uint32_t area, uint8_t block);

	Result writeJumpPadEntry(Addr addr, JumpPadEntry* entry);
	Result readJumpPadEntry(Addr addr, JumpPadEntry* entry);

	Result writeSuperPageIndex(Addr addr, superIndex* entry);
	Result readSuperPageIndex(Addr addr, superIndex* entry, bool withAreaMap);

};

}
