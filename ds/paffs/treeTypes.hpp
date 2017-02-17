/*
 * treeTypes.hpp
 *
 *  Created on: 16 Feb 2017
 *      Author: rooot
 */

#include "commonTypes.hpp"
#pragma once

namespace paffs {

//Calculates how many pointers a node can hold in one page
static constexpr int branchOrder = (dataBytesPerPage - sizeof(Addr)
		- sizeof(unsigned char))
		/ (sizeof(Addr) + sizeof(InodeNo));
static constexpr int leafOrder = (dataBytesPerPage - sizeof(Addr)
		- sizeof(unsigned char))
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
	unsigned char num_keys:7; //If leaf: Number of pInodes
							//If Branch: Number of addresses - 1
} treeNode;

struct TreeCacheNode{
	TreeNode raw;
	struct TreeCacheNode* parent;	//Parent either points to parent or to node itself if is root. Special case: NULL if node is invalid.
	struct TreeCacheNode* pointers[branchOrder];
	bool dirty:1;
	bool locked:1;
	bool inheritedLock:1;
};

}  // namespace paffs
