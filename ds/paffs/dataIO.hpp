/*
 * paffs_flash.h
 *
 *  Created on: 30.06.2016
 *      Author: Pascal Pieper
 */

#ifndef PAFFS_FLASH_H_
#define PAFFS_FLASH_H_

#include "btree.hpp"
#include "superblock.hpp"
#include "commonTypes.hpp"

namespace paffs{

class DataIO{
	Device *dev;
public:
	DataIO(Device *mdev) : dev(mdev){};
//Updates changes to treeCache as well
Result writeInodeData(Inode* inode,
					unsigned int offs, unsigned int bytes,unsigned int *bytes_written,
					const char* data);
Result readInodeData(Inode* inode,
					unsigned int offs, unsigned int bytes, unsigned int *bytes_read,
					char* data);
Result deleteInodeData(Inode* inode, unsigned int offs);


// TreeNode related
Result writeTreeNode(TreeNode* node);
Result readTreeNode(Addr addr, TreeNode* node);
Result deleteTreeNode(TreeNode* node);

private:
/**
 * @param buf input buffer read from page
 * @param addresses Array big enough to hold 'count' Addresses
 * @param count number of addresses contained in buffer
 */
Result extractIndirection(char* buf, Addr* addresses, unsigned count);

/**
 * @param buf output buffer read from page big enough to hold 'count' addresses
 * @param addresses Array of 'count' Addresses
 * @param count number of addresses contained in Array
 */
Result composeIndirection(char* buf, Addr* addresses, unsigned count);
};
}

#endif /* PAFFS_FLASH_H_ */
