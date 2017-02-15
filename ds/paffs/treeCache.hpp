/*
 * treeCache.h
 *
 *  Created on: 21.09.2016
 *      Author: Pascal Pieper
 */

#pragma once
#include "btree.hpp"
#include "paffs.hpp"

namespace paffs{

class TreeCache{

	Device *dev;

	int16_t cache_root = 0;

	TreeCacheNode cache[treeNodeCacheSize];

	uint8_t cache_usage[(treeNodeCacheSize/8)+1];

	//Just for debug/tuning purposes
	static uint16_t cache_hits = 0;
	static uint16_t cache_misses = 0;

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
	bool isSubTreeValid(TreeCacheNode* node, uint8_t* cache_node_reachable, InodeNo keyMin, InodeNo keyMax);
	bool isTreeCacheValid();

	/**
	 * returns true if path contains dirty elements
	 * traverses through all paths and marks them
	 */
	bool resolveDirtyPaths(TreeCacheNode* tcn);

	void markParentPathDirty(TreeCacheNode* tcn);

	void deleteFromParent(TreeCacheNode* tcn);

public:

	TreeCache(Device *dev) : dev(dev){};

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
	Result commitTreeCache();

	//--------------------------------------

	/**
	 * Clears all internal Memory Structures
	 */
	void initCache();

	/**
	 * This locks specified treeCache node and its path from Rootnode
	 * To prevent Cache GC from deleting it
	 */
	Result lockTreeCacheNode(TreeCacheNode* tcn);

	Result unlockTreeCacheNode(TreeCacheNode* tcn);

	/**
	 * Possible cache flush. Although rootnode should be at any case in cache.
	 */
	Result getRootNodeFromCache(TreeCacheNode** tcn);
	/**
	 * Possible cache flush. Tree could be empty except for path to child! (and parent, of course)
	 */
	Result getTreeNodeAtIndexFrom(unsigned char index,
										TreeCacheNode* parent, TreeCacheNode** child);

	Result removeCacheNode(TreeCacheNode* tcn);

	Result setCacheRoot(TreeCacheNode* rootTcn);

	Result addNewCacheNodeWithPossibleFlush(TreeCacheNode** newTcn);


	//debug
	uint16_t getCacheUsage();
	uint16_t getCacheSize();
	uint16_t getCacheHits();
	uint16_t getCacheMisses();
	void deleteTreeCache();
	void printTreeCache();


};


}
