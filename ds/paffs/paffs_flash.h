/*
 * paffs_flash.h
 *
 *  Created on: 30.06.2016
 *      Author: rooot
 */

#ifndef PAFFS_FLASH_H_
#define PAFFS_FLASH_H_

#include "paffs.h"

//Returns same area if there is still writable Space left
unsigned int findWritableArea(p_areaType areaType, p_dev* dev);

PAFFS_RESULT findFirstFreePage(unsigned int* p_out, p_dev* dev, unsigned int area);

PAFFS_RESULT writeInodeData(pInode* inode,
					unsigned int offs, unsigned int bytes,unsigned int *bytes_written,
					const char* data, p_dev* dev);
PAFFS_RESULT readInodeData(pInode* inode,
					unsigned int offs, unsigned int bytes, unsigned int *bytes_read,
					char* data, p_dev* dev);
PAFFS_RESULT deleteInodeData(pInode* inode, p_dev* dev);

void initArea(p_dev* dev, unsigned long int area);


// TreeNode related
void registerRootnode(p_addr addr);

p_addr getRootnode();

/**
 * @param addr: Location where node has been written to
 */
PAFFS_RESULT writeTreeNode(p_dev* dev, p_addr *addr, treeNode* node);

PAFFS_RESULT readTreeNode(p_dev* dev, p_addr addr, treeNode* node);

#endif /* PAFFS_FLASH_H_ */
