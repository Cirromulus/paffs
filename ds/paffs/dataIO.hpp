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
#include "bitlist.hpp"

namespace paffs{

class DataIO{
	Device *dev;
	Addr pageListBuffer[1];	//todo: determine buffer size
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
	 * @param inode: Needed to increase reserved size and for bounds check.
	 */
	Result writePageData(PageOffs pageFrom, PageOffs pageTo, unsigned offs,
			unsigned bytes, const char* data, Addr *pageList,
			unsigned* bytes_written, Inode* inode, BitList<maxAddrs> &modified);
	Result readPageData(PageOffs pageFrom, PageOffs pageTo, unsigned offs,
			unsigned bytes, char* data, Addr *pageList,
			unsigned* bytes_read);

	Result writePageList(Inode *inode, Addr* &pageList, BitList<maxAddrs> &modified,
			unsigned int fromPage, unsigned int toPage);

	Result readPageList(Inode *inode, Addr* &pageList, unsigned int fromPage,
			unsigned int toPage);
};

};

#endif /* PAFFS_FLASH_H_ */
