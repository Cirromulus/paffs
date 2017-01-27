/*
 * paffs_flash.h
 *
 *  Created on: 30.06.2016
 *      Author: rooot
 */

#ifndef PAFFS_FLASH_H_
#define PAFFS_FLASH_H_

#include "btree.hpp"
#include "paffs.hpp"
#include "superblock.hpp"

namespace paffs{


//Updates changes to treeCache as well
Result writeInodeData(Inode* inode,
					unsigned int offs, unsigned int bytes,unsigned int *bytes_written,
					const char* data, Dev* dev);
Result readInodeData(Inode* inode,
					unsigned int offs, unsigned int bytes, unsigned int *bytes_read,
					char* data, Dev* dev);
Result deleteInodeData(Inode* inode, Dev* dev, unsigned int offs);


// TreeNode related
Result writeTreeNode(Dev* dev, TreeNode* node);
Result readTreeNode(Dev* dev, Addr addr, TreeNode* node);
Result deleteTreeNode(Dev* dev, TreeNode* node);
}

#endif /* PAFFS_FLASH_H_ */
