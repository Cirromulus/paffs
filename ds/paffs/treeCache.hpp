/*
 * treeCache.h
 *
 *  Created on: 21.09.2016
 *      Author: Pascal Pieper
 */

#pragma once
#include "commonTypes.hpp"
#include "treeTypes.hpp"

namespace paffs{

class TreeCache{

	Device *dev;

	int16_t cache_root = 0;

	TreeCacheNode cache[treeNodeCacheSize];

	uint8_t cache_usage[(treeNodeCacheSize/8)+1];

	//Just for debug/tuning purposes
	uint16_t cache_hits = 0;
	uint16_t cache_misses = 0;

public:

	TreeCache(Device *mdev) : dev(mdev){};

	/**
	 * Commits complete Tree to Flash
	 */
	Result commitCache();

	//--------------------------------------

	/**
	 * Clears all internal Memory Structures
	 */
	void clear();

	/**
	 * This locks specified treeCache node and its path from Rootnode
	 * To prevent Cache GC from deleting it
	 */
	Result lockTreeCacheNode(TreeCacheNode* tcn);

	Result unlockTreeCacheNode(TreeCacheNode* tcn);

	/**
	 * Possible cache flush. Although rootnode should be in cache any time.
	 */
	Result getRootNodeFromCache(TreeCacheNode** tcn);
	/**
	 * Possible cache flush.
	 */
	Result getTreeNodeAtIndexFrom(uint16_t index,
										TreeCacheNode* parent, TreeCacheNode** child);

	Result removeNode(TreeCacheNode* tcn);

	Result setRoot(TreeCacheNode* rootTcn);

	Result addNewCacheNode(TreeCacheNode** newTcn);

	/**
	 * Consistency checkers for Treecache
	 */
	bool isSubTreeValid(TreeCacheNode* node, uint8_t* cache_node_reachable, InodeNo keyMin, InodeNo keyMax);
	bool isTreeCacheValid();



	//debug
	uint16_t getCacheUsage();
	uint16_t getCacheSize();
	uint16_t getCacheHits();
	uint16_t getCacheMisses();
	void printTreeCache();

private:
	/**
	 * returns true if path contains dirty elements
	 * traverses through all paths and marks them
	 */
	bool resolveDirtyPaths(TreeCacheNode* tcn);
	void markParentPathDirty(TreeCacheNode* tcn);
	void deleteFromParent(TreeCacheNode* tcn);
	bool hasNoSiblings(TreeCacheNode* tcn);
	void deletePathToRoot(TreeCacheNode* tcn);
	Result tryAddNewCacheNode(TreeCacheNode** newTcn);
	Result commitNodesRecursively(TreeCacheNode* node);
	void setIndexUsed(uint16_t index);
	void setIndexFree(uint16_t index);
	bool isIndexUsed(uint16_t index);
	void printSubtree(int layer, TreeCacheNode* node);
	int16_t findFirstFreeIndex();
	int16_t getIndexFromPointer(TreeCacheNode* tcn);
	bool hasLockedChilds(TreeCacheNode* tcn);
	bool isParentPathClean(TreeCacheNode* tcn);
	Result updateFlashAddressInParent(TreeCacheNode* node);
	/**
	 * returns true if any sibling is dirty
	 * stops at first occurrence.
	 */
	bool areSiblingsClean(TreeCacheNode* tcn);
	/*
	 * Just frees clean nodes
	 */
	void cleanFreeNodes();
	/*
	 * Just frees clean leaf nodes, so cache still contains branches...
	 */
	void cleanFreeLeafNodes();

};


}
