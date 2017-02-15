/*
 * superblock.h
 *
 *  Created on: 17.10.2016
 *      Author: urinator
 */

#ifndef DS_PAFFS_SUPERBLOCK_H_
#define DS_PAFFS_SUPERBLOCK_H_

#include <stdint.h>

#include "paffs.hpp"

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
	Addr rootNode;
	Area* areaMap;	//Size can be calculated via dev->param
	AreaPos asPositions[2];
	SummaryEntry* areaSummary[2];
} superIndex;

Result registerRootnode(Device* dev, Addr addr);
Addr getRootnodeAddr(Device* dev);

//returns PAFFS_NF if no superindex is in flash
Result readSuperIndex(Device* dev, superIndex* index);
Result commitSuperIndex(Device* dev, superIndex* index);


//returns PAFFS_NF if no superindex is in flash
Result getAddrOfMostRecentSuperIndex(Device* dev, Addr *out);

void printSuperIndex(Device* dev, superIndex* ind);


Result findFirstFreeEntryInBlock(Device* dev, uint32_t area, uint8_t block, uint32_t* out_pos, unsigned int required_pages);
Result findMostRecentEntryInBlock(Device* dev, uint32_t area, uint8_t block, uint32_t* out_pos, uint32_t* out_index);

Result writeAnchorEntry(Device* dev, Addr _addr, AnchorEntry* entry);
Result readAnchorEntry(Device* dev, Addr addr, AnchorEntry* entry);
Result deleteAnchorBlock(Device* dev, uint32_t area, uint8_t block);

Result writeJumpPadEntry(Device* dev, Addr addr, JumpPadEntry* entry);
Result readJumpPadEntry(Device* dev, Addr addr, JumpPadEntry* entry);

Result writeSuperPageIndex(Device* dev, Addr addr, superIndex* entry);
Result readSuperPageIndex(Device* dev, Addr addr, superIndex* entry, bool withAreaMap);


}

#endif /* DS_PAFFS_SUPERBLOCK_H_ */
