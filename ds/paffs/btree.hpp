/**
 * File created on 18.05.2016
 * Author: Pascal Pieper
 */

#ifndef __PAFFS_BTREE_H__
#define __PAFFS_BTREE_H__

#include <stdbool.h>
#include <stddef.h>

#include "paffs.hpp"

namespace paffs{

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

class Btree{
	Device* dev;
	TreeCache cache;
public:
	Btree(Device* dev) : dev(dev){
		cache = TreeCache(dev);
	};

	Result insertInode(Inode* inode);
	Result getInode(InodeNo number, Inode* outInode);
	Result updateExistingInode(Inode* inode);
	Result deleteInode(InodeNo number);
	Result findFirstFreeNo(InodeNo* outNumber);


	void print_tree();
	void print_leaves(TreeCacheNode* c);
	void print_keys(TreeCacheNode* c);


private:

	Addr* getPointerAsAddr(char* pointers, unsigned int pos);
	Inode* getPointerAsInode(char* pointers, unsigned int pos);
	void insertAddrInPointer(char* pointers, Addr* addr, unsigned int pos);
	void insertInodeInPointer(char* pointers, Inode* inode, unsigned int pos);
	Result updateAddrInTreeCacheNode(TreeCacheNode* node, Addr* old, Addr* newAddress);
	bool isTreeCacheNodeEqual(TreeCacheNode* left, TreeCacheNode* right);

	//bool isEqual(TreeCacheNode* left, TreeCacheNode* right);
	int height(TreeCacheNode * root );
	//Length is number of Kanten, not Knoten
	int length_to_root(TreeCacheNode * child );
	//Path is root first, child last
	Result path_from_root(TreeCacheNode * child, Addr* path, unsigned int* lengthOut);
	int find_range(TreeCacheNode * root, InodeNo key_start, InodeNo key_end,
					int returned_keys[], void * returned_pointers[]);
	Result find_branch( TreeCacheNode* target, TreeCacheNode** outtreeCacheNode);
	Result find_leaf( InodeNo key, TreeCacheNode** outtreeCacheNode);
	Result find_in_leaf (TreeCacheNode* leaf, InodeNo key, Inode* outInode);
	Result find(InodeNo key, Inode* outInode);
	int cut(int length );


	// Insertion.

	int get_left_index(TreeCacheNode * parent, TreeCacheNode * left);
	Result insert_into_leaf(TreeCacheNode * leaf, Inode * pointer );
	Result insert_into_leaf_after_splitting(TreeCacheNode * leaf, Inode * newInode);
	Result insert_into_node(TreeCacheNode * newNode,
				int left_index, InodeNo key, TreeCacheNode * right);
	Result insert_into_node_after_splitting(TreeCacheNode * old_node, int left_index,
					InodeNo key, TreeCacheNode * right);
	Result insert_into_parent(TreeCacheNode * left, InodeNo key, TreeCacheNode * right);
	Result insert_into_new_root(TreeCacheNode * left, InodeNo key, TreeCacheNode * right);
	Result insert(Inode* value);

	Result start_new_tree();

	// Deletion.

	int get_neighbor_index(TreeCacheNode * n );
	Result adjust_root(TreeCacheNode * root);
	Result coalesce_nodes(TreeCacheNode * n, TreeCacheNode * neighbor, int neighbor_index, int k_prime);
	Result redistribute_nodes(TreeCacheNode * n, TreeCacheNode * neighbor, int neighbor_index,
					int k_prime_index, int k_prime);
	Result delete_entry(TreeCacheNode * n, InodeNo key);
	Result remove_entry_from_node(TreeCacheNode * n, InodeNo key);

};
}
#endif
