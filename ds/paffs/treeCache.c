/*
 * treeCache.c
 *
 *  Created on: 23.09.2016
 *      Author: urinator
 */

#include "treeCache.h"
#include "paffs_flash.h"
#include "superblock.h"
#include "btree.h"
#include <string.h>

static int16_t cache_root = 0;

static treeCacheNode cache[TREENODECACHESIZE];

static uint8_t cache_usage[(TREENODECACHESIZE/8)+1];

//Just for debug/tuning purposes
static uint16_t cache_hits = 0;
static uint16_t cache_misses = 0;

void setIndexUsed(uint16_t index){
	if(index > TREENODECACHESIZE){
		PAFFS_DBG(PAFFS_TRACE_BUG, "Tried to set Index used at %u!", index);
		paffs_lasterr = PAFFS_BUG;
	}
	cache_usage[index / 8] |= 1 << index % 8;
}

void setIndexFree(uint16_t index){
	if(index > TREENODECACHESIZE){
		PAFFS_DBG(PAFFS_TRACE_BUG, "Tried to set Index free at %u!", index);
		paffs_lasterr = PAFFS_BUG;
	}
	cache_usage[index / 8] &= ~(1 << index % 8);
}

bool isIndexUsed(uint16_t index){
	if(index > TREENODECACHESIZE){
		PAFFS_DBG(PAFFS_TRACE_BUG, "Tried to query Index Used at %u!", index);
		paffs_lasterr = PAFFS_BUG;
		return true;
	}
	return cache_usage[index / 8] & (1 << index % 8);
}

int16_t findFirstFreeIndex(){
	for(int i = 0; i <= TREENODECACHESIZE/8; i++){
		if(cache_usage[i] != 0xFF)
			for(int j = 0; j < 8; j++)
				if(i*8 + j < TREENODECACHESIZE && !isIndexUsed(i*8 + j))
					return i*8 + j;
	}
	return -1;
}

int16_t getIndexFromPointer(treeCacheNode* tcn){
	if(tcn - cache > TREENODECACHESIZE){
		PAFFS_DBG(PAFFS_TRACE_BUG, "Tried to get Index from Pointer not inside array (%p)!", tcn);
		paffs_lasterr = PAFFS_BUG;
		return 0;
	}
	return tcn - cache;
}

void initCache(){
	memset(cache, 0, TREENODECACHESIZE * sizeof(treeCacheNode));
	memset(cache_usage, 0, TREENODECACHESIZE / 8 + 1);
}

