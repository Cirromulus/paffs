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
	uint64_t no;	//This only has to hold as many numbers as there are pages in superblock area
	uint8_t fs_version;
	p_param param;
	uint32_t jump_pad_area;
	//Todo: Still space free
} anchorEntry;

typedef struct jumpPadEntry{
	uint64_t no;	//This only has to hold as many numbers as there are pages in superblock area
	uint32_t nextArea;
} jumpPadEntry;

typedef struct superIndex{
	uint64_t no;	//This only has to hold as many numbers as there are pages in superblock area
	p_addr rootNode;
	p_area* areaMap;
} superIndex;

PAFFS_RESULT registerRootnode(p_dev* dev, p_addr addr);

p_addr getRootnodeAddr(p_dev* dev);


#endif /* DS_PAFFS_SUPERBLOCK_H_ */
