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

static uint8_t cache_usage[(TREENODECACHESIZE/8)+1];

void setIndexUsed(uint16_t index){
	cache_usage[index / 8] |= 1 << index % 8;
}

void setIndexFree(uint16_t index){
	cache_usage[index / 8] &= ~(1 << index % 8);
}

bool isIndexUsed(uint16_t index){
	return cache_usage[index / 8] & (1 << index % 8);
}

int16_t findFirstFreeIndex(){
	for(int i = 0; i <= TREENODECACHESIZE/8; i++){
		if(cache_usage[i] != 0xFF)
			for(int j = 0; j < 8; j++)
				if(!isIndexUsed(i*8 + j) && i*8 + j < TREENODECACHESIZE)
					return i*8 + j;
	}
	return -1;
}

int16_t getIndexFromPointer(treeCacheNode* tcn){
	return tcn - cache;
}

PAFFS_RESULT addNewCacheNode(treeCacheNode** newTcn){
	int16_t index = findFirstFreeIndex();
	if(index < 0){
		PAFFS_DBG(PAFFS_TRACE_ALLOCATE, "Ran out of RAM while trying to add new cache Element!");
		return PAFFS_LOWMEM;
	}
	*newTcn = &cache[index];
	memset(*newTcn, 0, sizeof(treeCacheNode));
	setIndexUsed(index);
	PAFFS_DBG_S(PAFFS_TRACE_CACHE, "Created new Cache element %p (position %d)", *newTcn, index);
	return PAFFS_OK;
}

/*
 * The new tcn->parent has to be changed _before_ calling another addNewCacheNode!
 */
PAFFS_RESULT addNewCacheNodeWithPossibleFlush(p_dev* dev, treeCacheNode** newTcn){
	PAFFS_RESULT r = addNewCacheNode(newTcn);
	if(r == PAFFS_OK)
		return r;
	if(r != PAFFS_LOWMEM)
		return r;
	//First, try to clean up unchanged nodes
	PAFFS_DBG_S(PAFFS_TRACE_CACHE, "Cache full, cleaning cache!");
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
		if(!isIndexUsed(getIndexFromPointer(tcn->pointers[i])))	//Siblings not in cache
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
	if(isIndexUsed(getIndexFromPointer(tcn->parent))){
		PAFFS_DBG(PAFFS_TRACE_BUG, "Parent of %p is not in cache!", tcn);
		paffs_lasterr = PAFFS_BUG;
		return;
	}
	return markParentPathDirty(tcn->parent);
}

