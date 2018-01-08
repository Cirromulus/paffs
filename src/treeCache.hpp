/*
 * Copyright (c) 2016-2017, German Aerospace Center (DLR)
 *
 * This file is part of the development version of OUTPOST.
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 *
 * Authors:
 * - 2016-2017, Pascal Pieper (DLR RY-AVS)
 */
// ----------------------------------------------------------------------------

#pragma once
#include "bitlist.hpp"
#include "commonTypes.hpp"
#include "treeTypes.hpp"
#include "journalEntry.hpp"

namespace paffs
{
class TreeCache
{
    Device* dev;

    int16_t mCacheRoot = 0;

    TreeCacheNode mCache[treeNodeCacheSize];

    BitList<treeNodeCacheSize> mCacheUsage;

    Addr newPageList[treeNodeCacheSize];
    uint16_t newPageListHWM = 0;
    Addr oldPageList[treeNodeCacheSize];
    uint16_t oldPageListHWM = 0;
    enum class JournalState : uint8_t
    {
        ok,
        invalid,
        recover,
    } journalState = JournalState::ok;

    // Just for debug/tuning purposes
    uint16_t mCacheHits = 0;
    uint16_t mCacheMisses = 0;

public:
    TreeCache(Device* mdev) : dev(mdev){};

    /**
     * Commits complete Tree to Flash
     */
    Result
    commitCache();

    /**
     * \warn May flush nodes, so lock used nodes
     */
    Result
    reserveNodes(uint16_t neededNodes);
    //--------------------------------------

    /**
     * Clears all internal Memory Structures
     */
    void
    clear();

    /**
     * This locks specified treeCache node and its path to Rootnode
     * To prevent Cache GC from deleting it
     */
    Result
    lockTreeCacheNode(TreeCacheNode& tcn);

    Result
    unlockTreeCacheNode(TreeCacheNode& tcn);

    /**
     * Possible cache flush. Although rootnode should be in cache any time.
     */
    Result
    getRootNodeFromCache(TreeCacheNode*& tcn);
    /**
     * Possible cache flush.
     */
    Result
    getTreeNodeAtIndexFrom(uint16_t index, TreeCacheNode& parent, TreeCacheNode*& child);

    Result
    removeNode(TreeCacheNode& tcn);

    Result
    setRoot(TreeCacheNode& rootTcn);

    Result
    addNewCacheNode(TreeCacheNode*& newTcn);

    /**
     * \return fail if not determinable
     */
    Result
    tryGetHeight(uint16_t &heightOut);

    /**
     * Consistency checkers for Treecache
     */
    bool
    isSubTreeValid(TreeCacheNode& node,
                   BitList<treeNodeCacheSize>& reachable,
                   InodeNo keyMin,
                   InodeNo keyMax);
    bool
    isTreeCacheValid();

    Result
    processEntry(const journalEntry::btree::Commit& commit);
    void
    signalEndOfLog();

    // debug
    uint16_t
    getCacheUsage();
    uint16_t
    getCacheSize();
    uint16_t
    getCacheHits();
    uint16_t
    getCacheMisses();
    void
    printTreeCache();
    uint16_t
    getIndexFromPointer(TreeCacheNode& tcn);
private:

    Result
    freeNodes(uint16_t neededCleanNodes = treeNodeCacheSize);
    /**
     * returns true if path contains dirty elements
     * traverses through all paths and marks them
     */
    bool
    resolveDirtyPaths(TreeCacheNode& tcn);
    void
    markParentPathDirty(TreeCacheNode& tcn);
    void
    deleteFromParent(TreeCacheNode& tcn);
    bool
    hasNoChilds(TreeCacheNode& tcn);
    //! \return number of deleted Nodes
    uint16_t
    deletePathToRoot(TreeCacheNode& tcn);
    Result
    tryAddNewCacheNode(TreeCacheNode*& newTcn);
    /**
     * \warn Only valid if resolveDirtyPaths has been called before
     */
    Result
    commitNodesRecursively(TreeCacheNode& node);
    void
    setIndexUsed(uint16_t index);
    void
    setIndexFree(uint16_t index);
    bool
    isIndexUsed(uint16_t index);
    void
    printNode(TreeCacheNode& node);
    void
    printSubtree(int layer, BitList<treeNodeCacheSize>& reached, TreeCacheNode& node);
    int16_t
    findFirstFreeIndex();
    bool
    hasLockedChilds(TreeCacheNode& tcn);
    bool
    isParentPathClean(TreeCacheNode& tcn);
    Result
    updateFlashAddressInParent(TreeCacheNode& node);
    Result
    markPageUsed(Addr addr);
    Result
    markPageOld(Addr addr);
    Result
    invalidateOldPages();
    /**
     * returns true if any sibling is dirty
     * stops at first occurrence.
     */
    bool
    areSiblingsClean(TreeCacheNode& tcn);
    /*
     * \brief Just frees clean leaf nodes, so cache still contains branches...
     * \param neededCleanNodes will be decreased if some nodes were cleaned
     */
    Result
    cleanFreeLeafNodes(uint16_t& neededCleanNodes);

    /*
     * \brief Frees clean nodes
     * \param neededCleanNodes will be decreased if some nodes were cleaned
     */
    Result
    cleanFreeNodes(uint16_t& neededCleanNodes);

    Result
    writeTreeNode(TreeCacheNode& node);
    Result
    readTreeNode(Addr addr, TreeNode& node);
    Result
    deleteTreeNode(TreeNode& node);
};
}
