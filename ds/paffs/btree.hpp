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



Addr* getPointerAsAddr(char* pointers, unsigned int pos);
Inode* getPointerAsInode(char* pointers, unsigned int pos);
void insertAddrInPointer(char* pointers, Addr* addr, unsigned int pos);
void insertInodeInPointer(char* pointers, Inode* inode, unsigned int pos);
Result updateAddrInTreeCacheNode(TreeCacheNode* node, Addr* old, Addr* newAddress);
bool isTreeCacheNodeEqual(TreeCacheNode* left, TreeCacheNode* right);


Result insertInode( Dev* dev, Inode* inode);
Result getInode( Dev* dev, InodeNo number, Inode* outInode);
Result updateExistingInode( Dev* dev, Inode* inode);
Result deleteInode( Dev* dev, InodeNo number);
Result findFirstFreeNo(Dev* dev, InodeNo* outNumber);

//bool isEqual(TreeCacheNode* left, TreeCacheNode* right);
int height( Dev* dev, TreeCacheNode * root );
//Length is number of Kanten, not Knoten
int length_to_root( Dev* dev, TreeCacheNode * child );
//Path is root first, child last
Result path_from_root( Dev* dev, TreeCacheNode * child, Addr* path, unsigned int* lengthOut);
int find_range( Dev* dev, TreeCacheNode * root, InodeNo key_start, InodeNo key_end,
                int returned_keys[], void * returned_pointers[]); 
Result find_branch(  Dev* dev, TreeCacheNode* target, TreeCacheNode** outtreeCacheNode);
Result find_leaf(  Dev* dev, InodeNo key, TreeCacheNode** outtreeCacheNode);
Result find_in_leaf (TreeCacheNode* leaf, InodeNo key, Inode* outInode);
Result find( Dev* dev, InodeNo key, Inode* outInode);
int cut( int length );


// Insertion.

int get_left_index(TreeCacheNode * parent, TreeCacheNode * left);
Result insert_into_leaf( Dev* dev, TreeCacheNode * leaf, Inode * pointer );
Result insert_into_leaf_after_splitting(Dev* dev, TreeCacheNode * leaf, Inode * newInode);
Result insert_into_node(Dev *dev, TreeCacheNode * newNode,
        	int left_index, InodeNo key, TreeCacheNode * right);
Result insert_into_node_after_splitting(Dev* dev, TreeCacheNode * old_node, int left_index,
                InodeNo key, TreeCacheNode * right);
Result insert_into_parent(Dev* dev, TreeCacheNode * left, InodeNo key, TreeCacheNode * right);
Result insert_into_new_root(Dev* dev, TreeCacheNode * left, InodeNo key, TreeCacheNode * right);
Result insert( Dev* dev, Inode* value);
Result insert_into_new_root(Dev* dev, TreeCacheNode * left, InodeNo key, TreeCacheNode * right);
Result start_new_tree(Dev* dev);

// Deletion.

int get_neighbor_index( TreeCacheNode * n );
Result adjust_root(Dev* dev, TreeCacheNode * root);
Result coalesce_nodes(Dev* dev, TreeCacheNode * n, TreeCacheNode * neighbor, int neighbor_index, int k_prime);
Result redistribute_nodes(Dev* dev, TreeCacheNode * n, TreeCacheNode * neighbor, int neighbor_index,
                int k_prime_index, int k_prime);
Result delete_entry( Dev* dev, TreeCacheNode * n, InodeNo key);
Result remove_entry_from_node(Dev* dev, TreeCacheNode * n, InodeNo key);

void print_tree( Dev* dev);
void print_leaves(Dev* dev, TreeCacheNode* c);
void print_keys(Dev* dev, TreeCacheNode* c);
//DEBUG

}
#endif
