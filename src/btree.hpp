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

#ifndef __PAFFS_BTREE_H__
#define __PAFFS_BTREE_H__

#include <stdbool.h>
#include <stddef.h>

#include "commonTypes.hpp"
#include "journalTopic.hpp"
#include "paffs_trace.hpp"
#include "treeCache.hpp"
#include "treeTypes.hpp"
#include "treequeue.hpp"  //Just for printing debug info in tree

namespace paffs
{
class Btree : public JournalTopic
{
    Device* dev;

public:
    TreeCache mCache;
    Btree(Device* mdev) : dev(mdev), mCache(TreeCache(mdev)){};

    Result
    insertInode(const Inode& inode);
    Result
    getInode(InodeNo number, Inode& outInode);
    Result
    updateExistingInode(const Inode& inode);
    Result
    deleteInode(InodeNo number);
    Result
    findFirstFreeNo(InodeNo* outNumber);

    uint16_t
    calculateMaxNeededNewNodesForInsertion(const TreeCacheNode& insertTarget);

    void
    printTree();
    Result
    startNewTree();
    Result
    commitCache();
    void
    wipeCache();

    JournalEntry::Topic
    getTopic() override;
    void
    resetState() override;
    bool
    isInterestedIn(const journalEntry::Max& entry) override;
    Result
    processEntry(const journalEntry::Max& entry, JournalEntryPosition position) override;
    void
    signalEndOfLog() override;

private:
    void
    printLeaves(TreeCacheNode& c);
    void
    printQueuedKeysRecursively(TreeQueue* q);
    void
    printKeys(TreeCacheNode& c);

    bool
    isTreeCacheNodeEqual(TreeCacheNode& left, TreeCacheNode& right);

    // bool isEqual(TreeCacheNode &left, TreeCacheNode &right);
    int
    height(TreeCacheNode& root);
    // Length is number of Kanten, not Knoten
    uint16_t
    lengthToRoot(TreeCacheNode& child);
    // Path is root first, child last
    Result
    pathFromRoot(TreeCacheNode& child, Addr* path, unsigned int& lengthOut);
    Result
    findBranch(TreeCacheNode& target, TreeCacheNode*& outtreeCacheNode);
    Result
    findLeaf(InodeNo key, TreeCacheNode*& outtreeCacheNode);
    Result
    findInLeaf(TreeCacheNode& leaf, InodeNo key, Inode& outInode);
    Result
    find(InodeNo key, Inode& outInode);
    uint16_t
    cut(uint16_t length);

    // Insertion.

    uint16_t
    getLeftIndex(TreeCacheNode& parent, TreeCacheNode& left);
    Result
    insertIntoLeaf(TreeCacheNode& leaf, const Inode& pointer);
    Result
    insertIntoLeafAfterSplitting(TreeCacheNode& leaf, const Inode& newInode);
    Result
    insertIntoNode(TreeCacheNode& newNode, uint16_t leftIndex, InodeNo key, TreeCacheNode& right);
    Result
    insertIntoNodeAfterSplitting(TreeCacheNode& old_node,
                                 uint16_t leftIndex,
                                 InodeNo key,
                                 TreeCacheNode& right);
    Result
    insertIntoParent(TreeCacheNode& left, InodeNo key, TreeCacheNode& right);
    Result
    insertIntoNewRoot(TreeCacheNode& left, InodeNo key, TreeCacheNode& right);

    // Deletion.

    int
    getNeighborIndex(TreeCacheNode& n);
    Result
    adjustRoot(TreeCacheNode& root);
    Result
    coalesceNodes(TreeCacheNode& n,
                   TreeCacheNode& neighbor,
                   int16_t  neighborIndex,
                   uint16_t kPrime);
    Result
    redistributeNodes(TreeCacheNode& n,
                      TreeCacheNode& neighbor,
                      int16_t  neighborIndex,
                      uint16_t kPrimeIndex,
                      uint16_t kPrime);
    Result
    deleteEntry(TreeCacheNode& n, InodeNo key);
    Result
    removeEntryFromNode(TreeCacheNode& n, InodeNo key);
};
}
#endif
