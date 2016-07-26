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

//Returns same area if there is still writable Space left
unsigned int findWritableArea(p_areaType areaType, p_dev* dev);

PAFFS_RESULT findFirstFreePage(unsigned int* p_out, p_dev* dev, unsigned int area);

PAFFS_RESULT checkActiveAreaFull(p_dev *dev, unsigned int *area, p_areaType areaType);

PAFFS_RESULT getNextUsedAddr(p_dev *dev, p_areaType areaType, p_addr* addr);

void initArea(p_dev* dev, unsigned long int area);

PAFFS_RESULT writeInodeData(pInode* inode,
					unsigned int offs, unsigned int bytes,unsigned int *bytes_written,
					const char* data, p_dev* dev);
PAFFS_RESULT readInodeData(pInode* inode,
					unsigned int offs, unsigned int bytes, unsigned int *bytes_read,
					char* data, p_dev* dev);
PAFFS_RESULT deleteInodeData(pInode* inode, p_dev* dev);




// TreeNode related
void registerRootnode(p_dev* dev, p_addr addr);

p_addr getRootnodeAddr(p_dev* dev);

PAFFS_RESULT writeTreeNode(p_dev* dev, treeNode* node);

PAFFS_RESULT readTreeNode(p_dev* dev, p_addr addr, treeNode* node);

#endif /* PAFFS_FLASH_H_ */
