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
#include "pageAddressCache.hpp"

namespace paffs{

class DataIO{
	Device *dev;
	PageAddressCache pac;
public:
	DataIO(Device *mdev) : dev(mdev), pac(mdev){};
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
	 * @param reservedPages Is increased if new page was used
	 */
	Result writePageData(PageOffs pageFrom, PageOffs pageTo, unsigned offs,
			unsigned bytes, const char* data, PageAddressCache &ac,
			unsigned* bytes_written, FileSize filesize, uint32_t &reservedPages);
	Result readPageData(PageOffs pageFrom, PageOffs pageTo, unsigned offs,
			unsigned bytes, char* data, PageAddressCache &ac,
			unsigned* bytes_read);

};

};

#endif /* PAFFS_FLASH_H_ */
