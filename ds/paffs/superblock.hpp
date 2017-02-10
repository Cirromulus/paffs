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

Result registerRootnode(Dev* dev, Addr addr);
Addr getRootnodeAddr(Dev* dev);

//returns PAFFS_NF if no superindex is in flash
Result readSuperIndex(Dev* dev, superIndex* index);
Result commitSuperIndex(Dev* dev, superIndex* index);


//returns PAFFS_NF if no superindex is in flash
Result getAddrOfMostRecentSuperIndex(Dev* dev, Addr *out);

void printSuperIndex(Dev* dev, superIndex* ind);


Result findFirstFreeEntryInBlock(Dev* dev, uint32_t area, uint8_t block, uint32_t* out_pos, unsigned int required_pages);
Result findMostRecentEntryInBlock(Dev* dev, uint32_t area, uint8_t block, uint32_t* out_pos, uint32_t* out_index);

Result writeAnchorEntry(Dev* dev, Addr _addr, AnchorEntry* entry);
Result readAnchorEntry(Dev* dev, Addr addr, AnchorEntry* entry);
Result deleteAnchorBlock(Dev* dev, uint32_t area, uint8_t block);

Result writeJumpPadEntry(Dev* dev, Addr addr, JumpPadEntry* entry);
Result readJumpPadEntry(Dev* dev, Addr addr, JumpPadEntry* entry);

Result writeSuperPageIndex(Dev* dev, Addr addr, superIndex* entry);
Result readSuperPageIndex(Dev* dev, Addr addr, superIndex* entry, bool withAreaMap);


}

#endif /* DS_PAFFS_SUPERBLOCK_H_ */
