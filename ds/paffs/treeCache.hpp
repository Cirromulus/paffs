/*
 * treeCache.h
 *
 *  Created on: 21.09.2016
 *      Author: rooot
 */

#pragma once
#include "btree.hpp"
#include "paffs.hpp"

namespace paffs{
// "private"

int16_t findFirstFreeIndex();

int16_t getIndexFromPointer(TreeCacheNode* tcn);

Result addNewCacheNode(TreeCacheNode** newTcn);

bool isParentPathClean(TreeCacheNode* tcn);

/**
 * returns true if any sibling is dirty
 * stops at first occurrence.
 */
bool areSiblingsClean(TreeCacheNode* tcn);


/**
 * Consistency checkers for Treecache
 */
bool isSubTreeValid(TreeCacheNode* node, uint8_t* cache_node_reachable, long keyMin, long keyMax);
bool isTreeCacheValid();

/**
 * returns true if path contains dirty elements
 * traverses through all paths and marks them
 */
bool resolveDirtyPaths(TreeCacheNode* tcn);

void markParentPathDirty(TreeCacheNode* tcn);

void deleteFromParent(TreeCacheNode* tcn);

/**
 * Builds up cache with Elements in the Path to tcn.
 */
//Result buildUpCacheToNode(Dev* dev, TreeCacheNode* localCopyOfNode, TreeCacheNode** cachedOutputNode);

/*
 * Just frees clean nodes
 */
void cleanTreeCache();
/*
 * Just frees clean leaf nodes, so cache still contains branches...
 */
void cleanTreeCacheLeaves();

/**
 * Commits complete Tree to Flash
 */
Result commitTreeCache(Dev* dev);

//--------------------------------------

/**
 * Clears all internal Memory Structures
 */
void initCache();

/**
 * This locks specified treeCache node and its path from Rootnode
 * To prevent Cache GC from deleting it
 */
Result lockTreeCacheNode(Dev* dev, TreeCacheNode* tcn);

Result unlockTreeCacheNode(Dev* dev, TreeCacheNode* tcn);

/**
 * Possible cache flush. Although rootnode should be at any case in cache.
 */
Result getRootNodeFromCache(Dev* dev, TreeCacheNode** tcn);
/**
 * Possible cache flush. Tree could be empty except for path to child! (and parent, of course)
 */
Result getTreeNodeAtIndexFrom(Dev* dev, unsigned char index,
									TreeCacheNode* parent, TreeCacheNode** child);

Result removeCacheNode(Dev* dev, TreeCacheNode* tcn);

Result setCacheRoot(Dev* dev, TreeCacheNode* rootTcn);

Result addNewCacheNodeWithPossibleFlush(Dev* dev, TreeCacheNode** newTcn);


//debug
uint16_t getCacheUsage();
uint16_t getCacheSize();
uint16_t getCacheHits();
uint16_t getCacheMisses();
void deleteTreeCache();
void printTreeCache();

}
