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

#include "treeCache.hpp"
#include "btree.hpp"
#include "dataIO.hpp"
#include "device.hpp"
#include <inttypes.h>
#include <string.h>

namespace paffs
{
void
TreeCache::setIndexUsed(uint16_t index)
{
    mCacheUsage.setBit(index);
}

void
TreeCache::setIndexFree(uint16_t index)
{
    mCacheUsage.resetBit(index);
}

bool
TreeCache::isIndexUsed(uint16_t index)
{
    return mCacheUsage.getBit(index);
}

int16_t
TreeCache::findFirstFreeIndex()
{
    size_t r = mCacheUsage.findFirstFree();
    return r == treeNodeCacheSize ? -1 : r;
}

int16_t
TreeCache::getIndexFromPointer(TreeCacheNode& tcn)
{
    if (&tcn - mCache > treeNodeCacheSize)
    {
        PAFFS_DBG(PAFFS_TRACE_BUG, "Tried to get Index from Pointer not inside array (%p)!", &tcn);
        dev->lasterr = Result::bug;
        return 0;
    }
    return &tcn - mCache;
}

/**
 * Deletes all the nodes
 */
void
TreeCache::clear()
{
    memset(mCache, 0, treeNodeCacheSize * sizeof(TreeCacheNode));
    mCacheUsage.clear();
}

Result
TreeCache::tryAddNewCacheNode(TreeCacheNode*& newTcn)
{
    int16_t index = findFirstFreeIndex();
    if (index < 0)
    {
        PAFFS_DBG_S(PAFFS_TRACE_TREECACHE, "Cache is full!");
        if (traceMask & PAFFS_TRACE_TREECACHE)
            printTreeCache();
        return Result::lowmem;
    }
    newTcn = &mCache[index];
    memset(newTcn, 0, sizeof(TreeCacheNode));
    setIndexUsed(index);
    PAFFS_DBG_S(PAFFS_TRACE_TREECACHE, "Created new Cache element %p (position %d)", newTcn, index);
    return Result::ok;
}


Result
TreeCache::tryGetHeight(uint16_t &heightOut)
{
    for(uint16_t node = 0; node < treeNodeCacheSize; node++)
    {
        if(mCacheUsage.getBit(node) && mCache[node].raw.isLeaf)
        {
            TreeCacheNode* curr = &mCache[node];
            while(curr != curr->parent)
            {
                ++heightOut;
                curr = curr->parent;
            }
            return Result::ok;
        }
    }
    return Result::fail;
}

/*
 * The new tcn.parent has to be changed _before_ calling another addNewCacheNode!
 * IDEA: Lock nodes when operating to prevent deletion
 */
Result
TreeCache::addNewCacheNode(TreeCacheNode*& newTcn)
{
    Result r = tryAddNewCacheNode(newTcn);
    if (r == Result::ok)
        return r;
    if (r != Result::lowmem)
        return r;
    if (traceMask & PAFFS_TRACE_VERIFY_TC && !isTreeCacheValid())
    {
        printTreeCache();
        PAFFS_DBG(PAFFS_TRACE_BUG, "Tree is invalid after adding new Cache Node!");
        return Result::bug;
    }

    if (traceMask & PAFFS_TRACE_TREECACHE)
    {
        printTreeCache();
    }
    r = freeNodes(1);
    if (r != Result::ok)
    {
        PAFFS_DBG(PAFFS_TRACE_ERROR, "Could not free one node!");
        return r;
    }

    return tryAddNewCacheNode(newTcn);
}

bool
TreeCache::isParentPathClean(TreeCacheNode& tcn)
{
    if (tcn.dirty)
        return false;
    if (tcn.parent == &tcn)
        return true;
    if (tcn.parent == nullptr)
    {
        PAFFS_DBG(PAFFS_TRACE_BUG, "Parent of %p is nullptr!", &tcn);
        dev->lasterr = Result::bug;
        return false;
    }
    return isParentPathClean(*tcn.parent);
}

/**
 * returns true if any sibling is dirty
 * stops at first occurrence.
 */
bool
TreeCache::areSiblingsClean(TreeCacheNode& tcn)
{
    if (tcn.dirty)
        return false;
    if (tcn.raw.isLeaf)
        return !tcn.dirty;
    for (int i = 0; i <= tcn.raw.keys; i++)
    {
        if (tcn.pointers[i] == nullptr)  // Siblings not in cache
            continue;
        if (!areSiblingsClean(*tcn.pointers[i]))
        {
            tcn.dirty = true;
            return false;
        }
    }
    return true;
}

bool
TreeCache::isSubTreeValid(TreeCacheNode& node,
                          BitList<treeNodeCacheSize>& reachable,
                          InodeNo keyMin,
                          InodeNo keyMax)
{
    reachable.setBit(getIndexFromPointer(node));

    if (node.parent == nullptr)
    {
        PAFFS_DBG(PAFFS_TRACE_BUG, "Node n° %d has invalid parent!", getIndexFromPointer(node));
        return false;
    }

    if (node.raw.self == 0 && !node.dirty)
    {
        PAFFS_DBG(PAFFS_TRACE_BUG,
                  "Node n° %d is not dirty, but has no flash address!",
                  getIndexFromPointer(node));
        return false;
    }
    InodeNo last = 0;
    bool first = true;
    if (node.raw.isLeaf)
    {
        for (int i = 0; i < node.raw.keys; i++)
        {
            if (node.raw.as.leaf.keys[i] != node.raw.as.leaf.pInodes[i].no)
            {
                PAFFS_DBG(PAFFS_TRACE_BUG,
                          "Node n° %" PRIu16 " has different Inode number (%d) than its key stated (%d)!",
                          getIndexFromPointer(node),
                          node.raw.as.leaf.keys[i],
                          node.raw.as.leaf.pInodes[i].no);
                return false;
            }

            if (!first && node.raw.as.leaf.keys[i] < last)
            {
                PAFFS_DBG(PAFFS_TRACE_BUG,
                          "Node n° %d is not sorted (prev: %d, curr: %d)!",
                          getIndexFromPointer(node),
                          last,
                          node.raw.as.leaf.keys[i]);
                return false;
            }
            last = node.raw.as.leaf.keys[i];
            first = false;

            if (keyMin != 0)
            {
                if (node.raw.as.leaf.keys[i] < keyMin)
                {
                    PAFFS_DBG(PAFFS_TRACE_BUG,
                              "Node n° %" PRIu32 "'s keys are inconsistent!\n"
                              "\twas: %" PRIu32 ", but parent stated keys would be over or equal %" PRIu32 "!",
                              getIndexFromPointer(node),
                              node.raw.as.leaf.keys[i],
                              keyMin);
                    return false;
                }
            }
            if (keyMax != 0)
            {
                if (node.raw.as.leaf.keys[i] >= keyMax)
                {
                    PAFFS_DBG(PAFFS_TRACE_BUG,
                              "Node n° %" PRIu32 "'s keys are inconsistent!\n"
                              "\twas: %" PRIu32 ", but parent stated keys would be under %" PRIu32 "!",
                              getIndexFromPointer(node),
                              node.raw.as.leaf.keys[i],
                              keyMax);
                    return false;
                }
            }
        }
    }
    else
    {
        for (int i = 0; i <= node.raw.keys; i++)
        {
            if (i < node.raw.keys)
            {
                if (keyMin != 0)
                {
                    if (node.raw.as.branch.keys[i] < keyMin)
                    {
                        PAFFS_DBG(PAFFS_TRACE_BUG,
                                  "Node n° %" PRIu32 "'s keys are inconsistent!\n"
                                  "\twas: %" PRIu32 ", but parent stated keys would be over or equal %" PRIu32 "!",
                                  getIndexFromPointer(node),
                                  node.raw.as.branch.keys[i],
                                  keyMin);
                        return false;
                    }
                }
                if (keyMax != 0)
                {
                    if (node.raw.as.branch.keys[i] >= keyMax)
                    {
                        PAFFS_DBG(PAFFS_TRACE_BUG,
                                  "Node n° %" PRIu32 "'s keys are inconsistent!\n"
                                  "\twas: %" PRIu32 ", but parent stated keys would be under %" PRIu32 "!",
                                  getIndexFromPointer(node),
                                  node.raw.as.branch.keys[i],
                                  keyMax);
                        return false;
                    }
                }

                if (!first && node.raw.as.branch.keys[i] < last)
                {
                    PAFFS_DBG(PAFFS_TRACE_BUG,
                              "Node n° %" PRIu16 " is not sorted (prev: %" PRIu32 ", curr: %" PRIu32
                              ")!",
                              getIndexFromPointer(node),
                              node.raw.as.leaf.keys[i],
                              last);
                    return false;
                }
                last = node.raw.as.branch.keys[i];
                first = false;
            }

            if (node.pointers[i] != nullptr)
            {
                if (node.pointers[i]->parent != &node)
                {
                    PAFFS_DBG(PAFFS_TRACE_BUG,
                              "Node n° %d stated parent was %d, but is actually %d!",
                              getIndexFromPointer(*node.pointers[i]),
                              getIndexFromPointer(*node.pointers[i]->parent),
                              getIndexFromPointer(node));
                    return false;
                }
                long keyMin_n = i == 0 ? 0 : node.raw.as.branch.keys[i - 1];
                long keyMax_n = i >= node.raw.keys ? 0 : node.raw.as.branch.keys[i];
                if (!isSubTreeValid(*node.pointers[i], reachable, keyMin_n, keyMax_n))
                    return false;
            }
        }
    }

    return true;
}

bool
TreeCache::isTreeCacheValid()
{
    // Just for debugging purposes
    BitList<treeNodeCacheSize> reachable;

    if (!isIndexUsed(mCacheRoot))
        return true;

    if (!isSubTreeValid(mCache[mCacheRoot], reachable, 0, 0))
        return false;

    bool valid = true;
    if (reachable != mCacheUsage)
    {
        for (int i = 0; i < treeNodeCacheSize; i++)
        {
            if (mCacheUsage.getBit(i) < reachable.getBit(i))
            {
                PAFFS_DBG(PAFFS_TRACE_BUG, "Deleted Node n° %d still reachable!", i);
                return false;
            }
            if (mCacheUsage.getBit(i) > reachable.getBit(i))
            {
                if (!mCache[i].locked && !mCache[i].inheritedLock)
                {
                    // it is allowed if we are moving a parent around
                    bool parentLocked = false;
                    TreeCacheNode*& par = mCache[i].parent;
                    while (par != par->parent)
                    {
                        if (par->locked)
                        {
                            parentLocked = true;
                            break;
                        }
                        par = par->parent;
                    }
                    if (!parentLocked)
                    {
                        PAFFS_DBG(PAFFS_TRACE_BUG, "Cache contains unreachable node %d!", i);
                        valid = false;
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
bool
TreeCache::resolveDirtyPaths(TreeCacheNode& tcn)
{
    if (tcn.raw.isLeaf)
        return tcn.dirty;

    bool anyDirt = false;
    for (int i = 0; i <= tcn.raw.keys; i++)
    {
        if (tcn.pointers[i] == nullptr)
            continue;
        if (!isIndexUsed(getIndexFromPointer(*tcn.pointers[i])))  // Sibling is not in cache)
            continue;
        if (resolveDirtyPaths(*tcn.pointers[i]))
        {
            tcn.dirty = true;
            anyDirt = true;
        }
    }
    return anyDirt | tcn.dirty;
}

void
TreeCache::markParentPathDirty(TreeCacheNode& tcn)
{
    tcn.dirty = true;
    if (tcn.parent == &tcn)
        return;
    if (isIndexUsed(getIndexFromPointer(*tcn.parent)))
    {
        PAFFS_DBG(PAFFS_TRACE_BUG, "Parent of %p is not in cache!", &tcn);
        dev->lasterr = Result::bug;
        return;
    }
    return markParentPathDirty(*tcn.parent);
}

void
TreeCache::deleteFromParent(TreeCacheNode& tcn)
{
    if (tcn.parent == nullptr)
    {
        PAFFS_DBG(PAFFS_TRACE_BUG,
                  "Tried to delete node %d from nullptr parent!",
                  getIndexFromPointer(tcn));
        isTreeCacheValid();  // This hopefully prints more detailed information
        dev->lasterr = Result::bug;
        return;
    }
    TreeCacheNode*& parent = tcn.parent;
    if (parent == &tcn)
        return;
    if (!isIndexUsed(getIndexFromPointer(*parent)))
    {
        // PAFFS_DBG(PAFFS_TRACE_BUG, "Parent of %p is not in cache!", tcn);
        // dev->lasterr = Result::bug;	//This is not a bug since the parent could be freed before
        // the sibling
        return;
    }
    for (unsigned int i = 0; i <= parent->raw.keys; i++)
    {
        if (parent->pointers[i] == &tcn)
        {
            parent->pointers[i] = nullptr;
            return;
        }
    }
}

bool
TreeCache::hasNoChilds(TreeCacheNode& tcn)
{
    if (tcn.raw.isLeaf)
        return true;
    for (int i = 0; i <= tcn.raw.keys; i++)
        if (tcn.pointers[i] != nullptr)
            return false;
    return true;
}

uint16_t
TreeCache::deletePathToRoot(TreeCacheNode& tcn)
{
    if (tcn.dirty || tcn.locked || tcn.inheritedLock)
        return 0;

    deleteFromParent(tcn);
    setIndexFree(getIndexFromPointer(tcn));
    if (tcn.parent != &tcn && hasNoChilds(*tcn.parent))
        return 1 + deletePathToRoot(*tcn.parent);
    return 1;
}

/*
 * Just frees clean leaf nodes
 */
Result
TreeCache::cleanFreeLeafNodes(uint16_t& neededCleanNodes)
{
    dev->lasterr = Result::ok;
    resolveDirtyPaths(mCache[mCacheRoot]);
    if (dev->lasterr != Result::ok)
        return dev->lasterr;

    for (unsigned int i = 0; i < treeNodeCacheSize; i++)
    {
        if (!isIndexUsed(getIndexFromPointer(mCache[i])))
            continue;
        if (!mCache[i].dirty && !mCache[i].locked && !mCache[i].inheritedLock && mCache[i].raw.isLeaf)
        {
            deleteFromParent(mCache[i]);
            setIndexFree(i);
            neededCleanNodes--;
            if (neededCleanNodes == 0)
            {
                return Result::ok;
            }
        }
    }
    return Result::ok;
}

/*
 * Frees clean nodes
 */
Result
TreeCache::cleanFreeNodes(uint16_t& neededCleanNodes)
{
    dev->lasterr = Result::ok;
    resolveDirtyPaths(mCache[mCacheRoot]);
    if (dev->lasterr != Result::ok)
        return dev->lasterr;
    for (unsigned int i = 0; i < treeNodeCacheSize; i++)
    {
        if (!isIndexUsed(getIndexFromPointer(mCache[i])))
            continue;
        if (!mCache[i].dirty && !mCache[i].locked && !mCache[i].inheritedLock && hasNoChilds(mCache[i]))
        {
            uint16_t cleaned = deletePathToRoot(mCache[i]);
            setIndexFree(i);
            if (cleaned > neededCleanNodes)
            {
                neededCleanNodes = 0;
                return Result::ok;
            }
            neededCleanNodes -= cleaned;
            if (neededCleanNodes == 0)
            {
                return Result::ok;
            }
        }
    }
    return Result::ok;
}
/**
 * Takes the node.raw.self to update parents flash pointer
 */
Result
TreeCache::updateFlashAddressInParent(TreeCacheNode& node)
{
    if (node.parent == &node)
    {
        // Rootnode
        return dev->superblock.registerRootnode(node.raw.self);
    }
    for (int i = 0; i <= node.parent->raw.keys; i++)
    {
        if (node.parent->pointers[i] == &node)
        {
            node.parent->raw.as.branch.pointers[i] = node.raw.self;
            node.parent->dirty = true;
            return Result::ok;
        }
    }
    PAFFS_DBG(PAFFS_TRACE_BUG,
              "Could not find Node %d in its parent (Node %d)!",
              getIndexFromPointer(node),
              getIndexFromPointer(*node.parent));
    return Result::notFound;
}


Result
TreeCache::commitNodesRecursively(TreeCacheNode& node)
{
    if (!node.dirty)
        return Result::ok;
    Result r;
    if (node.raw.isLeaf)
    {
        r = writeTreeNode(node);
        if (r != Result::ok)
        {
            PAFFS_DBG(PAFFS_TRACE_ERROR, "Could not write cached Treenode leaf!");
            return r;
        }
        node.dirty = false;
        return r;
    }

    for (uint16_t i = 0; i <= node.raw.keys; i++)
    {
        if (node.pointers[i] == nullptr)
            continue;
        r = commitNodesRecursively(*node.pointers[i]);
        if (r != Result::ok)
        {
            PAFFS_DBG(PAFFS_TRACE_ERROR,
                      "Could not write Node %" PRIu16 "'s child n° %" PRIu16 " (%" PRIu16 ") to flash!",
                      getIndexFromPointer(node),
                      i,
                      getIndexFromPointer(*node.pointers[i]));
            return r;
        }
    }

    r = writeTreeNode(node);
    if (r != Result::ok)
    {
        PAFFS_DBG(PAFFS_TRACE_ERROR, "Could not write cached Treenode!");
        return r;
    }
    node.dirty = false;
    return r;
}

/**
 * Commits complete Tree to Flash
 */
Result
TreeCache::commitCache()
{
    dev->lasterr = Result::ok;
    resolveDirtyPaths(mCache[mCacheRoot]);
    if (dev->lasterr != Result::ok)
        return dev->lasterr;
    Result r = commitNodesRecursively(mCache[mCacheRoot]);
    if (r != Result::ok)
    {
        PAFFS_DBG(PAFFS_TRACE_ERROR, "Could not write Node to flash! (%s)", err_msg(r));
        return r;
    }

    bool unreachableNodesExisting = false;
    for(uint16_t node = 0; node < treeNodeCacheSize; node++)
    {
        if(mCache[node].dirty && !mCache[node].locked)
        {
            //We could start a desperate mode, committing non-locked nodes in floating tree.
            //But this is dangerous for powerloss, leaving tree in inconsistent state.
            PAFFS_DBG(PAFFS_TRACE_ALWAYS, "Commit did not affect floating node %" PRIu16, node);
            unreachableNodesExisting = true;
        }
    }

    if(!unreachableNodesExisting)
    {
        dev->journal.addEvent(journalEntry::Success(JournalEntry::Topic::tree));
    }

    return Result::ok;
}

Result
TreeCache::reserveNodes(uint16_t neededNodes)
{

    PAFFS_DBG(PAFFS_TRACE_TREECACHE, "Reserving %" PRIu16 " nodes", neededNodes);

    if(static_cast<uint16_t>(treeNodeCacheSize - mCacheUsage.countSetBits()) >= neededNodes)
    {
        return Result::ok;
    }

    return freeNodes(neededNodes - (treeNodeCacheSize - mCacheUsage.countSetBits()));
}

Result
TreeCache::freeNodes(uint16_t neededCleanNodes)
{
    if (traceMask & PAFFS_TRACE_VERIFY_TC && !isTreeCacheValid())
    {
        PAFFS_DBG(PAFFS_TRACE_ERROR, "TreeCache invalid!");
        return Result::bug;
    }

    if (neededCleanNodes > treeNodeCacheSize)
    {
        PAFFS_DBG(PAFFS_TRACE_BUG, "Tried reserving more nodes than we could have!");
        return Result::bug;
    }


    cleanFreeLeafNodes(neededCleanNodes);
    if (neededCleanNodes == 0)
        return Result::ok;

    cleanFreeNodes(neededCleanNodes);
    if (neededCleanNodes == 0)
        return Result::ok;

    commitCache();

    cleanFreeLeafNodes(neededCleanNodes);
    if (neededCleanNodes == 0)
        return Result::ok;

    cleanFreeNodes(neededCleanNodes);
    if (neededCleanNodes == 0)
        return Result::ok;

    PAFFS_DBG_S(PAFFS_TRACE_ERROR, "Even hard commit could not allocate "
            "remaining %" PRIu16 " treenodes!", neededCleanNodes);
    return Result::lowmem;
}

/**
 * This locks specified treeCache node and its path from Rootnode
 * To prevent Cache GC from deleting it
 */
Result
TreeCache::TreeCache::lockTreeCacheNode(TreeCacheNode& tcn)
{
    tcn.locked = true;
    if (tcn.parent == nullptr)
        return Result::ok;

    TreeCacheNode* curr = tcn.parent;
    while (curr->parent != curr)
    {
        curr->inheritedLock = true;
        curr = curr->parent;
    }
    curr->inheritedLock = true;

    return Result::ok;
}

bool
TreeCache::hasLockedChilds(TreeCacheNode& tcn)
{
    if (tcn.raw.isLeaf)
    {
        // PAFFS_DBG(PAFFS_TRACE_BUG, "Node is leaf, has no childs!");
        return false;
    }
    for (int i = 0; i <= tcn.raw.keys; i++)
    {
        if (tcn.pointers[i] != nullptr)
        {
            if (tcn.pointers[i]->inheritedLock || tcn.pointers[i]->locked)
                return true;
        }
    }
    return false;
}

Result
TreeCache::unlockTreeCacheNode(TreeCacheNode& tcn)
{
    if (!tcn.locked)
    {
        PAFFS_DBG(
                PAFFS_TRACE_ERROR, "Tried to double-unlock node n° %d!", getIndexFromPointer(tcn));
    }
    tcn.locked = false;

    if (tcn.parent == nullptr)
    {
        PAFFS_DBG(PAFFS_TRACE_ERROR, "Node %d with invalid parent !", getIndexFromPointer(tcn));
        return Result::fail;
    }

    TreeCacheNode* curr = tcn.parent;
    TreeCacheNode* old = nullptr;
    do
    {
        if (hasLockedChilds(*curr))
            break;

        curr->inheritedLock = false;

        if (curr->locked)
            break;

        if (tcn.parent == nullptr)
        {
            PAFFS_DBG(PAFFS_TRACE_ERROR, "Node %d with invalid parent !", getIndexFromPointer(tcn));
            return Result::fail;
        }
        old = curr;
        curr = curr->parent;

    } while (old != curr);

    return Result::ok;
}

Result
TreeCache::getRootNodeFromCache(TreeCacheNode*& tcn)
{
    if (isIndexUsed(mCacheRoot))
    {
        tcn = &mCache[mCacheRoot];
        mCacheHits++;
        return Result::ok;
    }
    mCacheMisses++;

    PAFFS_DBG_S(PAFFS_TRACE_TREECACHE, "Load rootnode from Flash");

    Addr addr = dev->superblock.getRootnodeAddr();
    if (addr == 0)
        PAFFS_DBG(PAFFS_TRACE_TREE, "get Rootnode, but does not exist!");

    TreeCacheNode* new_root;
    Result r = tryAddNewCacheNode(new_root);
    if (r != Result::ok)
    {
        PAFFS_DBG(PAFFS_TRACE_ERROR,
                  "Rootnode can't be loaded, mCache size (%d) too small!",
                  treeNodeCacheSize);
        return r;
    }
    new_root->parent = new_root;
    r = setRoot(*new_root);
    if (r != Result::ok)
        return r;

    tcn = new_root;

    r = readTreeNode(addr, mCache[mCacheRoot].raw);
    if (r == Result::biterrorCorrected)
    {
        // Make sure it is going to be rewritten to flash
        PAFFS_DBG(PAFFS_TRACE_INFO, "Corrected biterror in TreeNode");
        mCache[mCacheRoot].dirty = true;
        return Result::ok;
    }
    return r;
}

/**
 * Possible mCache flush. Tree could be empty except for path to child! (and parent, of course)
 */
Result
TreeCache::getTreeNodeAtIndexFrom(uint16_t index, TreeCacheNode& parent, TreeCacheNode*& child)
{
    if (index > branchOrder)
    {  // FIXME index is smaller than branchOder?
        PAFFS_DBG(PAFFS_TRACE_BUG, "Tried to access index greater than branch size!");
        return Result::bug;
    }

    TreeCacheNode* target = parent.pointers[index];
    // To make sure parent and child can point to the same address, target is used as tmp buffer

    if (parent.raw.isLeaf)
    {
        PAFFS_DBG(PAFFS_TRACE_BUG, "Tried to get Node from leaf!");
        return Result::bug;
    }
    if (target != nullptr)
    {
        child = target;
        mCacheHits++;
        if (traceMask & PAFFS_TRACE_VERBOSE)
        {
            PAFFS_DBG_S(PAFFS_TRACE_TREECACHE,
                        "Cache hit, found target %p (position %ld)",
                        target,
                        target - mCache);
        }
        return Result::ok;  // mCache hit
    }

    //--------------
    dev->lasterr = Result::ok;
    if (getIndexFromPointer(parent) == 0 && dev->lasterr != Result::ok)
    {
        PAFFS_DBG(PAFFS_TRACE_BUG, "Tried to get child from Treenode not located in mCache!");
        dev->lasterr = Result::ok;
        return Result::invalidInput;
    }

    if (parent.raw.as.branch.pointers[index] == 0)
    {
        PAFFS_DBG(PAFFS_TRACE_ERROR,
                  "Tried to get treenode neither located in mCache nor in flash!");
        return Result::invalidInput;
    }

    mCacheMisses++;
    if (traceMask & PAFFS_TRACE_VERBOSE)
    {
        PAFFS_DBG_S(PAFFS_TRACE_TREECACHE, "Cache Miss");
    }

    lockTreeCacheNode(parent);
    Result r = addNewCacheNode(target);
    if (r != Result::ok)
    {
        unlockTreeCacheNode(parent);
        return r;
    }
    unlockTreeCacheNode(parent);

    target->parent = &parent;
    parent.pointers[index] = target;
    child = target;

    r = readTreeNode(parent.raw.as.branch.pointers[index], child->raw);
    if (r == Result::biterrorCorrected)
    {
        child->dirty = true;
        return Result::ok;
    }
    return r;
}

Result
TreeCache::removeNode(TreeCacheNode& tcn)
{
    setIndexFree(getIndexFromPointer(tcn));
    if (tcn.raw.self != 0)
    {
        return deleteTreeNode(tcn.raw);
    }

    return Result::ok;
}

Result
TreeCache::setRoot(TreeCacheNode& rootTcn)
{
    if (rootTcn.parent != &rootTcn)
    {
        PAFFS_DBG(PAFFS_TRACE_BUG, "BUG: setCacheRoot with root->parent not pointing to itself");
        return Result::bug;
    }
    mCacheRoot = getIndexFromPointer(rootTcn);
    return Result::ok;
}

// debug
uint16_t
TreeCache::getCacheUsage()
{
    uint16_t usage = 0;
    for (unsigned int i = 0; i < treeNodeCacheSize; i++)
    {
        if (isIndexUsed(i))
            usage++;
    }
    return usage;
}

uint16_t
TreeCache::getCacheSize()
{
    return treeNodeCacheSize;
}

uint16_t
TreeCache::getCacheHits()
{
    return mCacheHits;
}
uint16_t
TreeCache::getCacheMisses()
{
    return mCacheMisses;
}
void
TreeCache::printNode(TreeCacheNode& node)
{
    printf("[ID: %d PAR: %d %s%s%s|",
               getIndexFromPointer(node),
               getIndexFromPointer(*node.parent),
               node.dirty ? "d" : "-",
               node.locked ? "l" : "-",
               node.inheritedLock ? "i" : "-");
        if (node.raw.isLeaf)
        {
            for (int i = 0; i < node.raw.keys; i++)
            {
                if (i > 0)
                    printf(",");
                printf(" %" PRIu32, node.raw.as.leaf.keys[i]);
            }
            printf("]\n");
        }
        else
        {
            if (node.pointers[0] == 0)
                printf("x");
            else
                printf("%d", getIndexFromPointer(*node.pointers[0]));

            bool isGap = false;
            for (int i = 1; i <= node.raw.keys; i++)
            {
                if (!isGap)
                    printf("/%" PRIu32 "\\", node.raw.as.branch.keys[i - 1]);
                if (node.pointers[i] == 0)
                {
                    if (!isGap)
                    {
                        printf("...");
                        isGap = true;
                    }
                }
                else
                {
                    printf("%d", getIndexFromPointer(*node.pointers[i]));
                    isGap = false;
                }
            }
            printf("]\n");
        }
}
void
TreeCache::printSubtree(int layer, BitList<treeNodeCacheSize>& reached, TreeCacheNode& node)
{
    reached.setBit(getIndexFromPointer(node));
    for (int i = 0; i < layer; i++)
    {
        printf(".");
    }
    printNode(node);
    for (int i = 0; i <= node.raw.keys; i++)
    {
        if (node.pointers[i] != 0)
        {
            printSubtree(layer + 1, reached, *node.pointers[i]);
        }
    }

}
void
TreeCache::printTreeCache()
{
    printf("-----------------\n");
    if (isIndexUsed(mCacheRoot))
    {
        BitList<treeNodeCacheSize> reached;
        printSubtree(0, reached, mCache[mCacheRoot]);
        printf("- - floating: - -\n");
        for(uint16_t node = 0; node < treeNodeCacheSize; node++)
        {
            if(!reached.getBit(node) && mCacheUsage.getBit(node))
            {
                printf("~ ");
                printNode(mCache[node]);
            }
        }
    }
    else
        printf("Empty treeCache.\n");
    printf("-----------------\n");
}

/**
 * \warn changes address in parent Node
 */
Result
TreeCache::writeTreeNode(TreeCacheNode& node)
{
    if (dev->readOnly)
    {
        PAFFS_DBG(PAFFS_TRACE_BUG, "Tried writing TreeNode in readOnly mode!");
        return Result::bug;
    }
    dev->lasterr = Result::ok;
    dev->areaMgmt.findWritableArea(AreaType::index);
    if (dev->lasterr != Result::ok)
    {
        return dev->lasterr;
    }
    // Handle Areas
    if (dev->areaMgmt.getStatus(dev->areaMgmt.getActiveArea(AreaType::index)) != AreaStatus::active)
    {
        PAFFS_DBG(PAFFS_TRACE_BUG, "BUG: findWritableArea returned inactive area!");
        return Result::bug;
    }
    if (dev->areaMgmt.getActiveArea(AreaType::index) == 0)
    {
        PAFFS_DBG(PAFFS_TRACE_BUG, "WRITE TREE NODE findWritableArea returned 0");
        return Result::bug;
    }

    PageOffs firstFreePage = 0;
    if (dev->areaMgmt.findFirstFreePage(firstFreePage,
                                        dev->areaMgmt.getActiveArea(AreaType::index))
        == Result::nospace)
    {
        PAFFS_DBG(PAFFS_TRACE_BUG,
                  "BUG: findWritableArea returned full area (%" PRIu32 " on %" PRIu32 ").",
                  dev->areaMgmt.getActiveArea(AreaType::index),
                  dev->areaMgmt.getPos(dev->areaMgmt.getActiveArea(AreaType::index)));
        return dev->lasterr = Result::bug;
    }
    Addr addr = combineAddress(dev->areaMgmt.getActiveArea(AreaType::index), firstFreePage);
    Addr oldSelf = node.raw.self;
    node.raw.self = addr;

    Result r = dev->driver.writePage(getPageNumber(node.raw.self, *dev), &node, sizeof(TreeNode));
    if (r != Result::ok)
    {
        PAFFS_DBG(PAFFS_TRACE_ERROR, "Could not write TreeNode to page");
        return r;
    }

    // Mark Page as used
    r = dev->sumCache.setPageStatus(
            dev->areaMgmt.getActiveArea(AreaType::index), firstFreePage, SummaryEntry::used);
    if (r != Result::ok)
    {
        PAFFS_DBG(PAFFS_TRACE_ERROR, "Could not mark Page as used!");
        return r;
    }

    r = updateFlashAddressInParent(node);
    if (r != Result::ok)
    {
        PAFFS_DBG(PAFFS_TRACE_ERROR, "Could not update node address in Parent!");
        return r;
    }

    /*
     * NOTE: For best space efficiency, this would be done before finding new Area.
     * However, this would lead to invalidated valid data if something during write fails.
     */
    if (oldSelf != 0)
    {
        // invalidate former position
        r = dev->sumCache.setPageStatus(oldSelf, SummaryEntry::dirty);
        if (r != Result::ok)
        {
            PAFFS_DBG(PAFFS_TRACE_ERROR,
                      "Could not invalidate old Page! Ignoring Errors to continue...");
        }
    }

    r = dev->areaMgmt.manageActiveAreaFull(AreaType::index);
    if (r != Result::ok)
        return r;

    return Result::ok;
}

Result
TreeCache::readTreeNode(Addr addr, TreeNode& node)
{
    if (dev->areaMgmt.getType(extractLogicalArea(addr)) != AreaType::index)
    {
        if (traceMask & PAFFS_TRACE_AREA)
        {
            printf("Info: \n\t%" PRIu32 " used Areas\n", dev->areaMgmt.getUsedAreas());
            for (AreaPos i = 0; i < areasNo; i++)
            {
                printf("\tArea %03" PRIu32 "  on %03" PRIu32 " as %10s from page %4d %s\n",
                       i,
                       dev->areaMgmt.getPos(i),
                       areaNames[dev->areaMgmt.getType(i)],
                       dev->areaMgmt.getPos(i) * blocksPerArea * pagesPerBlock,
                       areaStatusNames[dev->areaMgmt.getStatus(i)]);
                if (i > 128)
                {
                    printf("\n -- truncated 128-%" PRIu16 " Areas.\n", areasNo);
                    break;
                }
            }
            printf("\t----------------------\n");
        }
        PAFFS_DBG(PAFFS_TRACE_BUG,
                  "READ TREEENODE operation on %s (Area %" PRIu32 ", pos %" PRIu32 "!",
                  areaNames[dev->areaMgmt.getType(extractLogicalArea(addr))],
                  extractLogicalArea(addr),
                  dev->areaMgmt.getPos(extractLogicalArea(addr)));
        return Result::bug;
    }

    if (dev->areaMgmt.getType(extractLogicalArea(addr)) != AreaType::index)
    {
        PAFFS_DBG(PAFFS_TRACE_BUG,
                  "READ TREE NODE operation on %s Area at %X:%X",
                  areaNames[dev->areaMgmt.getType(extractLogicalArea(addr))],
                  extractLogicalArea(addr),
                  extractPageOffs(addr));
        return Result::bug;
    }

    Result r;
    if (traceMask & PAFFS_TRACE_VERIFY_AS)
    {
        SummaryEntry s = dev->sumCache.getPageStatus(addr, &r);
        if (s == SummaryEntry::free)
        {
            PAFFS_DBG(PAFFS_TRACE_BUG,
                      "READ operation on FREE data at %X:%X",
                      extractLogicalArea(addr),
                      extractPageOffs(addr));
            return Result::bug;
        }
        if (s == SummaryEntry::dirty)
        {
            PAFFS_DBG(PAFFS_TRACE_BUG,
                      "READ operation on DIRTY data at %X:%X",
                      extractLogicalArea(addr),
                      extractPageOffs(addr));
            return Result::bug;
        }
        if (r != Result::ok || s == SummaryEntry::error)
        {
            PAFFS_DBG(PAFFS_TRACE_ERROR, "Could not verify Page status!");
        }
    }

    r = dev->driver.readPage(getPageNumber(addr, *dev), &node, sizeof(TreeNode));
    if (r != Result::ok)
    {
        if (r == Result::biterrorCorrected)
        {
            PAFFS_DBG(PAFFS_TRACE_INFO, "Corrected biterror");
        }
        else
        {
            PAFFS_DBG(PAFFS_TRACE_ERROR, "Error reading Treenode");
            return r;
        }
    }

    if (node.self != addr)
    {
        PAFFS_DBG(PAFFS_TRACE_BUG,
                  "Read Treenode at %X:%X, but its content stated that it was on %X:%X",
                  extractLogicalArea(addr),
                  extractPageOffs(addr),
                  extractLogicalArea(node.self),
                  extractPageOffs(node.self));
        return Result::bug;
    }

    return r;
}

Result
TreeCache::deleteTreeNode(TreeNode& node)
{
    if (dev->readOnly)
    {
        PAFFS_DBG(PAFFS_TRACE_BUG, "Tried deleting something in readOnly mode!");
        return Result::bug;
    }
    return dev->sumCache.setPageStatus(node.self, SummaryEntry::dirty);
}
}
