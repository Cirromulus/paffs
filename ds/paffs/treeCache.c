/*
 * treeCache.c
 *
 *  Created on: 23.09.2016
 *      Author: urinator
 */

#include "treeCache.h"
#include "paffs_flash.h"
#include "btree.h"
#include <string.h>

static int16_t cache_root = 0;

static treeCacheNode cache[TREENODECACHESIZE];
//TODO: use static cache instead of malloc usw.

int16_t findFirstFreeIndex(){
	for(int i = 0; i < TREENODECACHESIZE; i++){
		if(cache[i].parent == NULL)
			return i;
	}
	return -1;
}

int16_t getIndexFromPointer(treeCacheNode* tcn){
	return tcn - cache;
}

/*
 * The new tcn->parent has to be changed _before_ calling another addNewCacheNode!
 */
PAFFS_RESULT addNewCacheNode(treeCacheNode** newTcn){
	int16_t index = findFirstFreeIndex();
	if(index < 0){
		PAFFS_DBG(PAFFS_TRACE_ALLOCATE, "RAN OUT OF RAM!");
		return PAFFS_LOWMEM;
	}
	*newTcn = &cache[index];
	memset(*newTcn, 0, sizeof(treeCacheNode));
	PAFFS_DBG_S(PAFFS_TRACE_CACHE, "Created new Cache element %p (position %d)", *newTcn, index);
	return PAFFS_OK;
}

PAFFS_RESULT addNewCacheNodeWithPossibleFlush(p_dev* dev, treeCacheNode** newTcn){
	PAFFS_RESULT r = addNewCacheNode(newTcn);
	if(r == PAFFS_OK)
		return r;
	if(r != PAFFS_LOWMEM)
		return r;
	//First, try to clean up unchanged nodes
	PAFFS_DBG(PAFFS_TRACE_CACHE, "Cache full, cleaning cache!");
	cleanTreeCache();
	r = addNewCacheNode(newTcn);
	if(r == PAFFS_OK)
		return PAFFS_FLUSHEDCACHE;
	if(r != PAFFS_LOWMEM)
		return r;
	//Ok, we have to flush the cache now
	PAFFS_DBG(PAFFS_TRACE_CACHE, "Cache full, flushing cache!");
	flushTreeCache(dev);
	r = addNewCacheNode(newTcn);
	if(r == PAFFS_OK)
		return PAFFS_FLUSHEDCACHE;
	return r;
}

bool isParentPathClean(treeCacheNode* tcn){
	if(tcn->dirty)
		return false;
	if(tcn->parent == tcn)
		return true;
	if(tcn->parent == NULL){
		PAFFS_DBG(PAFFS_TRACE_BUG, "Parent of %p is NULL!", tcn);
		paffs_lasterr = PAFFS_BUG;
		return false;
	}
	return isParentPathClean(tcn->parent);
}

/**
 * returns true if any sibling is dirty
 * stops at first occurrence.
 */
bool areSiblingsClean(treeCacheNode* tcn){
	if(tcn->dirty)
		return false;
	if(tcn->raw.is_leaf)
		return !tcn->dirty;
	for(int i = 0; i <= tcn->raw.num_keys; i++){
		if(tcn->pointers[i] == NULL)	//Siblings not in cache
			continue;
		if(!areSiblingsClean(tcn->pointers[i])){
			tcn->dirty = true;
			return false;
		}
	}
	return true;
}

/**
 * returns true if path contains dirty elements
 * traverses through all paths and marks them
 */
bool resolveDirtyPaths(treeCacheNode* tcn){
	if(tcn->raw.is_leaf)
		return tcn->dirty;

	bool anyDirt = false;
	for(int i = 0; i <= tcn->raw.num_keys; i++){
		if(tcn->pointers[i] == NULL)	//Siblings not in cache
			continue;
		if(resolveDirtyPaths(tcn->pointers[i])){
			tcn->dirty = true;
			anyDirt = true;
		}
	}
	return anyDirt | tcn->dirty;
}

void markParentPathDirty(treeCacheNode* tcn){
	tcn->dirty = true;
	if(tcn->parent == tcn)
		return;
	if(tcn->parent == NULL){
		PAFFS_DBG(PAFFS_TRACE_BUG, "Parent of %p is NULL!", tcn);
		paffs_lasterr = PAFFS_BUG;
		return;
	}
	return markParentPathDirty(tcn->parent);
}

void deleteFromParent(treeCacheNode* tcn){
	treeCacheNode* parent = tcn;
	if(parent == tcn)
		return;
	if(parent == NULL){
		PAFFS_DBG(PAFFS_TRACE_BUG, "Parent of %p is NULL!", tcn);
		paffs_lasterr = PAFFS_BUG;
		return;
	}
	for(int i = 0; i <= parent->raw.num_keys; i++){
		if(parent->pointers[i] == tcn){
			parent->pointers[i] = NULL;
			return;
		}
	}
}