void deleteFromParent(treeCacheNode* tcn){
	treeCacheNode* parent = tcn->parent;
	if(parent == tcn)
		return;
	if(isIndexUsed(getIndexFromPointer(parent))){
		PAFFS_DBG(PAFFS_TRACE_BUG, "Parent of %p is not in cache!", tcn);
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
PAFFS_RESULT buildUpCacheToNode(p_dev* dev, treeCacheNode* localCopyOfNode, treeCacheNode** cachedOutputNode){
	if(localCopyOfNode->raw.is_leaf)
		return find_leaf(dev, localCopyOfNode->raw.as_leaf.keys[0], cachedOutputNode);

	return find_branch(dev, localCopyOfNode, cachedOutputNode);
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
		if(cache[i].parent == NULL)
			continue;
		if(!cache[i].dirty){
			deleteFromParent(&cache[i]);
			setIndexFree(i);	//you are free now!
		}
	}

	//debug ---->
	if(paffs_trace_mask & PAFFS_TRACE_CACHE){
		PAFFS_DBG_S(PAFFS_TRACE_CACHE, "CleanTreeCache freed %d Nodes.", usedCache - getCacheUsage());
	}
	//<---- debug
}

/**
 * Deletes all the nodes
 */
void deleteTreeCache(){
	for(int i = 0; i < TREENODECACHESIZE; i++){
		setIndexFree(i);
	}
}

/**
 * Takes the node->raw.self to update parents flash pointer
 */
PAFFS_RESULT updateFlashAddressInParent(p_dev* dev, treeCacheNode* node){
	if(node->parent == node){
		//Rootnode
		return registerRootnode(dev, node->raw.self);
	}
	for(int i = 0; i <= node->parent->raw.num_keys; i++){
		if(node->parent->pointers[i] == node){
			node->parent->raw.as_branch.pointers[i] = node->raw.self;
			return PAFFS_OK;
		}
	}

	return PAFFS_NF;
}

PAFFS_RESULT commitNodesRecursively(p_dev* dev, treeCacheNode* node) {
	PAFFS_RESULT r;
	if(node->raw.is_leaf){
		r = writeTreeNode(dev, &node->raw);
		if(r != PAFFS_OK){
			PAFFS_DBG(PAFFS_TRACE_ERROR, "Could not write cached Treenode leaf!");
			return r;
		}
		return updateFlashAddressInParent(dev, node);
	}

	for(int i = 0; i <= node->raw.num_keys; i++){
		if(node->pointers[i] == NULL)
			continue;
		r = commitNodesRecursively(dev, node->pointers[i]);
		if(r != PAFFS_OK)
			return r;
	}

	r = writeTreeNode(dev, &node->raw);
	if(r != PAFFS_OK){
		PAFFS_DBG(PAFFS_TRACE_ERROR, "Could not write cached Treenode branch!");
		return r;
	}
	return updateFlashAddressInParent(dev, node);
}

/**
 * Commits complete Tree to Flash
 */

PAFFS_RESULT flushTreeCache(p_dev* dev){
	//debug ---->
	uint16_t usedCache;
	if(paffs_trace_mask & PAFFS_TRACE_CACHE){
		usedCache = getCacheUsage();
	}
	//<---- debug


	cleanTreeCache();
	treeCacheNode* root;
	PAFFS_RESULT r = getRootNodeFromCache(dev, &root);
	if(r != PAFFS_OK)
		return r;

	r = commitNodesRecursively(dev, root);
	if(r != PAFFS_OK)
		return r;

	//TODO: Dont delete all of the Node elements for optimizing usage
	deleteTreeCache();

	//debug ---->
	if(paffs_trace_mask & PAFFS_TRACE_CACHE){
		PAFFS_DBG_S(PAFFS_TRACE_CACHE, "flushTreeCache freed %d Nodes.", usedCache - getCacheUsage());
	}
	//<---- debug

	return PAFFS_OK;
}




PAFFS_RESULT getRootNodeFromCache(p_dev* dev, treeCacheNode** tcn){
	if(isIndexUsed(cache_root)){
		*tcn = &cache[cache_root];
		return PAFFS_OK;
	}

	PAFFS_DBG_S(PAFFS_TRACE_CACHE, "Load rootnode from Flash");

	p_addr addr = getRootnodeAddr(dev);
	if(addr == 0)
		PAFFS_DBG(PAFFS_TRACE_TREE, "get Rootnode, but does not exist!");

	treeCacheNode* new_root;
	PAFFS_RESULT r = addNewCacheNode(&new_root);
	if(r != PAFFS_OK){
		PAFFS_DBG(PAFFS_TRACE_ERROR, "Rootnode can't be loaded, cache size (%d) too small!", TREENODECACHESIZE);
		return r;
	}
	new_root->parent = new_root;
	r = setCacheRoot(dev, new_root);
	if(r != PAFFS_OK)
		return r;

	*tcn = new_root;

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
		r = buildUpCacheToNode(dev, &parent_c, &parent);
		if(r != PAFFS_OK)
			return r;
	}
	if(r != PAFFS_OK){
		return r;
	}


	PAFFS_DBG_S(PAFFS_TRACE_CACHE, "Cache Miss, loaded target %p (position %ld)", target, target - cache);

	target->parent = parent;
	parent->pointers[index] = target;
	*child = target;

	return readTreeNode(dev, parent->raw.as_branch.pointers[index], &(*child)->raw);
}

PAFFS_RESULT removeCacheNode(p_dev* dev, treeCacheNode* tcn){
	tcn->parent = NULL;
	if(tcn->raw.self != 0) {
		return deleteTreeNode(dev, &tcn->raw);
	}

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
		if(isIndexUsed(i))
			usage++;
	}
	return usage;
}

uint16_t getCacheSize(){
	return TREENODECACHESIZE;
}

