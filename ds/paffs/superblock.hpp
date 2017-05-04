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


struct AnchorEntry{
	SerialNo no;
	uint8_t fs_version;
	Param param;
	uint32_t jump_pad_area;
	//Todo: Still space free
};

struct JumpPadEntry{
	SerialNo no;
	uint32_t nextArea;
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

	//returns PAFFS_NF if no superindex is in flash
	Result getAddrOfMostRecentSuperIndex(Addr *out);

	Result findFirstFreeEntryInBlock(uint32_t area, uint8_t block, uint32_t* out_pos, unsigned int required_pages);
	Result findMostRecentEntryInBlock(uint32_t area, uint8_t block, uint32_t* out_pos, uint32_t* out_index);

	Result writeAnchorEntry(Addr _addr, AnchorEntry* entry);
	Result readAnchorEntry(Addr addr, AnchorEntry* entry);
	Result deleteAnchorBlock(uint32_t area, uint8_t block);

	Result writeJumpPadEntry(Addr addr, JumpPadEntry* entry);
	Result readJumpPadEntry(Addr addr, JumpPadEntry* entry);

	Result writeSuperPageIndex(Addr addr, SuperIndex* entry);
	Result readSuperPageIndex(Addr addr, SuperIndex* entry, bool withAreaMap);

};

}