/**
 * Builds up cache with Elements in the Path to tcn.
 * Maybe this function has to be everywhere a tcn is accessed...
 */
PAFFS_RESULT buildUpCacheToNode(p_dev* dev, treeCacheNode* localCopyOfNode, treeCacheNode* cachedOutputNode){
	treeCacheNode* tmp;
	if(localCopyOfNode->raw.is_leaf)
		return find_leaf(dev, localCopyOfNode->raw.as_leaf.keys[0], &tmp);


	return PAFFS_NIMPL;
}


/*
 * Just frees clean nodes
 */
void cleanTreeCache(){

	//debug ---->
	uint16_t usedCache;
	if(paffs_trace_mask & PAFFS_TRACE_CACHE){
		usedCache = getCacheUsage();
	}
	//<---- debug


	resolveDirtyPaths(&cache[cache_root]);
	for(int i = 0; i < TREENODECACHESIZE; i++){
		if(cache[i].dirty){
			deleteFromParent(&cache[i]);
			cache[i].parent = NULL;	//you are free now!
		}
	}

	//debug ---->
	if(paffs_trace_mask & PAFFS_TRACE_CACHE){
		PAFFS_DBG(PAFFS_TRACE_CACHE, "CleanTreeCache freed %d Nodes.", usedCache - getCacheUsage());
	}
	//<---- debug
}

/**
 * Commits complete Tree to Flash
 */

PAFFS_RESULT flushTreeCache(p_dev* dev){

	return PAFFS_NIMPL;
}




PAFFS_RESULT getRootNodeFromCache(p_dev* dev, treeCacheNode** tcn){
	if(cache[cache_root].parent != NULL){
		*tcn = &cache[cache_root];
		return PAFFS_OK;
	}

	p_addr addr = getRootnodeAddr(dev);
	if(addr == 0)
		PAFFS_DBG(PAFFS_TRACE_TREE, "get Rootnode, but does not exist!");

	treeCacheNode* new_root;
	PAFFS_RESULT r = addNewCacheNodeWithPossibleFlush(dev, &new_root);
	if(r != PAFFS_OK){
		PAFFS_DBG(PAFFS_TRACE_ERROR, "Rootnode can't be loaded, cache size (%d) too small!", TREENODECACHESIZE);
		return r;
	}
	new_root->parent = new_root;
	r = setCacheRoot(dev, new_root);
	if(r != PAFFS_OK)
		return r;

	return readTreeNode(dev, addr, &cache[cache_root].raw);
}

/**
 * Possible cache flush. Tree could be empty except for path to child! (and parent, of course)
 */
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
		PAFFS_DBG_S(PAFFS_TRACE_CACHE, "Cache hit, found target %p (position %ld)", target, target - cache);
		return PAFFS_OK;	//cache hit
	}

	treeCacheNode parent_c = *parent;
	PAFFS_RESULT r = addNewCacheNodeWithPossibleFlush(dev, &target);
	if(r == PAFFS_FLUSHEDCACHE){
		//We need to make sure parent and child is in cache again
		r = buildUpCacheToNode(dev, &parent_c, parent);
		if(r != PAFFS_OK)
			return r;
	}
	if(r != PAFFS_OK){
		return r;
	}


	PAFFS_DBG_S(PAFFS_TRACE_CACHE, "Cache Miss, loaded target %p (position %ld)", target, target - cache);

	target->parent = parent;
	*child = target;

	return readTreeNode(dev, parent->raw.as_branch.pointers[index], &(*child)->raw);
}

PAFFS_RESULT removeCacheNode(p_dev* dev, treeCacheNode* tcn){
	PAFFS_DBG(PAFFS_TRACE_BUG, "Is treeCacheNode deleted in Flash?! I dont think so.");
	//TODO: Delete flash pendant
	tcn->parent = NULL;
	return PAFFS_OK;
}

PAFFS_RESULT setCacheRoot(p_dev* dev, treeCacheNode* rootTcn){
	if(rootTcn->parent != rootTcn){
		PAFFS_DBG(PAFFS_TRACE_BUG, "BUG: setCacheRoot with root->parent not pointing to itself");
		return PAFFS_BUG;
	}
	cache_root = getIndexFromPointer(rootTcn);
	return PAFFS_OK;
}


//debug
uint16_t getCacheUsage(){
	uint16_t usage = 0;
	for(int i = 0; i < TREENODECACHESIZE; i++){
		if(cache[i].parent != NULL)
			usage++;
	}
	return usage;
}

uint16_t getCacheSize(){
	return TREENODECACHESIZE;
}

