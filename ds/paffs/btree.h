/**
 * File created on 18.05.2016
 * Author: Pascal Pieper
 */

#ifndef __PAFFS_BTREE_H__
#define __PAFFS_BTREE_H__

#include <stdbool.h>
#include <stddef.h>
#include "paffs.h"

//Calculates how many pointers a node can hold in one page
#define BRANCH_ORDER ((512 - sizeof(p_addr)\
		- sizeof(unsigned char))\
		/ (sizeof(p_addr) + sizeof(pInode_no)) ) //todo: '512' Dynamisch machen

#define LEAF_ORDER ((512 - sizeof(p_addr)\
		- sizeof(unsigned char))\
		/ (sizeof(pInode) + sizeof(pInode_no)) ) //todo: '512' Dynamisch machen


static const int btree_branch_order = BRANCH_ORDER;
static const int btree_leaf_order = LEAF_ORDER;

typedef struct treeNode{
	union {
		struct as_branch {
			pInode_no keys[BRANCH_ORDER];
			p_addr pointers[BRANCH_ORDER];
		} as_branch;
		struct as_leaf {
			pInode_no keys[LEAF_ORDER];
			pInode pInodes[LEAF_ORDER];
		} as_leaf;
	};
	p_addr self;	//If '0', it is not committed yet
	bool is_leaf:1;
	unsigned char num_keys:7; //If leaf: Number of pInodes
							//If Branch: Number of addresses - 1
} treeNode;

typedef struct treeCacheNode{
	treeNode raw;
	struct treeCacheNode* parent;	//Parent either points to parent or to node itself if is root
	struct treeCacheNode* pointers[BRANCH_ORDER];	//pointer can be NULL if not cached yet
	bool dirty;
} treeCacheNode;



p_addr* getPointerAsAddr(char* pointers, unsigned int pos);
pInode* getPointerAsInode(char* pointers, unsigned int pos);
void insertAddrInPointer(char* pointers, p_addr* addr, unsigned int pos);
void insertInodeInPointer(char* pointers, pInode* inode, unsigned int pos);
PAFFS_RESULT updateAddrIntreeCacheNode(treeCacheNode* node, p_addr* old, p_addr* newAddress);


PAFFS_RESULT insertInode( p_dev* dev, pInode* inode);
PAFFS_RESULT getInode( p_dev* dev, pInode_no number, pInode* outInode);
PAFFS_RESULT updateExistingInode( p_dev* dev, pInode* inode);
PAFFS_RESULT deleteInode( p_dev* dev, pInode_no number);
PAFFS_RESULT findFirstFreeNo(p_dev* dev, pInode_no* outNumber);

bool isEqual(treeCacheNode* left, treeCacheNode* right);
int height( p_dev* dev, treeCacheNode * root );
//Length is number of Kanten, not Knoten
int length_to_root( p_dev* dev, treeCacheNode * child );
//Path is root first, child last
PAFFS_RESULT path_from_root( p_dev* dev, treeCacheNode * child, p_addr* path, unsigned int* lengthOut);
int find_range( p_dev* dev, treeCacheNode * root, pInode_no key_start, pInode_no key_end,
                int returned_keys[], void * returned_pointers[]); 
PAFFS_RESULT find_leaf(  p_dev* dev, pInode_no key, treeCacheNode** outtreeCacheNode);
PAFFS_RESULT find_in_leaf (treeCacheNode* leaf, pInode_no key, pInode* outInode);
PAFFS_RESULT find( p_dev* dev, pInode_no key, pInode* outInode);
int cut( int length );


// Insertion.

int get_left_index(treeCacheNode * parent, treeCacheNode * left);
PAFFS_RESULT insert_into_leaf( p_dev* dev, treeCacheNode * leaf, pInode * pointer );
PAFFS_RESULT insert_into_leaf_after_splitting(p_dev* dev, treeCacheNode * leaf, pInode * newInode);
PAFFS_RESULT insert_into_node(p_dev *dev, treeCacheNode * newNode,
        	int left_index, pInode_no key, treeCacheNode * right);
PAFFS_RESULT insert_into_node_after_splitting(p_dev* dev, treeCacheNode * old_node, int left_index,
                pInode_no key, treeCacheNode * right);
PAFFS_RESULT insert_into_parent(p_dev* dev, treeCacheNode * left, pInode_no key, treeCacheNode * right);
PAFFS_RESULT insert_into_new_root(p_dev* dev, treeCacheNode * left, pInode_no key, treeCacheNode * right);
PAFFS_RESULT insert( p_dev* dev, pInode* value);
PAFFS_RESULT insert_into_new_root(p_dev* dev, treeCacheNode * left, pInode_no key, treeCacheNode * right);
PAFFS_RESULT start_new_tree(p_dev* dev);

// Deletion.

int get_neighbor_index( treeCacheNode * n );
PAFFS_RESULT adjust_root(p_dev* dev, treeCacheNode * root);
PAFFS_RESULT coalesce_nodes(p_dev* dev, treeCacheNode * n, treeCacheNode * neighbor, int neighbor_index, int k_prime);
PAFFS_RESULT redistribute_nodes(p_dev* dev, treeCacheNode * n, treeCacheNode * neighbor, int neighbor_index,
                int k_prime_index, int k_prime);
PAFFS_RESULT delete_entry( p_dev* dev, treeCacheNode * n, pInode_no key);
PAFFS_RESULT remove_entry_from_node(p_dev* dev, treeCacheNode * n, pInode_no key);

void print_tree( p_dev* dev);
void print_leaves(p_dev* dev, treeCacheNode* c);
void print_keys(p_dev* dev, treeCacheNode* c);
//DEBUG


#endif
