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

#include "btree.hpp"
#include "dataIO.hpp"
#include "device.hpp"
#include "treeCache.hpp"
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

namespace paffs
{
Result
Btree::insertInode(const Inode& inode)
{
    TreeCacheNode* node = nullptr;
    Result r;

    PAFFS_DBG_S(PAFFS_TRACE_TREE, "Insert Inode n° %" PRIinodeno, inode.no);

    r = findLeaf(inode.no, node);
    if (r != Result::ok)
    {
        PAFFS_DBG(PAFFS_TRACE_ERROR, "Could not find leaf");
        return r;
    }

    if (node->raw.keys > 0)
    {
        for (unsigned i = 0; i < leafOrder; i++)
        {
            if (node->raw.as.branch.keys[i] == inode.no)
            {
                PAFFS_DBG(PAFFS_TRACE_BUG, "BUG: Inode already existing with n° %" PRIinodeno "", inode.no);
                return Result::bug;
            }
        }
    }

    mCache.lockTreeCacheNode(*node);  // prevents own node from clear

    // This prevents the cache from a commit inside invalid state
    r = mCache.reserveNodes(calculateMaxNeededNewNodesForInsertion(*node));
    if (r != Result::ok)
    {
        PAFFS_DBG_S(PAFFS_TRACE_ERROR, "Could not reserve enough nodes in treecache!");
        return r;
    }

    mCache.unlockTreeCacheNode(*node);

    dev->journal.addEvent(journalEntry::btree::Insert(inode));

    /* Case: leaf has room for key and pointer.
     */
    if (node->raw.keys < leafOrder)
    {
        return insertIntoLeaf(*node, inode);
    }

    /* Case:  leaf must be split.
     */
    return insertIntoLeafAfterSplitting(*node, inode);
}

Result
Btree::getInode(InodeNo number, Inode& outInode)
{
    return find(number, outInode);
}

Result
Btree::updateExistingInode(const Inode& inode)
{
    PAFFS_DBG_S(PAFFS_TRACE_TREE, "Update existing inode n° %" PRIinodeno "", inode.no);
    TreeCacheNode* node = nullptr;
    Result r = findLeaf(inode.no, node);
    if (r != Result::ok)
        return r;

    int pos;
    for (pos = 0; pos < node->raw.keys; pos++)
    {
        if (node->raw.as.leaf.keys[pos] == inode.no)
            break;
    }

    if (pos == node->raw.keys)
    {
        PAFFS_DBG(PAFFS_TRACE_BUG,
                  "Tried to update existing Inode %" PRIinodeno ","
                  "but could not find it!",
                  inode.no);
        return Result::bug;  // This Key did not exist
    }

    dev->journal.addEvent(journalEntry::btree::Update(inode));

    node->raw.as.leaf.pInodes[pos] = inode;
    node->dirty = true;

    // todo: check cache memory consumption, possibly flush
    return Result::ok;
}

Result
Btree::deleteInode(InodeNo number)
{
    Inode key;
    TreeCacheNode* key_leaf;

    Result r = findLeaf(number, key_leaf);
    if (r != Result::ok)
        return r;
    r = findInLeaf(*key_leaf, number, key);
    if (r != Result::ok)
        return r;

    dev->journal.addEvent(journalEntry::btree::Remove(number));
    return deleteEntry(*key_leaf, number);
}

Result
Btree::findFirstFreeNo(InodeNo* outNumber)
{
    TreeCacheNode* c = nullptr;
    *outNumber = 0;
    Result r = mCache.getRootNodeFromCache(c);
    if (r != Result::ok)
        return r;
    while (!c->raw.isLeaf)
    {
        r = mCache.getTreeNodeAtIndexFrom(c->raw.keys, *c, c);
        if (r != Result::ok)
            return r;
    }
    if (c->raw.keys > 0)
    {
        *outNumber = c->raw.as.leaf.pInodes[c->raw.keys - 1].no + 1;
    }
    return Result::ok;
}

uint16_t
Btree::calculateMaxNeededNewNodesForInsertion(const TreeCacheNode& insertTarget)
{

    if(insertTarget.raw.isLeaf)
    {   //we are a leaf
        if(insertTarget.raw.keys == leafOrder)
        {   //leaf is full, would split
            if(insertTarget.parent != &insertTarget)
            {   //we are a normal leaf
                return 1 + calculateMaxNeededNewNodesForInsertion(*insertTarget.parent);
            }else
            {   //Rootnode splits into brother and parent is new rootnode
                return 2;
            }
        }else
        {
            return 0;
        }
    }else
    {   //we are a branch
        if(insertTarget.raw.keys == branchOrder - 1)
        {
            if(insertTarget.parent != &insertTarget)
            {   //we are a normal branch
                return 1 + calculateMaxNeededNewNodesForInsertion(*insertTarget.parent);
            }else
            {   //Rootnode splits into brother and parent is new rootnode
                return 2;
            }
        }else
        {
            return 0;
        }
    }
}

Result
Btree::commitCache()
{
    return mCache.commitCache();
}

void
Btree::wipeCache()
{
    mCache.clear();
}

JournalEntry::Topic
Btree::getTopic()
{
    return JournalEntry::Topic::tree;
}

void
Btree::processEntry(JournalEntry& entry)
{
    if (entry.topic != getTopic())
    {
        PAFFS_DBG(PAFFS_TRACE_BUG, "Got wrong entry to process!");
        return;
    }
    const journalEntry::BTree* e = static_cast<const journalEntry::BTree*>(&entry);
    switch (e->op)
    {
    case journalEntry::BTree::Operation::insert:
        insertInode(static_cast<const journalEntry::btree::Insert*>(&entry)->inode);
        break;
    case journalEntry::BTree::Operation::update:
        updateExistingInode(static_cast<const journalEntry::btree::Update*>(&entry)->inode);
        break;
    case journalEntry::BTree::Operation::remove:
        deleteInode(static_cast<const journalEntry::btree::Remove*>(&entry)->no);
        break;
    }
}

/**
 * Compares
 */
bool
Btree::isTreeCacheNodeEqual(TreeCacheNode& left, TreeCacheNode& right)
{
    for (int i = 0; i <= left.raw.keys; i++)
        if (left.raw.as.branch.keys[i] != right.raw.as.branch.keys[i])
            return false;

    return true;
}

/* Utility function to give the height
 * of the tree, which length in number of edges
 * of the path from the root to any leaf.
 */
int
Btree::height(TreeCacheNode& root)
{
    uint16_t h = 0;
    if(mCache.tryGetHeight(h) == Result::ok)
    {
        return h;
    }
    //This calculation is very mCache unfriendly, so only use if no better possibility exists
    TreeCacheNode* curr = &root;
    while (!curr->raw.isLeaf)
    {
        Result r = mCache.getTreeNodeAtIndexFrom(0, *curr, curr);
        if (r != Result::ok)
        {
            dev->lasterr = r;
            return -1;
        }
        h++;
    }
    return h;
}

/* Utility function to give the length in edges
 * of the path from any TreeCacheNode to the root.
 */
uint16_t
Btree::lengthToRoot(TreeCacheNode& child)
{
    uint16_t length = 0;
    TreeCacheNode* node = &child;
    while (node->parent != node)
    {
        length++;
        node = node->parent;
    }
    return length;
}

/* Traces the path from the root to a branch, searching
 * by key.
 * Returns the branch containing the given key.
 * This function is used to build up mCache to a given leaf after a mCache clean.
 */
Result
Btree::findBranch(TreeCacheNode& target, TreeCacheNode*& outtreeCacheNode)
{
    int i = 0;
    TreeCacheNode* c = nullptr;

    Result r = mCache.getRootNodeFromCache(c);
    if (r != Result::ok)
        return r;

    while (!isTreeCacheNodeEqual(*c, target))
    {
        i = 0;
        while (i < c->raw.keys)
        {
            if (target.raw.as.branch.keys[0] >= c->raw.as.branch.keys[i])
                i++;
            else
                break;
        }

        // printf("%" PRId16 " ->\n", i);
        r = mCache.getTreeNodeAtIndexFrom(i, *c, c);
        if (r != Result::ok)
            return r;
    }

    outtreeCacheNode = c;
    return Result::ok;
}

/* Traces the path from the root to a leaf, searching
 * by key.
 * Returns the leaf containing the given key.
 */
Result
Btree::findLeaf(InodeNo key, TreeCacheNode*& outtreeCacheNode)
{
    int i = 0;
    TreeCacheNode* c = nullptr;

    Result r = mCache.getRootNodeFromCache(c);
    if (r != Result::ok)
        return r;

    unsigned int depth = 0;
    while (!c->raw.isLeaf)
    {
        depth++;
        if (depth >= treeNodeCacheSize - 1)
        {  //-1 because one node is needed for insert functions.
            PAFFS_DBG(PAFFS_TRACE_ERROR,
                      "Cache size (%" PRId16 ") too small for depth %" PRId16 "!",
                      treeNodeCacheSize,
                      depth);
            return Result::lowmem;
        }

        i = 0;
        while (i < c->raw.keys)
        {
            if (key >= c->raw.as.branch.keys[i])
                i++;
            else
                break;
        }

        r = mCache.getTreeNodeAtIndexFrom(i, *c, c);
        if (r != Result::ok)
            return r;
    }

    outtreeCacheNode = c;
    return Result::ok;
}

Result
Btree::findInLeaf(TreeCacheNode& leaf, InodeNo key, Inode& outInode)
{
    unsigned int i;
    for (i = 0; i < leaf.raw.keys; i++)
        if (leaf.raw.as.leaf.keys[i] == key)
            break;
    if (i == leaf.raw.keys)
        return Result::notFound;
    outInode = leaf.raw.as.leaf.pInodes[i];
    return Result::ok;
}

/* Finds and returns the Inode to which
 * a key refers.
 */
Result
Btree::find(InodeNo key, Inode& outInode)
{
    TreeCacheNode* c = nullptr;
    Result r = findLeaf(key, c);
    if (r != Result::ok)
        return r;
    return findInLeaf(*c, key, outInode);
}

/* Finds the appropriate place to
 * split a TreeCacheNode that is too big into two.
 */
int
Btree::cut(int length)
{
    if (length % 2 == 0)
        return length / 2;
    else
        return length / 2 + 1;
}

// INSERTION

/* Helper function used in insert_into_parent
 * to find the index of the parent's pointer to
 * the TreeCacheNode to the left of the key to be inserted.
 */
int
Btree::getLeftIndex(TreeCacheNode& parent, TreeCacheNode& left)
{
    int left_index = 0;
    while (left_index < parent.raw.keys)
    {
        if (parent.raw.as.branch.pointers[left_index] != 0)
            if (parent.raw.as.branch.pointers[left_index] == left.raw.self)
                break;
        if (parent.pointers[left_index] != 0)
            if (parent.pointers[left_index] == &left)
                break;
        left_index++;
    }
    return left_index;
}

/* Inserts a new pointer to a Inode and its corresponding
 * key into a leaf when it has enough space free.
 * (No further Tree-action Required)
 */
Result
Btree::insertIntoLeaf(TreeCacheNode& leaf, const Inode& newInode)
{
    int i, insertion_point;

    insertion_point = 0;
    while (insertion_point < leaf.raw.keys
           && leaf.raw.as.leaf.keys[insertion_point] < newInode.no)
        insertion_point++;

    for (i = leaf.raw.keys; i > insertion_point; i--)
    {
        leaf.raw.as.leaf.keys[i] = leaf.raw.as.leaf.keys[i - 1];
        leaf.raw.as.leaf.pInodes[i] = leaf.raw.as.leaf.pInodes[i - 1];
    }
    leaf.raw.as.leaf.keys[insertion_point] = newInode.no;
    leaf.raw.keys++;
    leaf.raw.as.leaf.pInodes[insertion_point] = newInode;

    leaf.dirty = true;

    return Result::ok;
}

/* Inserts a new key and pointer
 * to a new Inode into a leaf so as to exceed
 * the tree's order, causing the leaf to be split
 * in half.
 */
Result
Btree::insertIntoLeafAfterSplitting(TreeCacheNode& leaf, const Inode& newInode)
{
    PAFFS_DBG_S(PAFFS_TRACE_TREE, "Insert into leaf after splitting");
    InodeNo temp_keys[leafOrder + 1];
    Inode temp_pInodes[leafOrder + 1];
    unsigned int insertion_index, split, new_key, i, j;
    memset(temp_keys, 0, leafOrder + 1 * sizeof(InodeNo));
    memset(temp_pInodes, 0, leafOrder + 1 * sizeof(Inode));

    TreeCacheNode* new_leaf = nullptr;

    mCache.lockTreeCacheNode(leaf);
    Result r = mCache.addNewCacheNode(new_leaf);
    mCache.unlockTreeCacheNode(leaf);
    if (r != Result::ok)
        return r;

    new_leaf->raw.isLeaf = true;

    insertion_index = 0;
    while (insertion_index < leafOrder && leaf.raw.as.leaf.keys[insertion_index] < newInode.no)
        insertion_index++;

    for (i = 0, j = 0; i < leaf.raw.keys; i++, j++)
    {
        if (j == insertion_index)
            j++;
        temp_keys[j] = leaf.raw.as.leaf.keys[i];
        temp_pInodes[j] = leaf.raw.as.leaf.pInodes[i];
    }

    temp_keys[insertion_index] = newInode.no;
    temp_pInodes[insertion_index] = newInode;

    leaf.raw.keys = 0;

    split = cut(leafOrder);

    for (i = 0; i < split; i++)
    {
        leaf.raw.as.leaf.pInodes[i] = temp_pInodes[i];
        leaf.raw.as.leaf.keys[i] = temp_keys[i];
        leaf.raw.keys++;
    }

    for (i = split, j = 0; i <= leafOrder; i++, j++)
    {
        new_leaf->raw.as.leaf.pInodes[j] = temp_pInodes[i];
        new_leaf->raw.as.leaf.keys[j] = temp_keys[i];
        new_leaf->raw.keys++;
    }

    for (i = leaf.raw.keys; i < leafOrder; i++)
    {
        memset(&leaf.raw.as.leaf.pInodes[i], 0, sizeof(Inode));
        leaf.raw.as.leaf.keys[i] = 0;
    }
    for (i = new_leaf->raw.keys; i < leafOrder; i++)
    {
        memset(&new_leaf->raw.as.leaf.pInodes[i], 0, sizeof(Inode));
        new_leaf->raw.as.leaf.keys[i] = 0;
    }

    new_leaf->dirty = true;
    new_leaf->parent = leaf.parent;
    new_key = new_leaf->raw.as.leaf.keys[0];
    leaf.dirty = true;

    return insertIntoParent(leaf, new_key, *new_leaf);
}

/* Inserts a new key and pointer to a TreeCacheNode
 * into a TreeCacheNode into which these can fit
 * without violating the B+ tree properties.
 * (No further Tree-action Required)
 */
Result
Btree::insertIntoNode(TreeCacheNode& node, int left_index, InodeNo key, TreeCacheNode& right)
{
    int i;

    for (i = node.raw.keys; i > left_index; i--)
    {
        node.raw.as.branch.pointers[i + 1] = node.raw.as.branch.pointers[i];
        node.pointers[i + 1] = node.pointers[i];
        node.raw.as.branch.keys[i] = node.raw.as.branch.keys[i - 1];
    }
    node.raw.as.branch.pointers[left_index + 1] = right.raw.self;
    node.pointers[left_index + 1] = &right;

    node.raw.as.branch.keys[left_index] = key;
    node.raw.keys++;
    node.dirty = true;
    right.parent = &node;
    right.dirty = true;

    return Result::ok;
}

/* Inserts a new key and pointer to a TreeCacheNode
 * into a TreeCacheNode, causing the TreeCacheNode's size to exceed
 * the order, and causing the TreeCacheNode to split into two.
 */
Result
Btree::insertIntoNodeAfterSplitting(TreeCacheNode& old_node,
                                        unsigned int left_index,
                                        InodeNo key,
                                        TreeCacheNode& right)
{
    PAFFS_DBG_S(PAFFS_TRACE_TREE,
                "Insert into node after splitting at key %" PRIu32 ", index %" PRId16 "",
                key,
                left_index);
    unsigned int i, j, split, k_prime;
    TreeCacheNode* new_node;
    InodeNo temp_keys[branchOrder + 1];
    TreeCacheNode* temp_RAMaddresses[branchOrder + 1];
    Addr temp_addresses[branchOrder + 1];

    mCache.lockTreeCacheNode(old_node);
    mCache.lockTreeCacheNode(right);
    Result r = mCache.addNewCacheNode(new_node);
    if (r != Result::ok)
        return r;
    mCache.unlockTreeCacheNode(old_node);
    mCache.unlockTreeCacheNode(right);

    /* First create a temporary set of keys and pointers
     * to hold everything in order, including
     * the new key and pointer, inserted in their
     * correct places.
     * Then create a new TreeCacheNode and copy half of the
     * keys and pointers to the old TreeCacheNode and
     * the other half to the new.
     */
    for (i = 0, j = 0; i < old_node.raw.keys + 1U; i++, j++)
    {
        if (j == left_index + 1)
            j++;
        temp_addresses[j] = old_node.raw.as.branch.pointers[i];
        temp_RAMaddresses[j] = old_node.pointers[i];
    }

    for (i = 0, j = 0; i < old_node.raw.keys; i++, j++)
    {
        if (j == left_index)
            j++;
        temp_keys[j] = old_node.raw.as.branch.keys[i];
    }

    temp_addresses[left_index + 1] = right.raw.self;
    temp_RAMaddresses[left_index + 1] = &right;
    temp_keys[left_index] = key;

    /* Create the new TreeCacheNode and copy
     * half the keys and pointers to the
     * old and half to the new.
     */
    split = cut(branchOrder);

    old_node.raw.keys = 0;
    for (i = 0; i < split - 1; i++)
    {
        old_node.raw.as.branch.pointers[i] = temp_addresses[i];
        old_node.pointers[i] = temp_RAMaddresses[i];
        old_node.raw.as.branch.keys[i] = temp_keys[i];
        old_node.raw.keys++;
    }
    old_node.raw.as.branch.pointers[i] = temp_addresses[i];
    old_node.pointers[i] = temp_RAMaddresses[i];
    k_prime = temp_keys[split - 1];
    for (++i, j = 0; i < branchOrder; i++, j++)
    {
        new_node->pointers[j] = temp_RAMaddresses[i];
        new_node->raw.as.branch.pointers[j] = temp_addresses[i];
        new_node->raw.as.branch.keys[j] = temp_keys[i];
        new_node->raw.keys++;
        if (new_node->pointers[j] != nullptr)
            new_node->pointers[j]->parent = new_node;
        // cleanup
        old_node.pointers[i] = 0;
        old_node.raw.as.branch.pointers[i] = 0;
        if (i < branchOrder - 1)
            old_node.raw.as.branch.keys[i] = 0;
    }

    new_node->pointers[j] = temp_RAMaddresses[i];
    new_node->raw.as.branch.pointers[j] = temp_addresses[i];
    if (new_node->pointers[j] != nullptr)
        new_node->pointers[j]->parent = new_node;
    new_node->parent = old_node.parent;

    old_node.dirty = true;
    new_node->dirty = true;

    /* Insert a new key into the parent of the two
     * nodes resulting from the split, with
     * the old TreeCacheNode to the left and the new to the right.
     */

    return insertIntoParent(old_node, k_prime, *new_node);
}

/* Inserts a new TreeCacheNode (leaf or internal TreeCacheNode) into the B+ tree.
 */
Result
Btree::insertIntoParent(TreeCacheNode& left, InodeNo key, TreeCacheNode& right)
{
    int left_index;
    TreeCacheNode* parent = left.parent;

    if (&left == parent)
        return insertIntoNewRoot(left, key, right);

    left_index = getLeftIndex(*parent, left);

    if (parent->raw.keys < branchOrder - 1)
        return insertIntoNode(*parent, left_index, key, right);

    return insertIntoNodeAfterSplitting(*parent, left_index, key, right);
}

/* Creates a new root for two subtrees
 * and inserts the appropriate key into
 * the new root.
 * COULD INITIATE A CACHE FLUSH
 */
Result
Btree::insertIntoNewRoot(TreeCacheNode& left, InodeNo key, TreeCacheNode& right)
{
    PAFFS_DBG_S(PAFFS_TRACE_TREE, "Insert into new root at key %" PRIu32 "", key);
    TreeCacheNode* new_root = nullptr;
    mCache.lockTreeCacheNode(left);
    mCache.lockTreeCacheNode(right);
    Result r = mCache.addNewCacheNode(new_root);
    if (r != Result::ok)
    {
        return r;
    }
    mCache.unlockTreeCacheNode(left);
    mCache.unlockTreeCacheNode(right);

    new_root->raw.isLeaf = false;
    new_root->raw.as.branch.keys[0] = key;
    new_root->raw.as.branch.pointers[0] = left.raw.self;
    new_root->pointers[0] = &left;
    left.parent = new_root;
    new_root->raw.as.branch.pointers[1] = right.raw.self;
    new_root->pointers[1] = &right;
    right.parent = new_root;

    new_root->raw.keys = 1;
    new_root->dirty = true;
    new_root->parent = new_root;

    return mCache.setRoot(*new_root);
}

/* start a new tree.
 * So init rootnode
 */
Result
Btree::startNewTree()
{
    mCache.clear();
    TreeCacheNode* new_root = nullptr;
    Result r = mCache.addNewCacheNode(new_root);
    if (r != Result::ok)
        return r;
    new_root->raw.isLeaf = true;
    new_root->dirty = true;
    new_root->parent = new_root;
    return mCache.setRoot(*new_root);
}

// DELETION.

/* Utility function for deletion.  Retrieves
 * the index of a TreeCacheNode's nearest neighbor (sibling)
 * to the left if one exists.  If not (the TreeCacheNode
 * is the leftmost child), returns -1 to signify
 * this special case.
 */
int
Btree::getNeighborIndex(TreeCacheNode& n)
{
    int i;
    TreeCacheNode* parent = n.parent;

    for (i = 0; i <= parent->raw.keys; i++)
        if (parent->pointers[i] == &n)  // It is allowed for all other pointers to be invalid
            return i - 1;

    // Error state.
    return -1;
}

/**
 * Does not realign
 */
Result
Btree::removeEntryFromNode(TreeCacheNode& n, InodeNo key)
{
    unsigned int i = 0;

    // Remove the key and shift other keys accordingly.
    while (n.raw.as.branch.keys[i] != key
           && i < n.raw.keys)  // as.branch is OK, because it is same memory as as.leaf
        i++;
    if (i > 0 && key < n.raw.as.branch.keys[i - 1])
    {
        PAFFS_DBG(
                PAFFS_TRACE_BUG, "Key to delete (%lu) not found!", static_cast<long unsigned>(key));
        return Result::bug;
    }

    if (n.raw.isLeaf)
    {
        for (++i; i < n.raw.keys; i++)
        {
            n.raw.as.leaf.keys[i - 1] = n.raw.as.leaf.keys[i];
            n.raw.as.leaf.pInodes[i - 1] = n.raw.as.leaf.pInodes[i];
        }
    }
    else
    {
        for (++i; i < n.raw.keys; i++)
        {
            n.raw.as.branch.keys[i - 1] = n.raw.as.branch.keys[i];
            n.raw.as.branch.pointers[i] = n.raw.as.branch.pointers[i + 1];
            n.pointers[i] = n.pointers[i + 1];
        }
    }

    // One key fewer.
    n.raw.keys--;

    // Set the other pointers to nullptr for tidiness.
    if (n.raw.isLeaf)
        for (i = n.raw.keys; i < leafOrder; i++)
        {
            memset(&n.raw.as.leaf.pInodes[i], 0, sizeof(Inode));
            n.raw.as.leaf.keys[i] = 0;
        }
    else
        for (i = n.raw.keys + 1; i < branchOrder; i++)
        {
            n.raw.as.branch.pointers[i] = 0;
            n.pointers[i] = nullptr;
            n.raw.as.branch.keys[i - 1] = 0;
        }

    n.dirty = true;

    return Result::ok;
}

Result
Btree::adjustRoot(TreeCacheNode& root)
{
    /* Case: nonempty root.
     * Key and pointer have already been deleted,
     * so just commit dirty changes.
     */

    if (root.raw.keys > 0)
        return Result::ok;

    /* Case: empty root.
     */

    // If it has a child, promote
    // the first (only) child
    // as the new root.

    if (!root.raw.isLeaf)
    {
        root.pointers[0]->parent = root.pointers[0];
        Result r = mCache.setRoot(*root.pointers[0]);
        if (r != Result::ok)
            return r;
        return mCache.removeNode(root);
    }

    // If it is a leaf (has no children),
    // then the whole tree is empty.

    return mCache.removeNode(root);
}

/* Coalesces a TreeCacheNode (n) that has become
 * too small after deletion
 * with a neighboring TreeCacheNode that
 * can accept the additional entries
 * without exceeding the maximum.
 */
Result
Btree::coalesceNodes(TreeCacheNode& n,
                      TreeCacheNode& neighbor,
                      int neighbor_index,
                      unsigned int k_prime)
{
    int i, j, neighbor_insertion_index, n_end;
    TreeCacheNode* tmp;

    PAFFS_DBG_S(PAFFS_TRACE_TREE, "Coalesce nodes at %" PRId16 "", k_prime);

    /* Swap neighbor with TreeCacheNode if TreeCacheNode is on the
     * extreme left and neighbor is to its right.
     */

    if (neighbor_index == -1)
    {
        tmp = &n;
        n = neighbor;
        neighbor = *tmp;
    }

    /* Starting point in the neighbor for copying
     * keys and pointers from n.
     * Recall that n and neighbor have swapped places
     * in the special case of n being a leftmost child.
     */

    neighbor_insertion_index = neighbor.raw.keys;

    /* Case:  nonleaf TreeCacheNode.
     * Append k_prime and the following pointer.
     * Append all pointers and keys from the neighbor.
     */

    if (!n.raw.isLeaf)
    {
        /* Append k_prime.
         */

        neighbor.raw.as.branch.keys[neighbor_insertion_index] = k_prime;
        neighbor.raw.keys++;

        n_end = n.raw.keys;

        for (i = neighbor_insertion_index + 1, j = 0; j < n_end; i++, j++)
        {
            neighbor.raw.as.branch.keys[i] = n.raw.as.branch.keys[j];
            neighbor.raw.as.branch.pointers[i] = n.raw.as.branch.pointers[j];
            neighbor.pointers[i] = n.pointers[j];
            neighbor.raw.keys++;
            n.raw.keys--;
        }

        /* The number of pointers is always
         * one more than the number of keys.
         */

        neighbor.raw.as.branch.pointers[i] = n.raw.as.branch.pointers[j];
        neighbor.pointers[i] = n.pointers[j];

        /* All children must now point up to the same parent.
         */

        for (i = 0; i < neighbor.raw.keys + 1; i++)
        {
            tmp = neighbor.pointers[i];
            tmp->parent = &neighbor;
        }
    }

    /* In a leaf, append the keys and pointers of
     * n to the neighbor.
     */

    else
    {
        for (i = neighbor_insertion_index, j = 0; j < n.raw.keys; i++, j++)
        {
            neighbor.raw.as.leaf.keys[i] = n.raw.as.leaf.keys[j];
            neighbor.raw.as.leaf.pInodes[i] = n.raw.as.leaf.pInodes[j];
            neighbor.raw.keys++;
        }
    }

    neighbor.dirty = true;

    Result r = deleteEntry(*n.parent, k_prime);
    if (r != Result::ok)
        return r;

    return mCache.removeNode(n);
}

/* Redistributes entries between two nodes when
 * one has become too small after deletion
 * but its neighbor is too big to append the
 * small TreeCacheNode's entries without exceeding the
 * maximum
 */
Result
Btree::redistributeNodes(TreeCacheNode& n,
                          TreeCacheNode& neighbor,
                          int neighbor_index,
                          unsigned int k_prime_index,
                          unsigned int k_prime)
{
    int i;

    PAFFS_DBG_S(PAFFS_TRACE_TREE, "Redistribute Nodes at k_prime %" PRId16 "", k_prime);
    /* Case: n has a neighbor to the left.
     * Pull the neighbor's last key-pointer pair over
     * from the neighbor's right end to n's left end.
     */

    if (neighbor_index != -1)
    {
        if (!n.raw.isLeaf)
        {
            n.raw.as.branch.pointers[n.raw.keys + 1] = n.raw.as.branch.pointers[n.raw.keys];
            n.pointers[n.raw.keys + 1] = n.pointers[n.raw.keys];
            for (i = n.raw.keys; i > 0; i--)
            {
                n.raw.as.branch.keys[i] = n.raw.as.branch.keys[i - 1];
                n.pointers[i] = n.pointers[i - 1];
                n.raw.as.branch.pointers[i] = n.raw.as.branch.pointers[i - 1];
            }
        }
        else
        {
            for (i = n.raw.keys; i > 0; i--)
            {
                n.raw.as.leaf.keys[i] = n.raw.as.leaf.keys[i - 1];
                n.raw.as.leaf.pInodes[i] = n.raw.as.leaf.pInodes[i - 1];
            }
        }

        if (!n.raw.isLeaf)
        {
            n.pointers[0] = neighbor.pointers[neighbor.raw.keys];  // getTreeNodeIndex not
                                                                       // needed, nullptr is also
                                                                       // allowed
            n.raw.as.branch.pointers[0] = neighbor.raw.as.branch.pointers[neighbor.raw.keys];
            n.pointers[0]->parent = &n;
            neighbor.pointers[neighbor.raw.keys] = nullptr;
            neighbor.raw.as.branch.pointers[neighbor.raw.keys] = 0;
            n.raw.as.branch.keys[0] = k_prime;
            n.parent->raw.as.branch.keys[k_prime_index] =
                    neighbor.raw.as.branch.keys[neighbor.raw.keys - 1];
        }
        else
        {
            n.raw.as.leaf.pInodes[0] = neighbor.raw.as.leaf.pInodes[neighbor.raw.keys - 1];
            memset(&neighbor.raw.as.leaf.pInodes[neighbor.raw.keys - 1], 0, sizeof(Inode));
            n.raw.as.leaf.keys[0] = neighbor.raw.as.leaf.keys[neighbor.raw.keys - 1];
            n.parent->raw.as.leaf.keys[k_prime_index] = n.raw.as.leaf.keys[0];
        }
    }

    /* Case: n is the leftmost child.
     * Take a key-pointer pair from the neighbor to the right.
     * Move the neighbor's leftmost key-pointer pair
     * to n's rightmost position.
     */

    else
    {
        if (n.raw.isLeaf)
        {
            n.raw.as.leaf.keys[n.raw.keys] = neighbor.raw.as.leaf.keys[0];
            n.raw.as.leaf.pInodes[n.raw.keys] = neighbor.raw.as.leaf.pInodes[0];
            n.parent->raw.as.leaf.keys[k_prime_index] = neighbor.raw.as.leaf.keys[1];
            for (i = 0; i < neighbor.raw.keys - 1; i++)
            {
                neighbor.raw.as.leaf.keys[i] = neighbor.raw.as.leaf.keys[i + 1];
                neighbor.raw.as.leaf.pInodes[i] = neighbor.raw.as.leaf.pInodes[i + 1];
            }
        }
        else
        {
            n.raw.as.branch.keys[n.raw.keys] = k_prime;
            n.pointers[n.raw.keys + 1] = neighbor.pointers[0];
            n.raw.as.branch.pointers[n.raw.keys + 1] = neighbor.raw.as.branch.pointers[0];
            n.pointers[n.raw.keys + 1]->parent = &n;
            n.parent->raw.as.branch.keys[k_prime_index] = neighbor.raw.as.branch.keys[0];
            for (i = 0; i < neighbor.raw.keys - 1; i++)
            {
                neighbor.raw.as.branch.keys[i] = neighbor.raw.as.branch.keys[i + 1];
                neighbor.pointers[i] = neighbor.pointers[i + 1];
                neighbor.raw.as.branch.pointers[i] = neighbor.raw.as.branch.pointers[i + 1];
            }
        }

        if (!n.raw.isLeaf)
        {
            neighbor.pointers[i] = neighbor.pointers[i + 1];
            neighbor.raw.as.branch.pointers[i] = neighbor.raw.as.branch.pointers[i + 1];
        }
    }

    /* n now has one more key and one more pointer;
     * the neighbor has one fewer of each.
     */

    n.raw.keys++;
    neighbor.raw.keys--;

    n.dirty = true;
    neighbor.dirty = true;
    n.parent->dirty = true;

    return Result::ok;
}

/* Deletes an entry from the B+ tree.
 * Removes the Inode and its key and pointer
 * from the leaf, and then makes all appropriate
 * changes to preserve the B+ tree properties.
 */
Result
Btree::deleteEntry(TreeCacheNode& n, InodeNo key)
{
    int min_keys;
    TreeCacheNode* neighbor = nullptr;
    int neighbor_index;
    int k_prime_index, k_prime;
    int capacity;

    // Remove key and pointer from TreeCacheNode.

    Result r = removeEntryFromNode(n, key);
    if (r != Result::ok)
        return r;

    /* Case:  deletion from root.
     */

    if (n.parent == &n)
        return adjustRoot(n);

    /* Case:  deletion from a TreeCacheNode below the root.
     * (Rest of function body.)
     */

    /* Determine minimum allowable size of TreeCacheNode,
     * to be preserved after deletion.
     */

    min_keys = n.raw.isLeaf ? cut(leafOrder) : cut(branchOrder) - 1;

    /* Case:  TreeCacheNode stays at or above minimum.
     * (The simple case.)
     */

    if (n.raw.keys >= min_keys)
        return Result::ok;

    /* Case:  TreeCacheNode falls below minimum.
     * Either coalescence or redistribution
     * is needed.
     */

    /* Find the appropriate neighbor TreeCacheNode with which
     * to coalesce.
     * Also find the key (k_prime) in the parent
     * between the pointer to TreeCacheNode n and the pointer
     * to the neighbor.
     */

    neighbor_index = getNeighborIndex(n);
    k_prime_index = neighbor_index == -1 ? 0 : neighbor_index;
    k_prime = n.parent->raw.as.branch.keys[k_prime_index];
    r = neighbor_index == -1 ? mCache.getTreeNodeAtIndexFrom(1, *n.parent, neighbor)
                             : mCache.getTreeNodeAtIndexFrom(neighbor_index, *n.parent, neighbor);
    if (r != Result::ok)
        return r;

    capacity = neighbor->raw.isLeaf ? leafOrder : branchOrder - 1;

    /* Coalescence. */

    if (neighbor->raw.keys + n.raw.keys <= capacity)
        return coalesceNodes(n, *neighbor, neighbor_index, k_prime);

    /* Redistribution. */

    else
        return redistributeNodes(n, *neighbor, neighbor_index, k_prime_index, k_prime);
}

/* Prints the B+ tree in the command
 * line in level (rank) order, with the
 * keys in each TreeCacheNode and the '|' symbol
 * to separate nodes.
 */
void
Btree::printTree()
{
    TreeCacheNode* n = nullptr;
    Result r = mCache.getRootNodeFromCache(n);
    if (r != Result::ok)
    {
        printf("%s!\n", err_msg(r));
        return;
    }
    printKeys(*n);
    // print_leaves(&n);
}

/* Prints the bottom row of keys
 * of the tree (with their respective
 * pointers, if the verbose_output flag is set.
 */
void
Btree::printLeaves(TreeCacheNode& c)
{
    if (c.raw.isLeaf)
    {
        printf("| ");
        for (int i = 0; i < c.raw.keys; i++)
            printf("%" PRIu32 " ", static_cast<uint32_t>(c.raw.as.leaf.pInodes[i].no));
    }
    else
    {
        for (int i = 0; i <= c.raw.keys; i++)
        {
            TreeCacheNode* n = nullptr;
            Result r = mCache.getTreeNodeAtIndexFrom(i, c, n);
            if (r != Result::ok)
            {
                printf("%s!\n", err_msg(r));
                return;
            }
            printLeaves(*n);
            fflush(stdout);
        }
    }
}

/**
 * This only works to depth 'n' if RAM mCache is big enough to at least hold all nodes in Path to
 * depth 'n-1'
 */
void
Btree::printQueuedKeysRecursively(queue_s* q)
{
    queue_s* new_q = queue_new();
    printf("|");
    while (!queue_empty(q))
    {
        TreeCacheNode* n = static_cast<TreeCacheNode*>(queue_dequeue(q));
        for (int i = 0; i <= n->raw.keys; i++)
        {
            if (!n->raw.isLeaf)
            {
                TreeCacheNode* nn = nullptr;  // next node
                TreeCacheNode* n_cache =
                        nullptr;  // cache version of the copy of the former mCache entry...

                // Build up mCache to current branch.
                // This is not very efficient, but doing that once per branch would require
                // mCache to fit all child nodes of the current branch.
                Result r = findBranch(*n, n_cache);
                if (r != Result::ok)
                {
                    printf("%s!\n", err_msg(r));
                    return;
                }
                r = mCache.getTreeNodeAtIndexFrom(i, *n_cache, nn);
                if (r != Result::ok)
                {
                    printf("%s!\n", err_msg(r));
                    return;
                }
                TreeCacheNode* nn_copy = new TreeCacheNode;
                *nn_copy = *nn;
                queue_enqueue(new_q, nn_copy);
                if (i == 0)
                    printf(".");
                if (i < n->raw.keys)
                    printf("%" PRIu32 ".", static_cast<uint32_t>(n->raw.as.branch.keys[i]));
            }
            else
            {
                if (i == 0)
                    printf(" ");
                if (i < n->raw.keys)
                    printf("%" PRIu32 " ", static_cast<uint32_t>(n->raw.as.leaf.keys[i]));
            }
        }
        printf("|");
        delete n;
    }
    printf("\n");
    queue_destroy(q);
    if (!queue_empty(new_q))
        printQueuedKeysRecursively(new_q);
    else
        queue_destroy(new_q);
}

void
Btree::printKeys(TreeCacheNode& c)
{
    queue_s* q = queue_new();
    TreeCacheNode* c_copy = new TreeCacheNode;
    *c_copy = c;
    queue_enqueue(q, c_copy);
    printQueuedKeysRecursively(q);
}
}
