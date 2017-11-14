/*
 * Copyright (c) 2017, German Aerospace Center (DLR)
 *
 * This file is part of the development version of OUTPOST.
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 *
 * Authors:
 * - 2017, Pascal Pieper (DLR RY-AVS)
 */
// ----------------------------------------------------------------------------

#include "commonTypes.hpp"
#pragma once

namespace paffs
{
// Calculates how many pointers a node can hold in one page
// Note that this struct is not packed.
static constexpr uint32_t branchOrder =
        (dataBytesPerPage - sizeof(Addr) - sizeof(bool) - sizeof(unsigned char))
        / (sizeof(Addr) + sizeof(InodeNo));
static constexpr uint32_t leafOrder =
        (dataBytesPerPage - sizeof(Addr) - sizeof(bool) - sizeof(unsigned char))
        / (sizeof(Inode) + sizeof(InodeNo));

typedef struct TreeNode{
	union As{
		struct Branch {
			InodeNo keys[branchOrder-1];
			Addr pointers[branchOrder];
		} branch;
		struct Leaf {
			InodeNo keys[leafOrder];
			Inode pInodes[leafOrder];
		} leaf;
	}as;
	Addr self;	//If '0', it is not committed yet
	bool is_leaf:1;
	uint16_t num_keys; //If leaf: Number of pInodes
							//If Branch: Number of addresses - 1
} treeNode;

struct TreeCacheNode
{
    TreeNode raw;
    struct TreeCacheNode* parent;  // Parent either points to parent or to node itself if is root.
                                   // Special case: NULL if node is invalid.
    struct TreeCacheNode* pointers[branchOrder];
    bool dirty : 1;
    bool locked : 1;
    bool inheritedLock : 1;
};

}  // namespace paffs
