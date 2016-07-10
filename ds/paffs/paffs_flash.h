/*
 * paffs_flash.h
 *
 *  Created on: 30.06.2016
 *      Author: rooot
 */

#ifndef PAFFS_FLASH_H_
#define PAFFS_FLASH_H_

#include "paffs.h"

unsigned int findWritableArea(p_dev* dev);

PAFFS_RESULT findFirstFreePage(unsigned int* p_out, p_dev* dev, unsigned int area);

PAFFS_RESULT writeInodeData(pInode* inode,
					unsigned int offs, unsigned int bytes,
					void* data, p_dev* dev);
PAFFS_RESULT readInodeData(pInode* inode,
					unsigned int offs, unsigned int bytes,
					void* data, p_dev* dev);
PAFFS_RESULT deleteInodeData(pInode* inode, p_dev* dev);

void initArea(p_dev* dev, unsigned long int area);

#endif /* PAFFS_FLASH_H_ */
