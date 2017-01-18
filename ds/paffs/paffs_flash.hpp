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

//Returns same area if there is still writable Space left
unsigned int findWritableArea(areaType areaType, p_dev* dev);

PAFFS_RESULT findFirstFreePage(unsigned int* p_out, p_dev* dev, unsigned int area);

uint64_t getPageNumber(p_addr addr, p_dev *dev);	//Translates p_addr to physical page number in respect to the Area mapping

p_addr combineAddress(uint32_t logical_area, uint32_t page);
unsigned int extractLogicalArea(p_addr addr);			//Address-wrapper für einfacheren Garbagecollector
unsigned int extractPage(p_addr addr);

PAFFS_RESULT manageActiveAreaFull(p_dev *dev, area_pos_t *area, areaType areaType);

PAFFS_RESULT writeAreasummary(p_dev *dev, area_pos_t area, p_summaryEntry* summary);

PAFFS_RESULT readAreasummary(p_dev *dev, area_pos_t area, p_summaryEntry* out_summary, bool complete);

void initArea(p_dev* dev, area_pos_t area);
PAFFS_RESULT loadArea(p_dev *dev, area_pos_t area);
PAFFS_RESULT closeArea(p_dev *dev, area_pos_t area);
void retireArea(p_dev *dev, area_pos_t area);



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
PAFFS_RESULT readSuperPageIndex(p_dev* dev, p_addr addr, superIndex* entry,  p_summaryEntry* summary_Containers[2], bool withAreaMap);


#endif /* PAFFS_FLASH_H_ */
