/*
 * paffs_flash.h
 *
 *  Created on: 30.06.2016
 *      Author: rooot
 */

#ifndef PAFFS_FLASH_H_
#define PAFFS_FLASH_H_

#include "paffs.h"
#include "btree.h"
#include "superblock.h"

//Returns same area if there is still writable Space left
unsigned int findWritableArea(p_areaType areaType, p_dev* dev);

PAFFS_RESULT findFirstFreePage(unsigned int* p_out, p_dev* dev, unsigned int area);

PAFFS_RESULT checkActiveAreaFull(p_dev *dev, unsigned int *area, p_areaType areaType);

void initArea(p_dev* dev, unsigned long int area);

//Updates changes to treeCache as well
PAFFS_RESULT writeInodeData(pInode* inode,
					unsigned int offs, unsigned int bytes,unsigned int *bytes_written,
					const char* data, p_dev* dev);
PAFFS_RESULT readInodeData(pInode* inode,
					unsigned int offs, unsigned int bytes, unsigned int *bytes_read,
					char* data, p_dev* dev);
PAFFS_RESULT deleteInodeData(pInode* inode, p_dev* dev, unsigned int offs);




// TreeNode related
PAFFS_RESULT writeTreeNode(p_dev* dev, treeNode* node);
PAFFS_RESULT readTreeNode(p_dev* dev, p_addr addr, treeNode* node);
PAFFS_RESULT deleteTreeNode(p_dev* dev, treeNode* node);


// Superblock related
PAFFS_RESULT writeAnchorEntry(p_dev* dev, p_addr* out_addr, anchorEntry* entry);
PAFFS_RESULT readAnchorEntry(p_dev* dev, p_addr addr, anchorEntry* entry);

PAFFS_RESULT writeJumpPadEntry(p_dev* dev, p_addr* out_addr, jumpPadEntry* entry);
PAFFS_RESULT readJumpPadEntry(p_dev* dev, p_addr addr, jumpPadEntry* entry);

PAFFS_RESULT writeSuperIndex(p_dev* dev, p_addr* out_addr, superIndex* entry);
PAFFS_RESULT readSuperPageIndex(p_dev* dev, p_addr addr, superIndex* entry);



#endif /* PAFFS_FLASH_H_ */
