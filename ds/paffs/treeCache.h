/*
 * treeCache.h
 *
 *  Created on: 21.09.2016
 *      Author: rooot
 */

#ifndef TREECACHE_H_
#define TREECACHE_H_

#include "paffs.h"
#include "btree.h"



PAFFS_RESULT getRootNodeFromCache(p_dev* dev, treeCacheNode** tcn);

PAFFS_RESULT getTreeNodeAtIndexFrom(p_dev* dev, unsigned char index,
									treeCacheNode* parent, treeCacheNode** child);

PAFFS_RESULT removeCacheNode(p_dev* dev, treeCacheNode* tcn);

PAFFS_RESULT setCacheRoot(p_dev* dev, treeCacheNode* rootTcn);

PAFFS_RESULT flushTreeCache(p_dev* dev);


// "private"

int16_t findFirstFreeIndex();

int16_t getIndexFromPointer(treeCacheNode* tcn);
/*
 * The new tcn->parent has to be changed _before_ calling another addNewCacheNode!
 */
PAFFS_RESULT addNewCacheNode(treeCacheNode** newTcn);

PAFFS_RESULT addNewCacheNodeWithPossibleFlush(p_dev* dev, treeCacheNode** newTcn);

bool isParentPathClean(treeCacheNode* tcn);

/**
 * returns true if any sibling is dirty
 * stops at first occurrence.
 */
bool areSiblingsClean(treeCacheNode* tcn);

/**
 * returns true if path contains dirty elements
 * traverses through all paths and marks them
 */
bool resolveDirtyPaths(treeCacheNode* tcn);

void markParentPathDirty(treeCacheNode* tcn);

void deleteFromParent(treeCacheNode* tcn);


//debug
uint16_t getCacheUsage();
uint16_t getCacheSize();

#endif /* TREECACHE_H_ */
