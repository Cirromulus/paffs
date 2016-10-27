/*
 * superblock.h
 *
 *  Created on: 17.10.2016
 *      Author: urinator
 */

#ifndef DS_PAFFS_SUPERBLOCK_H_
#define DS_PAFFS_SUPERBLOCK_H_

#include <stdint.h>
#include "paffs.h"

typedef struct anchorEntry{
	uint32_t no;	//This only has to hold as many numbers as there are pages in superblock area
					//Value Zero and 0xFF... is reserved.
	uint8_t fs_version;
	p_param param;
	uint32_t jump_pad_area;
	//Todo: Still space free
} anchorEntry;

typedef struct jumpPadEntry{
	uint32_t no;	//This only has to hold as many numbers as there are pages in superblock area
					//Value Zero and 0xFF... is reserved.
	uint32_t nextArea;
} jumpPadEntry;

typedef struct superIndex{
	uint32_t no;	//This only has to hold as many numbers as there are pages in superblock area.
					//Values Zero and 0xFF... are reserved.
	p_addr rootNode;
	p_area* areaMap;	//Size can be calculated via dev->param
} superIndex;

PAFFS_RESULT registerRootnode(p_dev* dev, p_addr addr);
p_addr getRootnodeAddr(p_dev* dev);

//returns PAFFS_NF if no superindex is in flash
PAFFS_RESULT readSuperIndex(p_dev* dev, superIndex *out_Index, p_summaryEntry **summary_Containers);
PAFFS_RESULT commitSuperIndex(p_dev* dev);


//returns PAFFS_NF if no superindex is in flash
PAFFS_RESULT getAddrOfMostRecentSuperIndex(p_dev* dev, p_addr *out);

#endif /* DS_PAFFS_SUPERBLOCK_H_ */
