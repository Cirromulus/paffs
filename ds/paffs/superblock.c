/*
 * superblock.c
 *
 *  Created on: 19.10.2016
 *      Author: rooot
 */

#include "superblock.h"

//TODO: Save Rootnode's Address in Flash (Superblockarea)
static p_addr rootnode_addr;

PAFFS_RESULT registerRootnode(p_dev* dev, p_addr addr){
	if(addr == 0)
		PAFFS_DBG(PAFFS_TRACE_BUG, "BUG: Tried to set Rootnode to 0");
	rootnode_addr = addr;
	return PAFFS_OK;
}

p_addr getRootnodeAddr(p_dev* dev){
	return rootnode_addr;
}



