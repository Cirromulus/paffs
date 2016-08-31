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
		- sizeof(unsigned char) - sizeof(bool))\
		/ (sizeof(p_addr) + sizeof(pInode_no)) ) //todo: '512' Dynamisch machen

#define LEAF_ORDER (BRANCH_ORDER * sizeof(p_addr) / sizeof(pInode))	 //todo: '512' Dynamisch machen


typedef struct treeNode {
        char pointers[BRANCH_ORDER * sizeof(p_addr)];
        pInode_no keys[BRANCH_ORDER];	//Fixme: Unused space as leaf, useful for another pInode
        p_addr self;
        bool is_leaf:1;
        unsigned char num_keys;		//If Branch: count of addresses - 1, if leaf: count of pInodes
} treeNode;

static int btree_branch_order = BRANCH_ORDER;
static int btree_leaf_order = LEAF_ORDER;

p_addr* getPointerAsAddr(char* pointers, unsigned int pos);
pInode* getPointerAsInode(char* pointers, unsigned int pos);
void insertAddrInPointer(char* pointers, p_addr* addr, unsigned int pos);
void insertInodeInPointer(char* pointers, pInode* inode, unsigned int pos);
PAFFS_RESULT updateAddrInTreenode(treeNode* node, p_addr* old, p_addr* newAddress);


PAFFS_RESULT insertInode( p_dev* dev, pInode* inode);
PAFFS_RESULT getInode( p_dev* dev, pInode_no number, pInode* outInode);
PAFFS_RESULT updateExistingInode( p_dev* dev, pInode* inode);
PAFFS_RESULT deleteInode( p_dev* dev, pInode_no number);
PAFFS_RESULT findFirstFreeNo(p_dev* dev, pInode_no* outNumber);

bool isEqual(treeNode* left, treeNode* right);
int height( p_dev* dev, treeNode * root );
//Length is number of Kanten, not Knoten
int length_to_root( p_dev* dev, treeNode * child );
//Path is root first, child last
PAFFS_RESULT path_from_root( p_dev* dev, treeNode * child, p_addr* path, unsigned int* lengthOut);
//ParentOut is NULL when there is no Parent (= root)
PAFFS_RESULT getParent(p_dev* dev, treeNode * node, treeNode* parentOut);
int find_range( p_dev* dev, treeNode * root, pInode_no key_start, pInode_no key_end,
                int returned_keys[], void * returned_pointers[]); 
PAFFS_RESULT find_leaf( p_dev* dev, pInode_no key, treeNode* outTreenode);
PAFFS_RESULT find_in_leaf (treeNode* leaf, pInode_no key, pInode* outInode);
PAFFS_RESULT find( p_dev* dev, pInode_no key, pInode* outInode);
int cut( int length );
PAFFS_RESULT updateTreeNodePath( p_dev* dev, treeNode* node);


// Insertion.

int get_left_index(treeNode * parent, treeNode * left);
PAFFS_RESULT insert_into_leaf( p_dev* dev, treeNode * leaf, pInode * pointer );
PAFFS_RESULT insert_into_leaf_after_splitting(p_dev* dev, treeNode * leaf, pInode * newInode);
PAFFS_RESULT insert_into_node(p_dev *dev, treeNode * newNode,
        int left_index, pInode_no key, treeNode * right);
PAFFS_RESULT insert_into_node_after_splitting(p_dev* dev, treeNode * old_node, int left_index,
                pInode_no key, treeNode * right);
PAFFS_RESULT insert_into_former_parent(p_dev* dev, treeNode * formerParent, treeNode * left, pInode_no key, treeNode * right);
PAFFS_RESULT insert_into_new_root(p_dev* dev, treeNode * left, pInode_no key, treeNode * right);
PAFFS_RESULT insert( p_dev* dev, pInode* value);
PAFFS_RESULT insert_into_new_root(p_dev* dev, treeNode * left, pInode_no key, treeNode * right);
PAFFS_RESULT start_new_tree(p_dev* dev);

// Deletion.

int get_neighbor_index( p_dev* dev, treeNode * n );
PAFFS_RESULT adjust_root(p_dev* dev, treeNode * root);
PAFFS_RESULT coalesce_nodes(p_dev* dev, treeNode * n, treeNode * neighbor, int neighbor_index, int k_prime);
PAFFS_RESULT redistribute_nodes(p_dev* dev, treeNode * n, treeNode * neighbor, treeNode* parent, int neighbor_index,
                int k_prime_index, int k_prime);
PAFFS_RESULT delete_entry( p_dev* dev, treeNode * n, pInode_no key);
PAFFS_RESULT remove_entry_from_node(p_dev* dev, treeNode * n, pInode_no key);

void print_tree( p_dev* dev);
void print_leaves(p_dev* dev, treeNode* c);
void print_keys(p_dev* dev, treeNode* c);
//DEBUG


#endif
