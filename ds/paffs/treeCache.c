/*
 * treeCache.c
 *
 *  Created on: 23.09.2016
 *      Author: urinator
 */

#include "treeCache.h"
#include "paffs_flash.h"
#include <string.h>
#include "btree.h"

static treeCacheNode* cache_root = NULL;

PAFFS_RESULT getRootNodeFromCache(p_dev* dev, treeCacheNode** tcn){
	if(cache_root != NULL){
		*tcn = cache_root;
		return PAFFS_OK;
	}

	p_addr addr = getRootnodeAddr(dev);
	if(addr == 0)
		PAFFS_DBG(PAFFS_TRACE_TREE, "get Rootnode, but does not exist!");

	PAFFS_RESULT r = addNewCacheNode(dev, &cache_root);
	if(r != PAFFS_OK)
		return r;
	*tcn = cache_root;
	cache_root->parent = cache_root;
	return readTreeNode(dev, addr, &cache_root->raw);
}

PAFFS_RESULT getTreeNodeAtIndexFrom(p_dev* dev, unsigned char index,
									treeCacheNode* parent, treeCacheNode** child){
	if(index > btree_branch_order){
		PAFFS_DBG(PAFFS_TRACE_BUG, "Tried to access index greater than branch size!");
		return PAFFS_BUG;
	}

	treeCacheNode *target = parent->pointers[index];
	//To make sure parent and child can point to the same variable, target is used as tmp buffer

	if(parent->raw.is_leaf){
		PAFFS_DBG(PAFFS_TRACE_BUG, "Tried to get Node from leaf!");
		return PAFFS_BUG;
	}
	if(target != NULL){
		*child = target;
		return PAFFS_OK;	//cache hit
	}

	PAFFS_RESULT r = addNewCacheNode(dev, &target);
	if(r != PAFFS_OK)
		return r;	//TODO: this could actually be solved by a cache flush

	target->parent = parent;
	*child = target;

	return readTreeNode(dev, parent->raw.as_branch.pointers[index], &(*child)->raw);
}

PAFFS_RESULT addNewCacheNode(p_dev* dev, treeCacheNode** newTcn){
	*newTcn = (treeCacheNode*) malloc(sizeof(treeCacheNode));
	memset(*newTcn, 0, sizeof(treeCacheNode));
	(*newTcn)->dirty = true;
	if(*newTcn == NULL){
		PAFFS_DBG(PAFFS_TRACE_ALLOCATE, "RAN OUT OF RAM!");
		return PAFFS_LOWMEM;
	}
	return PAFFS_OK;
}

PAFFS_RESULT removeCacheNode(p_dev* dev, treeCacheNode* tcn){
	free(tcn);
	return PAFFS_OK;
}

PAFFS_RESULT setCacheRoot(p_dev* dev, treeCacheNode* rootTcn){
	if(rootTcn->parent != rootTcn){
		PAFFS_DBG(PAFFS_TRACE_BUG, "BUG: setCacheRoot with root->parent not pointing to itself");
		return PAFFS_BUG;
	}
	cache_root = rootTcn;
	return PAFFS_OK;
}