PAFFS_RESULT addNewCacheNode(treeCacheNode** newTcn){
	int16_t index = findFirstFreeIndex();
	if(index < 0){
		PAFFS_DBG_S(PAFFS_TRACE_CACHE, "Cache is full!");
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
 * IDEA: Lock nodes when operating to prevent deletion
 */
PAFFS_RESULT addNewCacheNodeWithPossibleFlush(p_dev* dev, treeCacheNode** newTcn){
	PAFFS_RESULT r = addNewCacheNode(newTcn);
	if(r == PAFFS_OK)
		return r;
	if(r != PAFFS_LOWMEM)
		return r;
	if(!isTreeCacheValid()){
		printTreeCache();
		return PAFFS_BUG;
	}

	if(paffs_trace_mask & PAFFS_TRACE_CACHE){
		printTreeCache();
	}

	//First, try to clean up unchanged nodes
	PAFFS_DBG_S(PAFFS_TRACE_CACHE, "Freeing clean leaves.");
	paffs_lasterr = PAFFS_OK;	//not nice code
	cleanTreeCacheLeaves();
	if(paffs_lasterr != PAFFS_OK)
		return paffs_lasterr;
	r = addNewCacheNode(newTcn);
	if(r == PAFFS_OK){
		if(paffs_trace_mask & PAFFS_TRACE_CACHE){
			printTreeCache();
		}
		return PAFFS_FLUSHEDCACHE;
	}
	if(r != PAFFS_LOWMEM)
		return r;

	PAFFS_DBG_S(PAFFS_TRACE_CACHE, "Freeing clean nodes.");
	cleanTreeCache();
	if(paffs_lasterr != PAFFS_OK)
		return paffs_lasterr;
	r = addNewCacheNode(newTcn);
	if(r == PAFFS_OK){
		if(paffs_trace_mask & PAFFS_TRACE_CACHE){
			printTreeCache();
		}
		return PAFFS_FLUSHEDCACHE;
	}
	if(r != PAFFS_LOWMEM)
		return r;

	//Ok, we have to flush the cache now
	PAFFS_DBG_S(PAFFS_TRACE_CACHE, "Flushing cache.");
	commitTreeCache(dev);
	r = addNewCacheNode(newTcn);
	if(r == PAFFS_OK){
		if(paffs_trace_mask & PAFFS_TRACE_CACHE){
			printTreeCache();
		}
		return PAFFS_FLUSHEDCACHE;
	}
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

bool isSubTreeValid(treeCacheNode* node, uint8_t* cache_node_reachable, long keyMin, long keyMax){

	cache_node_reachable[getIndexFromPointer(node) / 8] |= 1 << getIndexFromPointer(node) % 8;

	if(node->parent == NULL){
		PAFFS_DBG(PAFFS_TRACE_BUG, "Node n° %d has invalid parent!", getIndexFromPointer(node));
		return false;
	}

	if(node->raw.self == 0 && !node->dirty){
		PAFFS_DBG(PAFFS_TRACE_BUG, "Node n° %d is not dirty, but has no flash address!", getIndexFromPointer(node));
		return false;
	}

	if(node->raw.is_leaf){
		int last = -1;
		for(int i = 0; i < node->raw.num_keys; i++){
			if(node->raw.as_leaf.keys[i] != node->raw.as_leaf.pInodes[i].no){
				PAFFS_DBG(PAFFS_TRACE_BUG, "Node n° %d has different Inode number (%d) than its key stated (%d)!", getIndexFromPointer(node), node->raw.as_leaf.keys[i], node->raw.as_leaf.pInodes[i].no);
				return false;
			}

			if(last != -1 && node->raw.as_leaf.keys[i] < last){
				PAFFS_DBG(PAFFS_TRACE_BUG, "Node n° %d is not sorted (prev: %d, curr: %d)!", getIndexFromPointer(node), last, node->raw.as_leaf.keys[i]);
				return false;
			}
			last = node->raw.as_leaf.keys[i];

			if(keyMin != 0){
				if(node->raw.as_leaf.keys[i] < keyMin){
					PAFFS_DBG(PAFFS_TRACE_BUG, "Node n° %u's keys are inconsistent!\n"
							"\twas: %u, but parent stated keys would be over or equal %ld!",
							getIndexFromPointer(node), node->raw.as_leaf.keys[i], keyMin);
					return false;
				}
			}
			if(keyMax != 0){
				if(node->raw.as_leaf.keys[i] >= keyMax){
					PAFFS_DBG(PAFFS_TRACE_BUG, "Node n° %u's keys are inconsistent!\n"
							"\twas: %u, but parent stated keys would be under %ld!",
							getIndexFromPointer(node), node->raw.as_leaf.keys[i], keyMax);
					return false;
				}
			}
		}
	}else{
		int last = -1;
		for(int i = 0; i <= node->raw.num_keys; i++){

			if(i < node->raw.num_keys){
				if(keyMin != 0){
					if(node->raw.as_branch.keys[i] < keyMin){
						PAFFS_DBG(PAFFS_TRACE_BUG, "Node n° %u's keys are inconsistent!\n"
								"\twas: %u, but parent stated keys would be over or equal %ld!",
								getIndexFromPointer(node), node->raw.as_branch.keys[i], keyMin);
						return false;
					}
				}
				if(keyMax != 0){
					if(node->raw.as_branch.keys[i] >= keyMax){
						PAFFS_DBG(PAFFS_TRACE_BUG, "Node n° %u's keys are inconsistent!\n"
								"\twas: %u, but parent stated keys would be under %ld!",
								getIndexFromPointer(node), node->raw.as_branch.keys[i], keyMax);
						return false;
					}
				}


				if(last != -1 && node->raw.as_branch.keys[i] < last){
					PAFFS_DBG(PAFFS_TRACE_BUG, "Node n° %d is not sorted (prev: %d, curr: %d)!", getIndexFromPointer(node), node->raw.as_leaf.keys[i], last);
					return false;
				}
				last = node->raw.as_branch.keys[i];
			}

			if(node->pointers[i] != NULL){
				if(node->pointers[i]->parent != node){
					PAFFS_DBG(PAFFS_TRACE_BUG, "Node n° %d stated parent was %d, but is actually %d!"
						,getIndexFromPointer(node->pointers[i]), getIndexFromPointer(node->pointers[i]->parent), getIndexFromPointer(node));
					return false;
				}
				long keyMin_n = i == 0 ? 0 : node->raw.as_branch.keys[i-1];
				long keyMax_n = i >= node->raw.num_keys ? 0 : node->raw.as_branch.keys[i];
				if(!isSubTreeValid(node->pointers[i], cache_node_reachable, keyMin_n, keyMax_n))
					return false;
			}
		}
	}

	return true;
}


bool isTreeCacheValid(){
	//Just for debugging purposes
	//TODO: Switch to deactivate this costly but safer execution
	uint8_t cache_node_reachable[(TREENODECACHESIZE/8)+1];
	memset(cache_node_reachable, 0, (TREENODECACHESIZE/8)+1);	//See c. 162


	if(!isIndexUsed(cache_root))
		return true;


	if(!isSubTreeValid(&cache[cache_root], cache_node_reachable, 0, 0))
		return false;

	if(memcmp(cache_node_reachable,cache_usage, (TREENODECACHESIZE/8)+1)){
		for(int i = 0; i <= TREENODECACHESIZE/8; i++){
			for(int j = 0; j < 8; j++){
				if((cache_usage[i*8] & 1 << j % 8) < (cache_node_reachable[i*8] & 1 << j % 8)){
					PAFFS_DBG(PAFFS_TRACE_BUG, "Deleted Node n° %d still reachable!", i*8 + j);
					return false;
				}
				if((cache_usage[i*8] & 1 << j % 8) > (cache_node_reachable[i*8] & 1 << j % 8)){
					if(!cache[i*8+j].locked && !cache[i*8+j].inheritedLock){
						PAFFS_DBG(PAFFS_TRACE_BUG, "Cache contains unreachable node %d!", i*8 + j);
						return false;
					}
				}
			}
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
		if(tcn->pointers[i] == NULL)
			continue;
		if(!isIndexUsed(getIndexFromPointer(tcn->pointers[i])))	//Sibling is not in cache)
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
	if(tcn == NULL){
		PAFFS_DBG(PAFFS_TRACE_BUG, "Tried to delete NULL node from Parent!");
		paffs_lasterr = PAFFS_BUG;
		return;
	}
	if(tcn->parent == NULL){
		PAFFS_DBG(PAFFS_TRACE_BUG, "Tried to delete node %d from NULL parent!", getIndexFromPointer(tcn));
		isTreeCacheValid();	//This hopefully prints more detailed information
		paffs_lasterr = PAFFS_BUG;
		return;
	}
	treeCacheNode* parent = tcn->parent;
	if(parent == tcn)
		return;
	if(!isIndexUsed(getIndexFromPointer(parent))){
		//PAFFS_DBG(PAFFS_TRACE_BUG, "Parent of %p is not in cache!", tcn);
		//paffs_lasterr = PAFFS_BUG;	//This is not a bug since the parent could be freed before the sibling
		return;
	}
	for(int i = 0; i <= parent->raw.num_keys; i++){
		if(parent->pointers[i] == tcn){
			parent->pointers[i] = NULL;
			return;
		}
	}
}


bool hasNoSiblings(treeCacheNode* tcn){
	if(tcn->raw.is_leaf)
		return true;
	for(int i = 0; i <= tcn->raw.num_keys; i++)
		if(tcn->pointers[i] != NULL)
			return false;
	return true;
}

void deletePathToRoot(treeCacheNode* tcn){
	if(tcn->dirty)
		return;

	deleteFromParent(tcn);
	setIndexFree(getIndexFromPointer(tcn));
	if(tcn->parent != tcn && hasNoSiblings(tcn->parent))
		deletePathToRoot(tcn->parent);
}
/**
 * Builds up cache with Elements in the Path to tcn.
 * Maybe this function has to be everywhere a tcn is accessed...
 */
/*PAFFS_RESULT buildUpCacheToNode(p_dev* dev, treeCacheNode* localCopyOfNode, treeCacheNode** cachedOutputNode){
	if(localCopyOfNode->raw.is_leaf)
		return find_leaf(dev, localCopyOfNode->raw.as_leaf.keys[0], cachedOutputNode);

	return find_branch(dev, localCopyOfNode, cachedOutputNode);
}*/


/*
 * Just frees clean leaf nodes
 */
void cleanTreeCacheLeaves(){

	//debug ---->
	uint16_t usedCache;
	if(paffs_trace_mask & PAFFS_TRACE_CACHE){
		usedCache = getCacheUsage();
	}
	//<---- debug


	resolveDirtyPaths(&cache[cache_root]);
	if(paffs_lasterr != PAFFS_OK)
		return;
	for(int i = 0; i < TREENODECACHESIZE; i++){
		if(!isIndexUsed(getIndexFromPointer(&cache[i])))
			continue;
		if(!cache[i].dirty && !cache[i].locked && !cache[i].inheritedLock && cache[i].raw.is_leaf){
			deleteFromParent(&cache[i]);
			setIndexFree(i);
		}
	}

	//debug ---->
	if(paffs_trace_mask & PAFFS_TRACE_CACHE){
		PAFFS_DBG_S(PAFFS_TRACE_CACHE, "CleanTreeCacheLeaves freed %d Leaves.", usedCache - getCacheUsage());
	}
	//<---- debug
}

/*
 * Frees clean nodes
 */
void cleanTreeCache(){
	//TODO: This only removes one layer of clean nodes, should check whole path to root
	//debug ---->
	uint16_t usedCache;
	if(paffs_trace_mask & PAFFS_TRACE_CACHE){
		if(!isTreeCacheValid()){
			paffs_lasterr = PAFFS_BUG;
			return;
		}
		usedCache = getCacheUsage();
	}
	//<---- debug


	resolveDirtyPaths(&cache[cache_root]);
	for(int i = 0; i < TREENODECACHESIZE; i++){
		if(!isIndexUsed(getIndexFromPointer(&cache[i])))
			continue;
		if(!cache[i].dirty && !cache[i].locked && !cache[i].inheritedLock && hasNoSiblings(&cache[i])){
			deleteFromParent(&cache[i]);
			setIndexFree(i);
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
	memset(cache_usage, 0, TREENODECACHESIZE/8 + 1);
	memset(cache, 0, TREENODECACHESIZE * sizeof(treeCacheNode));
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
	PAFFS_DBG(PAFFS_TRACE_BUG, "Could not find Node %d in its parent (Node %d)!", getIndexFromPointer(node), getIndexFromPointer(node->parent));
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
		node->dirty = false;
		return updateFlashAddressInParent(dev, node);
	}

	for(int i = 0; i <= node->raw.num_keys; i++){
		if(node->pointers[i] == NULL)
			continue;
		r = commitNodesRecursively(dev, node->pointers[i]);
		if(r != PAFFS_OK){
			PAFFS_DBG(PAFFS_TRACE_ERROR, "Could not write Node %d's child n° %d (%d) to flash!",getIndexFromPointer(node), i, getIndexFromPointer(node->pointers[i]));
			return r;
		}
	}

	r = writeTreeNode(dev, &node->raw);
	if(r != PAFFS_OK){
		PAFFS_DBG(PAFFS_TRACE_ERROR, "Could not write cached Treenode branch!");
		return r;
	}
	node->dirty = false;
	return updateFlashAddressInParent(dev, node);
}

/**
 * Commits complete Tree to Flash
 */

PAFFS_RESULT commitTreeCache(p_dev* dev){

	if(!isTreeCacheValid()){
		return PAFFS_BUG;
	}

	//debug ---->
	uint16_t usedCache;
	if(paffs_trace_mask & PAFFS_TRACE_CACHE){
		usedCache = getCacheUsage();
	}
	//<---- debug


	resolveDirtyPaths(&cache[cache_root]);
	if(paffs_lasterr != PAFFS_OK)
		return paffs_lasterr;
	PAFFS_RESULT r = commitNodesRecursively(dev, &cache[cache_root]);
	if(r != PAFFS_OK){
		PAFFS_DBG(PAFFS_TRACE_ERROR, "Could not write Node to flash! (%s)", paffs_err_msg(r));
		return r;
	}


	cleanTreeCacheLeaves();
	if(paffs_lasterr != PAFFS_OK)
		return paffs_lasterr;

	if(findFirstFreeIndex() < 0)
		cleanTreeCache();	//if tree cache did not contain any leaves (unlikely)

	//debug ---->
	if(paffs_trace_mask & PAFFS_TRACE_CACHE){
		PAFFS_DBG_S(PAFFS_TRACE_CACHE, "flushTreeCache freed %d Nodes.", usedCache - getCacheUsage());
	}
	//<---- debug

	return PAFFS_OK;
}

/**
 * This locks specified treeCache node and its path from Rootnode
 * To prevent Cache GC from deleting it
 */
PAFFS_RESULT lockTreeCacheNode(p_dev* dev, treeCacheNode* tcn){
	tcn->locked = true;
	if(tcn->parent == NULL)
		return PAFFS_OK;	//FIXME: Is it allowed to violate the treerules?

	treeCacheNode* curr = tcn->parent;
	while(curr->parent != curr){
		curr->inheritedLock = true;
		curr = curr->parent;
	}
	curr->inheritedLock = true;

	return PAFFS_OK;
}

bool hasLockedChilds(p_dev* dev, treeCacheNode* tcn){
	if(tcn == NULL){
		PAFFS_DBG(PAFFS_TRACE_ERROR, "Node is invalid!");
		return false;
	}
	if(tcn->raw.is_leaf){
		//PAFFS_DBG(PAFFS_TRACE_BUG, "Node is leaf, has no childs!");
		return false;
	}
	for(int i = 0; i <= tcn->raw.num_keys; i++){
		if(tcn->pointers[i] != NULL){
			if(tcn->pointers[i]->inheritedLock || tcn->pointers[i]->locked)
				return true;
		}
	}
	return false;
}

PAFFS_RESULT unlockTreeCacheNode(p_dev* dev, treeCacheNode* tcn){
	if(!tcn->locked){
		PAFFS_DBG(PAFFS_TRACE_ERROR, "Tried to double-unlock node n° %d!", getIndexFromPointer(tcn));
	}
	tcn->locked = false;

	if(tcn->parent == NULL){
		PAFFS_DBG(PAFFS_TRACE_ERROR, "Node %d with invalid parent !", getIndexFromPointer(tcn));
		return PAFFS_FAIL;
	}

	treeCacheNode* curr = tcn->parent;
	treeCacheNode *old = NULL;
	do{
		if(hasLockedChilds(dev, curr))
			break;

		curr->inheritedLock = false;

		if(curr->locked)
			break;

		if(tcn->parent == NULL){
			PAFFS_DBG(PAFFS_TRACE_ERROR, "Node %d with invalid parent !", getIndexFromPointer(tcn));
			return PAFFS_FAIL;
		}
		old = curr;
		curr = curr->parent;

	}while(old != curr);

	return PAFFS_OK;
}


PAFFS_RESULT getRootNodeFromCache(p_dev* dev, treeCacheNode** tcn){
	if(isIndexUsed(cache_root)){
		*tcn = &cache[cache_root];
		cache_hits++;
		return PAFFS_OK;
	}
	cache_misses++;

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
	//To make sure parent and child can point to the same address, target is used as tmp buffer

	if(parent->raw.is_leaf){
		PAFFS_DBG(PAFFS_TRACE_BUG, "Tried to get Node from leaf!");
		return PAFFS_BUG;
	}
	if(target != NULL){
		*child = target;
		cache_hits++;
		PAFFS_DBG_S(PAFFS_TRACE_CACHE, "Cache hit, found target %p (position %ld)", target, target - cache);
		return PAFFS_OK;	//cache hit
	}

	//--------------

	if(getIndexFromPointer(parent) == 0 && paffs_lasterr != PAFFS_OK){
		PAFFS_DBG(PAFFS_TRACE_BUG, "Tried to get child from Treenode not located in cache!");
		paffs_lasterr = PAFFS_OK;
		return PAFFS_EINVAL;
	}

	if(parent->raw.as_branch.pointers[index] == 0){
		PAFFS_DBG(PAFFS_TRACE_ERROR, "Tried to get treenode neither located in cache nor in flash!");
		return PAFFS_EINVAL;
	}

	cache_misses++;
	PAFFS_DBG_S(PAFFS_TRACE_CACHE, "Cache Miss");

	lockTreeCacheNode(dev, parent);
	PAFFS_RESULT r = addNewCacheNodeWithPossibleFlush(dev, &target);
	if(r == PAFFS_FLUSHEDCACHE){

	}else if(r != PAFFS_OK){
		return r;
	}
	unlockTreeCacheNode(dev, parent);


	target->parent = parent;
	parent->pointers[index] = target;
	*child = target;

	PAFFS_RESULT r2 = readTreeNode(dev, parent->raw.as_branch.pointers[index], &(*child)->raw);
	if(r2 != PAFFS_OK)
		return r2;
	return r;
}

PAFFS_RESULT removeCacheNode(p_dev* dev, treeCacheNode* tcn){
	setIndexFree(getIndexFromPointer(tcn));
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

uint16_t getCacheHits(){
	return cache_hits;
}
uint16_t getCacheMisses(){
	return cache_misses;
}

void printSubtree(int layer, treeCacheNode* node){
	for(int i = 0; i < layer; i++){
		printf(".");
	}
	printf("[ID: %d PAR: %d %s%s%s|", getIndexFromPointer(node), getIndexFromPointer(node->parent),
			node->dirty ? "d" : "-", node->locked ? "l" : "-", node->inheritedLock ? "i" : "-");
	if(node->raw.is_leaf){
		for(int i = 0; i < node->raw.num_keys; i++){
			if(i > 0)
				printf(",");
			printf (" %d", node->raw.as_leaf.keys[i]);
		}
		printf("]\n");
	}else{
		if(node->pointers[0] == 0)
			printf("x");
		else
			printf("%d", getIndexFromPointer(node->pointers[0]));

		bool isGap = false;
		for(int i = 1; i <= node->raw.num_keys; i++) {
			if(!isGap)
				printf("/%d\\", node->raw.as_branch.keys[i-1]);
			if(node->pointers[i] == 0){
				if(!isGap){
					printf("...");
					isGap = true;
				}
			}
			else{
				printf("%d", getIndexFromPointer(node->pointers[i]));
				isGap = false;
			}



		}
		printf("]\n");
		for(int i = 0; i <= node->raw.num_keys; i++) {
			if(node->pointers[i] != 0)
				printSubtree(layer+1, node->pointers[i]);
		}
	}

}
void printTreeCache(){
	printf("----------------\n");
	if(isIndexUsed(cache_root)){
		printSubtree(0, &cache[cache_root]);
	}else
		printf("Empty treeCache.\n");
	printf("----------------\n");
}
