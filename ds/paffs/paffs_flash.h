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

uint64_t getPageNumber(p_addr addr, p_dev *dev);	//Translates p_addr to physical page number in respect to the Area mapping

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
PAFFS_RESULT findFirstFreeEntryInBlock(p_dev* dev, uint32_t area, uint8_t block, uint32_t* out_pos, unsigned int required_pages);
PAFFS_RESULT findMostRecentEntryInBlock(p_dev* dev, uint32_t area, uint8_t block, uint32_t* out_pos, uint32_t* out_index);

PAFFS_RESULT writeAnchorEntry(p_dev* dev, p_addr _addr, anchorEntry* entry);
PAFFS_RESULT readAnchorEntry(p_dev* dev, p_addr addr, anchorEntry* entry);
PAFFS_RESULT deleteAnchorBlock(p_dev* dev, uint32_t area, uint8_t block);

PAFFS_RESULT writeJumpPadEntry(p_dev* dev, p_addr addr, jumpPadEntry* entry);
PAFFS_RESULT readJumpPadEntry(p_dev* dev, p_addr addr, jumpPadEntry* entry);

PAFFS_RESULT writeSuperIndex(p_dev* dev, p_addr addr, superIndex* entry);
PAFFS_RESULT readSuperPageIndex(p_dev* dev, p_addr addr, superIndex* entry, bool withAreaMap);


#endif /* PAFFS_FLASH_H_ */
