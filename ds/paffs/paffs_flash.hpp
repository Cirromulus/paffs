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

extern const char* area_names[];

//Returns same area if there is still writable Space left
unsigned int findWritableArea(AreaType areaType, Dev* dev);

Result findFirstFreePage(unsigned int* p_out, Dev* dev, unsigned int area);

uint64_t getPageNumber(Addr addr, Dev *dev);	//Translates Addr to physical page number in respect to the Area mapping

Addr combineAddress(uint32_t logical_area, uint32_t page);
unsigned int extractLogicalArea(Addr addr);			//Address-wrapper für einfacheren Garbagecollector
unsigned int extractPage(Addr addr);

Result manageActiveAreaFull(Dev *dev, AreaPos *area, AreaType areaType);

Result writeAreasummary(Dev *dev, AreaPos area, SummaryEntry* summary);

Result readAreasummary(Dev *dev, AreaPos area, SummaryEntry* out_summary, bool complete);

void initArea(Dev* dev, AreaPos area);
Result loadArea(Dev *dev, AreaPos area);
Result closeArea(Dev *dev, AreaPos area);
void retireArea(Dev *dev, AreaPos area);



//Updates changes to treeCache as well
Result writeInodeData(Inode* inode,
					unsigned int offs, unsigned int bytes,unsigned int *bytes_written,
					const char* data, Dev* dev);
Result readInodeData(Inode* inode,
					unsigned int offs, unsigned int bytes, unsigned int *bytes_read,
					char* data, Dev* dev);
Result deleteInodeData(Inode* inode, Dev* dev, unsigned int offs);




// TreeNode related
Result writeTreeNode(Dev* dev, treeNode* node);
Result readTreeNode(Dev* dev, Addr addr, treeNode* node);
Result deleteTreeNode(Dev* dev, treeNode* node);


// Superblock related
Result findFirstFreeEntryInBlock(Dev* dev, uint32_t area, uint8_t block, uint32_t* out_pos, unsigned int required_pages);
Result findMostRecentEntryInBlock(Dev* dev, uint32_t area, uint8_t block, uint32_t* out_pos, uint32_t* out_index);

Result writeAnchorEntry(Dev* dev, Addr _addr, AnchorEntry* entry);
Result readAnchorEntry(Dev* dev, Addr addr, AnchorEntry* entry);
Result deleteAnchorBlock(Dev* dev, uint32_t area, uint8_t block);

Result writeJumpPadEntry(Dev* dev, Addr addr, JumpPadEntry* entry);
Result readJumpPadEntry(Dev* dev, Addr addr, JumpPadEntry* entry);

Result writeSuperIndex(Dev* dev, Addr addr, superIndex* entry);
Result readSuperPageIndex(Dev* dev, Addr addr, superIndex* entry,  SummaryEntry* summary_Containers[2], bool withAreaMap);

}

#endif /* PAFFS_FLASH_H_ */
