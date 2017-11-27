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
    processEntry(JournalEntry& entry) override;

private:
    void
    printLeaves(TreeCacheNode& c);
    void
    printQueuedKeysRecursively(queue_s* q);
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
    int
    findRange(TreeCacheNode& root,
              InodeNo key_start,
              InodeNo key_end,
              int returned_keys[],
              void* returned_pointers[]);
    Result
    findBranch(TreeCacheNode& target, TreeCacheNode*& outtreeCacheNode);
    Result
    findLeaf(InodeNo key, TreeCacheNode*& outtreeCacheNode);
    Result
    findInLeaf(TreeCacheNode& leaf, InodeNo key, Inode& outInode);
    Result
    find(InodeNo key, Inode& outInode);
    int
    cut(int length);

    // Insertion.

    int
    getLeftIndex(TreeCacheNode& parent, TreeCacheNode& left);
    Result
    insertIntoLeaf(TreeCacheNode& leaf, const Inode& pointer);
    Result
    insertIntoLeafAfterSplitting(TreeCacheNode& leaf, const Inode& newInode);
    Result
    insertIntoNode(TreeCacheNode& newNode, int left_index, InodeNo key, TreeCacheNode& right);
    Result
    insertIntoNodeAfterSplitting(TreeCacheNode& old_node,
                                 unsigned int left_index,
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
                   int neighbor_index,
                   unsigned int k_prime);
    Result
    redistributeNodes(TreeCacheNode& n,
                      TreeCacheNode& neighbor,
                      int neighbor_index,
                      unsigned int k_prime_index,
                      unsigned int k_prime);
    Result
    deleteEntry(TreeCacheNode& n, InodeNo key);
    Result
    removeEntryFromNode(TreeCacheNode& n, InodeNo key);
};
}
#endif
