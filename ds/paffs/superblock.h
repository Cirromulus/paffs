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
	uint64_t no;			//This only has to hold as many numbers as there are pages in superblock area
	uint8_t fs_version;
	p_param param;
	p_addr jump_pad;
	//Todo: Still space free
} anchorEntry;

typedef struct jumpPadEntry{
	p_addr next;
} jumpPadEntry;

typedef struct superPage{
	p_addr rootNode;
	areamap
};

#endif /* DS_PAFFS_SUPERBLOCK_H_ */
