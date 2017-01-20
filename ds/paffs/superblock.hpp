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

typedef struct AnchorEntry{
	uint32_t no;	//This only has to hold as many numbers as there are pages in superblock area
					//Value Zero and 0xFF... is reserved.
	uint8_t fs_version;
	Param param;
	uint32_t jump_pad_area;
	//Todo: Still space free
} anchorEntry;

typedef struct JumpPadEntry{
	uint32_t no;	//This only has to hold as many numbers as there are pages in superblock area
					//Value Zero and 0xFF... is reserved.
	uint32_t nextArea;
} jumpPadEntry;

typedef struct superIndex{
	uint32_t no;	//This only has to hold as many numbers as there are pages in superblock area.
					//Values Zero and 0xFF... are reserved.
	Addr rootNode;
	Area* areaMap;	//Size can be calculated via dev->param
} superIndex;

Result registerRootnode(Dev* dev, Addr addr);
Addr getRootnodeAddr(Dev* dev);

//returns PAFFS_NF if no superindex is in flash
Result readSuperIndex(Dev* dev, SummaryEntry **summary_Containers);
Result commitSuperIndex(Dev* dev);


//returns PAFFS_NF if no superindex is in flash
Result getAddrOfMostRecentSuperIndex(Dev* dev, Addr *out);

void printSuperIndex(Dev* dev, superIndex* ind);

}

#endif /* DS_PAFFS_SUPERBLOCK_H_ */
