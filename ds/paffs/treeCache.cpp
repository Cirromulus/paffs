/*
 * treeCache.c
 *
 *  Created on: 23.09.2016
 *      Author: urinator
 */

#include "treeCache.hpp"
#include "device.hpp"
#include "dataIO.hpp"
#include "btree.hpp"
#include <inttypes.h>
#include <string.h>

namespace paffs{

void TreeCache::setIndexUsed(uint16_t index){
	if(index > treeNodeCacheSize){
		PAFFS_DBG(PAFFS_TRACE_BUG, "Tried to set Index used at %u!", index);
		dev->lasterr = Result::bug;
	}
	cache_usage[index / 8] |= 1 << index % 8;
}

void TreeCache::setIndexFree(uint16_t index){
	if(index > treeNodeCacheSize){
		PAFFS_DBG(PAFFS_TRACE_BUG, "Tried to set Index free at %u!", index);
		dev->lasterr = Result::bug;
	}
	cache_usage[index / 8] &= ~(1 << index % 8);
}

bool TreeCache::isIndexUsed(uint16_t index){
	if(index > treeNodeCacheSize){
		PAFFS_DBG(PAFFS_TRACE_BUG, "Tried to query Index Used at %u!", index);
		dev->lasterr = Result::bug;
		return true;
	}
	return cache_usage[index / 8] & (1 << index % 8);
}

int16_t TreeCache::findFirstFreeIndex(){
	for(int i = 0; i <= treeNodeCacheSize/8; i++){
		if(cache_usage[i] != 0xFF)
			for(int j = 0; j < 8; j++)
				if(i*8 + j < treeNodeCacheSize && !isIndexUsed(i*8 + j))
					return i*8 + j;
	}
	return -1;
}

int16_t TreeCache::getIndexFromPointer(TreeCacheNode* tcn){
	if(tcn - cache > treeNodeCacheSize){
		PAFFS_DBG(PAFFS_TRACE_BUG, "Tried to get Index from Pointer not inside array (%p)!", tcn);
		dev->lasterr = Result::bug;
		return 0;
	}
	return tcn - cache;
}

/**
 * Deletes all the nodes
 */
void TreeCache::clear(){
	memset(cache, 0, treeNodeCacheSize * sizeof(TreeCacheNode));
	memset(cache_usage, 0, treeNodeCacheSize / 8 + 1);
}

Result TreeCache::tryAddNewCacheNode(TreeCacheNode** newTcn){
	int16_t index = findFirstFreeIndex();
	if(index < 0){
		PAFFS_DBG_S(PAFFS_TRACE_TREECACHE, "Cache is full!");
		if(traceMask & PAFFS_TRACE_TREECACHE)
			printTreeCache();
		return Result::lowmem;
	}
	*newTcn = &cache[index];
	memset(*newTcn, 0, sizeof(TreeCacheNode));
	setIndexUsed(index);
	PAFFS_DBG_S(PAFFS_TRACE_TREECACHE, "Created new Cache element %p (position %d)", *newTcn, index);
	return Result::ok;
}

/*
 * The new tcn->parent has to be changed _before_ calling another addNewCacheNode!
 * IDEA: Lock nodes when operating to prevent deletion
 */
Result TreeCache::addNewCacheNode(TreeCacheNode** newTcn){
	Result r = tryAddNewCacheNode(newTcn);
	if(r == Result::ok)
		return r;
	if(r != Result::lowmem)
		return r;
	if(traceMask & PAFFS_TRACE_VERIFY_TC && !isTreeCacheValid()){
		printTreeCache();
		return Result::bug;
	}

	if(traceMask & PAFFS_TRACE_TREECACHE){
		printTreeCache();
	}

	//First, try to clean up unchanged nodes
	PAFFS_DBG_S(PAFFS_TRACE_TREECACHE, "Freeing clean leaves.");
	dev->lasterr = Result::ok;	//not nice code
	cleanFreeLeafNodes();
	if(dev->lasterr != Result::ok)
		return dev->lasterr;
	r = tryAddNewCacheNode(newTcn);
	if(r == Result::ok){
		if(traceMask & PAFFS_TRACE_TREECACHE){
			printTreeCache();
		}
		return Result::ok;
	}
	if(r != Result::lowmem)
		return r;

	PAFFS_DBG_S(PAFFS_TRACE_TREECACHE, "Freeing clean nodes.");
	cleanFreeNodes();
	if(dev->lasterr != Result::ok)
		return dev->lasterr;
	r = tryAddNewCacheNode(newTcn);
	if(r == Result::ok){
		if(traceMask & PAFFS_TRACE_TREECACHE){
			printTreeCache();
		}
		return Result::ok;
	}
	if(r != Result::lowmem)
		return r;

	//Ok, we have to flush the cache now
	PAFFS_DBG_S(PAFFS_TRACE_TREECACHE, "Flushing cache.");
	r = commitCache();
	if(r != Result::ok)
		return r;

	r = tryAddNewCacheNode(newTcn);
	if(r == Result::ok){
		if(traceMask & PAFFS_TRACE_TREECACHE){
			printTreeCache();
		}
		return Result::ok;
	}
	return r;
}

bool TreeCache::isParentPathClean(TreeCacheNode* tcn){
	if(tcn->dirty)
		return false;
	if(tcn->parent == tcn)
		return true;
	if(tcn->parent == NULL){
		PAFFS_DBG(PAFFS_TRACE_BUG, "Parent of %p is NULL!", tcn);
		dev->lasterr = Result::bug;
		return false;
	}
	return isParentPathClean(tcn->parent);
}

/**
 * returns true if any sibling is dirty
 * stops at first occurrence.
 */
bool TreeCache::areSiblingsClean(TreeCacheNode* tcn){
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

bool TreeCache::isSubTreeValid(TreeCacheNode* node, uint8_t* cache_node_reachable, InodeNo keyMin, InodeNo keyMax){

	cache_node_reachable[getIndexFromPointer(node) / 8] |= 1 << getIndexFromPointer(node) % 8;

	if(node->parent == NULL){
		PAFFS_DBG(PAFFS_TRACE_BUG, "Node n° %d has invalid parent!", getIndexFromPointer(node));
		return false;
	}

	if(node->raw.self == 0 && !node->dirty){
		PAFFS_DBG(PAFFS_TRACE_BUG, "Node n° %d is not dirty, but has no flash address!", getIndexFromPointer(node));
		return false;
	}
	InodeNo last = 0;
	bool first = true;
	if(node->raw.is_leaf){
		for(int i = 0; i < node->raw.num_keys; i++){
			if(node->raw.as.leaf.keys[i] != node->raw.as.leaf.pInodes[i].no){
				PAFFS_DBG(PAFFS_TRACE_BUG, "Node n° %d has different Inode number (%d) than its key stated (%d)!", getIndexFromPointer(node), node->raw.as.leaf.keys[i], node->raw.as.leaf.pInodes[i].no);
				return false;
			}

			if(!first && node->raw.as.leaf.keys[i] < last){
				PAFFS_DBG(PAFFS_TRACE_BUG, "Node n° %d is not sorted (prev: %d, curr: %d)!", getIndexFromPointer(node), last, node->raw.as.leaf.keys[i]);
				return false;
			}
			last = node->raw.as.leaf.keys[i];
			first = false;

			if(keyMin != 0){
				if(node->raw.as.leaf.keys[i] < keyMin){
					PAFFS_DBG(PAFFS_TRACE_BUG, "Node n° %u's keys are inconsistent!\n"
							"\twas: %u, but parent stated keys would be over or equal %u!",
							getIndexFromPointer(node), node->raw.as.leaf.keys[i], keyMin);
					return false;
				}
			}
			if(keyMax != 0){
				if(node->raw.as.leaf.keys[i] >= keyMax){
					PAFFS_DBG(PAFFS_TRACE_BUG, "Node n° %u's keys are inconsistent!\n"
							"\twas: %u, but parent stated keys would be under %u!",
							getIndexFromPointer(node), node->raw.as.leaf.keys[i], keyMax);
					return false;
				}
			}
		}
	}else{
		for(int i = 0; i <= node->raw.num_keys; i++){

			if(i < node->raw.num_keys){
				if(keyMin != 0){
					if(node->raw.as.branch.keys[i] < keyMin){
						PAFFS_DBG(PAFFS_TRACE_BUG, "Node n° %u's keys are inconsistent!\n"
								"\twas: %u, but parent stated keys would be over or equal %u!",
								getIndexFromPointer(node), node->raw.as.branch.keys[i], keyMin);
						return false;
					}
				}
				if(keyMax != 0){
					if(node->raw.as.branch.keys[i] >= keyMax){
						PAFFS_DBG(PAFFS_TRACE_BUG, "Node n° %u's keys are inconsistent!\n"
								"\twas: %u, but parent stated keys would be under %u!",
								getIndexFromPointer(node), node->raw.as.branch.keys[i], keyMax);
						return false;
					}
				}


				if(!first && node->raw.as.branch.keys[i] < last){
					PAFFS_DBG(PAFFS_TRACE_BUG, "Node n° %" PRIu16 " is not sorted (prev: %" PRIu32 ", curr: %" PRIu32 ")!", getIndexFromPointer(node), node->raw.as.leaf.keys[i], last);
					return false;
				}
				last = node->raw.as.branch.keys[i];
				first = false;
			}

			if(node->pointers[i] != NULL){
				if(node->pointers[i]->parent != node){
					PAFFS_DBG(PAFFS_TRACE_BUG, "Node n° %d stated parent was %d, but is actually %d!"
						,getIndexFromPointer(node->pointers[i]), getIndexFromPointer(node->pointers[i]->parent), getIndexFromPointer(node));
					return false;
				}
				long keyMin_n = i == 0 ? 0 : node->raw.as.branch.keys[i-1];
				long keyMax_n = i >= node->raw.num_keys ? 0 : node->raw.as.branch.keys[i];
				if(!isSubTreeValid(node->pointers[i], cache_node_reachable, keyMin_n, keyMax_n))
					return false;
			}
		}
	}

	return true;
}


bool TreeCache::isTreeCacheValid(){
	//Just for debugging purposes
	uint8_t cache_node_reachable[(treeNodeCacheSize/8)+1];
	memset(cache_node_reachable, 0, (treeNodeCacheSize/8)+1);	//See c. 162


	if(!isIndexUsed(cache_root))
		return true;


	if(!isSubTreeValid(&cache[cache_root], cache_node_reachable, 0, 0))
		return false;

	bool valid = true;
	if(memcmp(cache_node_reachable,cache_usage, (treeNodeCacheSize/8)+1)){
		for(int i = 0; i <= treeNodeCacheSize/8; i++){
			for(int j = 0; j < 8; j++){
				if((cache_usage[i*8] & 1 << j % 8) < (cache_node_reachable[i*8] & 1 << j % 8)){
					PAFFS_DBG(PAFFS_TRACE_BUG, "Deleted Node n° %d still reachable!", i*8 + j);
					return false;
				}
				if((cache_usage[i*8] & 1 << j % 8) > (cache_node_reachable[i*8] & 1 << j % 8)){
					if(!cache[i*8+j].locked && !cache[i*8+j].inheritedLock){
						//it is allowed if we are moving a parent around
						bool parentLocked = false;
						TreeCacheNode* par = cache[i*8+j].parent;
						while(par != par->parent){
							if(par->locked){
								parentLocked = true;
								break;
							}
							par = par->parent;
						}
						if(!parentLocked){
							PAFFS_DBG(PAFFS_TRACE_BUG, "Cache contains unreachable node %d!", i*8 + j);
							valid = false;
						}
					}
				}
			}
		}
	}
	return valid;
}

/**
 * returns true if path contains dirty elements
 * traverses through all paths and marks them
 */
bool TreeCache::resolveDirtyPaths(TreeCacheNode* tcn){
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

void TreeCache::markParentPathDirty(TreeCacheNode* tcn){
	tcn->dirty = true;
	if(tcn->parent == tcn)
		return;
	if(isIndexUsed(getIndexFromPointer(tcn->parent))){
		PAFFS_DBG(PAFFS_TRACE_BUG, "Parent of %p is not in cache!", tcn);
		dev->lasterr = Result::bug;
		return;
	}
	return markParentPathDirty(tcn->parent);
}

void TreeCache::deleteFromParent(TreeCacheNode* tcn){
	if(tcn == NULL){
		PAFFS_DBG(PAFFS_TRACE_BUG, "Tried to delete NULL node from Parent!");
		dev->lasterr = Result::bug;
		return;
	}
	if(tcn->parent == NULL){
		PAFFS_DBG(PAFFS_TRACE_BUG, "Tried to delete node %d from NULL parent!", getIndexFromPointer(tcn));
		isTreeCacheValid();	//This hopefully prints more detailed information
		dev->lasterr = Result::bug;
		return;
	}
	TreeCacheNode* parent = tcn->parent;
	if(parent == tcn)
		return;
	if(!isIndexUsed(getIndexFromPointer(parent))){
		//PAFFS_DBG(PAFFS_TRACE_BUG, "Parent of %p is not in cache!", tcn);
		//dev->lasterr = Result::bug;	//This is not a bug since the parent could be freed before the sibling
		return;
	}
	for(unsigned int i = 0; i <= parent->raw.num_keys; i++){
		if(parent->pointers[i] == tcn){
			parent->pointers[i] = NULL;
			return;
		}
	}
}


bool TreeCache::hasNoSiblings(TreeCacheNode* tcn){
	if(tcn->raw.is_leaf)
		return true;
	for(int i = 0; i <= tcn->raw.num_keys; i++)
		if(tcn->pointers[i] != NULL)
			return false;
	return true;
}

void TreeCache::deletePathToRoot(TreeCacheNode* tcn){
	if(tcn->dirty)
		return;

	deleteFromParent(tcn);
	setIndexFree(getIndexFromPointer(tcn));
	if(tcn->parent != tcn && hasNoSiblings(tcn->parent))
		deletePathToRoot(tcn->parent);
}

/*
 * Just frees clean leaf nodes
 */
void TreeCache::cleanFreeLeafNodes(){

	//debug ---->
	uint16_t usedCache = 0;
	if(traceMask & PAFFS_TRACE_TREECACHE){
		usedCache = getCacheUsage();
	}
	//<---- debug


	resolveDirtyPaths(&cache[cache_root]);
	if(dev->lasterr != Result::ok)
		return;
	for(unsigned int i = 0; i < treeNodeCacheSize; i++){
		if(!isIndexUsed(getIndexFromPointer(&cache[i])))
			continue;
		if(!cache[i].dirty && !cache[i].locked && !cache[i].inheritedLock && cache[i].raw.is_leaf){
			deleteFromParent(&cache[i]);
			setIndexFree(i);
		}
	}

	//debug ---->
	if(traceMask & PAFFS_TRACE_TREECACHE){
		PAFFS_DBG_S(PAFFS_TRACE_TREECACHE, "CleanTreeCacheLeaves freed %d Leaves.", usedCache - getCacheUsage());
	}
	//<---- debug
}

/*
 * Frees clean nodes
 */
void TreeCache::cleanFreeNodes(){
	//TODO: This only removes one layer of clean nodes, should check whole path to root
	//debug ---->
	uint16_t usedCache = 0;
	if(traceMask & PAFFS_TRACE_TREECACHE){
		if(traceMask & PAFFS_TRACE_VERIFY_TC && !isTreeCacheValid()){
			dev->lasterr = Result::bug;
			return;
		}
		usedCache = getCacheUsage();
	}
	//<---- debug


	resolveDirtyPaths(&cache[cache_root]);
	for(unsigned int i = 0; i < treeNodeCacheSize; i++){
		if(!isIndexUsed(getIndexFromPointer(&cache[i])))
			continue;
		if(!cache[i].dirty && !cache[i].locked && !cache[i].inheritedLock && hasNoSiblings(&cache[i])){
			deleteFromParent(&cache[i]);
			setIndexFree(i);
		}
	}

	//debug ---->
	if(traceMask & PAFFS_TRACE_TREECACHE){
		PAFFS_DBG_S(PAFFS_TRACE_TREECACHE, "CleanTreeCache freed %d Nodes.", usedCache - getCacheUsage());
	}
	//<---- debug
}
/**
 * Takes the node->raw.self to update parents flash pointer
 */
Result TreeCache::updateFlashAddressInParent(TreeCacheNode* node){
	if(node->parent == node){
		//Rootnode
		return dev->superblock.registerRootnode(node->raw.self);
	}
	for(int i = 0; i <= node->parent->raw.num_keys; i++){
		if(node->parent->pointers[i] == node){
			node->parent->raw.as.branch.pointers[i] = node->raw.self;
			return Result::ok;
		}
	}
	PAFFS_DBG(PAFFS_TRACE_BUG, "Could not find Node %d in its parent (Node %d)!", getIndexFromPointer(node), getIndexFromPointer(node->parent));
	return Result::nf;
}

//Only valid if resolveDirtyPaths has been called before
Result TreeCache::commitNodesRecursively(TreeCacheNode* node) {
	if(!node->dirty)
		return Result::ok;
	Result r;
	if(node->raw.is_leaf){
		r = dev->dataIO.writeTreeNode(&node->raw);
		if(r != Result::ok){
			PAFFS_DBG(PAFFS_TRACE_ERROR, "Could not write cached Treenode leaf!");
			return r;
		}
		node->dirty = false;
		return updateFlashAddressInParent(node);
	}

	for(int i = 0; i <= node->raw.num_keys; i++){
		if(node->pointers[i] == NULL)
			continue;
		r = commitNodesRecursively(node->pointers[i]);
		if(r != Result::ok){
			PAFFS_DBG(PAFFS_TRACE_ERROR, "Could not write Node %d's child n° %d (%d) to flash!",getIndexFromPointer(node), i, getIndexFromPointer(node->pointers[i]));
			return r;
		}
	}

	r = dev->dataIO.writeTreeNode(&node->raw);
	if(r != Result::ok){
		PAFFS_DBG(PAFFS_TRACE_ERROR, "Could not write cached Treenode branch!");
		return r;
	}
	node->dirty = false;
	return updateFlashAddressInParent(node);
}

/**
 * Commits complete Tree to Flash
 */

Result TreeCache::commitCache(){

	if(traceMask & PAFFS_TRACE_VERIFY_TC && !isTreeCacheValid()){
		return Result::bug;
	}

	//debug ---->
	uint16_t usedCache = 0;
	if(traceMask & PAFFS_TRACE_TREECACHE){
		usedCache = getCacheUsage();
		printTreeCache();
	}
	//<---- debug

	dev->lasterr = Result::ok;
	resolveDirtyPaths(&cache[cache_root]);
	if(dev->lasterr != Result::ok)
		return dev->lasterr;
	Result r = commitNodesRecursively(&cache[cache_root]);
	if(r != Result::ok){
		PAFFS_DBG(PAFFS_TRACE_ERROR, "Could not write Node to flash! (%s)", err_msg(r));
		return r;
	}


	cleanFreeLeafNodes();
	if(dev->lasterr != Result::ok)
		return dev->lasterr;

	if(findFirstFreeIndex() < 0)
		cleanFreeNodes();	//if tree cache did not contain any leaves (unlikely)

	//debug ---->
	if(traceMask & PAFFS_TRACE_TREECACHE){
		PAFFS_DBG_S(PAFFS_TRACE_TREECACHE, "flushTreeCache freed %d Nodes.", usedCache - getCacheUsage());
	}
	//<---- debug

	return Result::ok;
}

/**
 * This locks specified treeCache node and its path from Rootnode
 * To prevent Cache GC from deleting it
 */
Result TreeCache::TreeCache::lockTreeCacheNode(TreeCacheNode* tcn){
	tcn->locked = true;
	if(tcn->parent == NULL)
		return Result::ok;

	TreeCacheNode* curr = tcn->parent;
	while(curr->parent != curr){
		curr->inheritedLock = true;
		curr = curr->parent;
	}
	curr->inheritedLock = true;

	return Result::ok;
}

bool TreeCache::hasLockedChilds(TreeCacheNode* tcn){
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

Result TreeCache::unlockTreeCacheNode(TreeCacheNode* tcn){
	if(!tcn->locked){
		PAFFS_DBG(PAFFS_TRACE_ERROR, "Tried to double-unlock node n° %d!", getIndexFromPointer(tcn));
	}
	tcn->locked = false;

	if(tcn->parent == NULL){
		PAFFS_DBG(PAFFS_TRACE_ERROR, "Node %d with invalid parent !", getIndexFromPointer(tcn));
		return Result::fail;
	}

	TreeCacheNode* curr = tcn->parent;
	TreeCacheNode *old = NULL;
	do{
		if(hasLockedChilds(curr))
			break;

		curr->inheritedLock = false;

		if(curr->locked)
			break;

		if(tcn->parent == NULL){
			PAFFS_DBG(PAFFS_TRACE_ERROR, "Node %d with invalid parent !", getIndexFromPointer(tcn));
			return Result::fail;
		}
		old = curr;
		curr = curr->parent;

	}while(old != curr);

	return Result::ok;
}


Result TreeCache::getRootNodeFromCache(TreeCacheNode** tcn){
	if(isIndexUsed(cache_root)){
		*tcn = &cache[cache_root];
		cache_hits++;
		return Result::ok;
	}
	cache_misses++;

	PAFFS_DBG_S(PAFFS_TRACE_TREECACHE, "Load rootnode from Flash");

	Addr addr = dev->superblock.getRootnodeAddr();
	if(addr == 0)
		PAFFS_DBG(PAFFS_TRACE_TREE, "get Rootnode, but does not exist!");

	TreeCacheNode* new_root;
	Result r = tryAddNewCacheNode(&new_root);
	if(r != Result::ok){
		PAFFS_DBG(PAFFS_TRACE_ERROR, "Rootnode can't be loaded, cache size (%d) too small!", treeNodeCacheSize);
		return r;
	}
	new_root->parent = new_root;
	r = setRoot(new_root);
	if(r != Result::ok)
		return r;

	*tcn = new_root;

	return dev->dataIO.readTreeNode(addr, &cache[cache_root].raw);
}

/**
 * Possible cache flush. Tree could be empty except for path to child! (and parent, of course)
 */
Result TreeCache::getTreeNodeAtIndexFrom(uint16_t index,
									TreeCacheNode* parent, TreeCacheNode** child){
	if(index > branchOrder){	//FIXME index is smaller than branchOder?
		PAFFS_DBG(PAFFS_TRACE_BUG, "Tried to access index greater than branch size!");
		return Result::bug;
	}

	TreeCacheNode *target = parent->pointers[index];
	//To make sure parent and child can point to the same address, target is used as tmp buffer

	if(parent->raw.is_leaf){
		PAFFS_DBG(PAFFS_TRACE_BUG, "Tried to get Node from leaf!");
		return Result::bug;
	}
	if(target != NULL){
		*child = target;
		cache_hits++;
		PAFFS_DBG_S(PAFFS_TRACE_TREECACHE, "Cache hit, found target %p (position %ld)", target, target - cache);
		return Result::ok;	//cache hit
	}

	//--------------

	if(getIndexFromPointer(parent) == 0 && dev->lasterr != Result::ok){
		PAFFS_DBG(PAFFS_TRACE_BUG, "Tried to get child from Treenode not located in cache!");
		dev->lasterr = Result::ok;
		return Result::einval;
	}

	if(parent->raw.as.branch.pointers[index] == 0){
		PAFFS_DBG(PAFFS_TRACE_ERROR, "Tried to get treenode neither located in cache nor in flash!");
		return Result::einval;
	}

	cache_misses++;
	PAFFS_DBG_S(PAFFS_TRACE_TREECACHE, "Cache Miss");

	lockTreeCacheNode(parent);
	Result r = addNewCacheNode(&target);
	if(r != Result::ok){
		unlockTreeCacheNode(parent);
		return r;
	}
	unlockTreeCacheNode(parent);


	target->parent = parent;
	parent->pointers[index] = target;
	*child = target;

	Result r2 = dev->dataIO.readTreeNode(parent->raw.as.branch.pointers[index], &(*child)->raw);
	if(r2 != Result::ok)
		return r2;
	return r;
}

Result TreeCache::removeNode(TreeCacheNode* tcn){
	setIndexFree(getIndexFromPointer(tcn));
	if(tcn->raw.self != 0) {
		return dev->dataIO.deleteTreeNode(&tcn->raw);
	}

	return Result::ok;
}

Result TreeCache::setRoot(TreeCacheNode* rootTcn){
	if(rootTcn->parent != rootTcn){
		PAFFS_DBG(PAFFS_TRACE_BUG, "BUG: setCacheRoot with root->parent not pointing to itself");
		return Result::bug;
	}
	cache_root = getIndexFromPointer(rootTcn);
	return Result::ok;
}


//debug
uint16_t TreeCache::getCacheUsage(){
	uint16_t usage = 0;
	for(unsigned int i = 0; i < treeNodeCacheSize; i++){
		if(isIndexUsed(i))
			usage++;
	}
	return usage;
}

uint16_t TreeCache::getCacheSize(){
	return treeNodeCacheSize;
}

uint16_t TreeCache::getCacheHits(){
	return cache_hits;
}
uint16_t TreeCache::getCacheMisses(){
	return cache_misses;
}

void TreeCache::printSubtree(int layer, TreeCacheNode* node){
	for(int i = 0; i < layer; i++){
		printf(".");
	}
	printf("[ID: %d PAR: %d %s%s%s|", getIndexFromPointer(node), getIndexFromPointer(node->parent),
			node->dirty ? "d" : "-", node->locked ? "l" : "-", node->inheritedLock ? "i" : "-");
	if(node->raw.is_leaf){
		for(int i = 0; i < node->raw.num_keys; i++){
			if(i > 0)
				printf(",");
			printf (" %" PRIu32, node->raw.as.leaf.keys[i]);
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
				printf("/%" PRIu32 "\\", node->raw.as.branch.keys[i-1]);
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
void TreeCache::printTreeCache(){
	printf("----------------\n");
	if(isIndexUsed(cache_root)){
		printSubtree(0, &cache[cache_root]);
	}else
		printf("Empty treeCache.\n");
	printf("----------------\n");
}

}
