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
#define MAX_ORDER ((512 - sizeof(p_addr)- sizeof(p_addr)\
		- sizeof(unsigned int))\
		/ (sizeof(p_addr) + sizeof(pInode_no) )) //todo: '512' Dynamisch machen


typedef struct treeNode {
        char pointers[MAX_ORDER * sizeof(p_addr)];
        pInode_no keys[MAX_ORDER];
        p_addr parent;		//parent == 0 if rootNode
        p_addr self;
        bool is_leaf:1;
        unsigned int num_keys:31;
} treeNode;

static int btree_order = MAX_ORDER;

p_addr* getPointerAsAddr(char* pointers, unsigned int pos);
pInode* getPointerAsInode(char* pointers, unsigned int pos);
void insertAddrInPointer(char* pointers, p_addr* addr, unsigned int pos);
void insertInodeInPointer(char* pointers, pInode* inode, unsigned int pos);


PAFFS_RESULT insertInode( p_dev* dev, pInode* inode);
PAFFS_RESULT getInode( p_dev* dev, pInode_no number, pInode* outInode);
PAFFS_RESULT modifyInode( p_dev* dev, pInode* inode);
PAFFS_RESULT deleteInode( p_dev* dev, pInode_no number);
pInode_no findFirstFreeNo(p_dev* dev);

int height( p_dev* dev, treeNode * root );
int path_to_root( p_dev* dev, treeNode * root, treeNode * child );
void print_leaves( p_dev* dev, treeNode * root);
void print_tree( p_dev* dev, treeNode * root);
void find_and_print(p_dev* dev, treeNode * root, pInode_no key);
void find_and_print_range(p_dev* dev, treeNode * root, pInode_no key_start, pInode_no key_end);
int find_range( p_dev* dev, treeNode * root, pInode_no key_start, pInode_no key_end,
                int returned_keys[], void * returned_pointers[]); 
PAFFS_RESULT find_leaf( p_dev* dev, treeNode * root, pInode_no key, treeNode* outTreenode);
PAFFS_RESULT find_in_leaf (treeNode* leaf, pInode_no key, pInode* outInode);
PAFFS_RESULT find( p_dev* dev, treeNode * root, pInode_no key, pInode* outInode);
PAFFS_RESULT find_first_free_key( p_dev* dev, treeNode * root, pInode_no* outNumber);
int cut( int length );
PAFFS_RESULT updateTreeNode( p_dev* dev, treeNode* node);


// Insertion.

int get_left_index(treeNode * parent, treeNode * left);
PAFFS_RESULT insert_into_leaf( p_dev* dev, treeNode * leaf, pInode_no key, pInode * pointer );
PAFFS_RESULT insert_into_leaf_after_splitting(p_dev* dev, treeNode * root, treeNode * leaf, pInode_no key, pInode * pointer);
PAFFS_RESULT insert_into_node(p_dev *dev, treeNode * newNode,
        int left_index, pInode_no key, treeNode * right);
PAFFS_RESULT insert_into_node_after_splitting(p_dev* dev, treeNode * root, treeNode * parent, int left_index,
                pInode_no key, treeNode * right);
PAFFS_RESULT insert_into_parent(p_dev* dev, treeNode * root, treeNode * left, pInode_no key, treeNode * right);
PAFFS_RESULT insert_into_new_root(p_dev* dev, treeNode * left, pInode_no key, treeNode * right);
PAFFS_RESULT start_new_tree(p_dev* dev, pInode_no key, pInode * pointer);

// Deletion.

int get_neighbor_index( p_dev* dev, treeNode * n );
PAFFS_RESULT adjust_root(p_dev* dev, treeNode * root);
PAFFS_RESULT coalesce_nodes(p_dev* dev, treeNode * root, treeNode * n, treeNode * neighbor, int neighbor_index, int k_prime);
PAFFS_RESULT redistribute_nodes(p_dev* dev, treeNode * root, treeNode * n, treeNode * neighbor, int neighbor_index,
                int k_prime_index, int k_prime);
PAFFS_RESULT delete_entry( p_dev* dev, treeNode * root, treeNode * n, pInode_no key, void * pointer );
PAFFS_RESULT delete_node( p_dev* dev, treeNode * root, pInode_no key );
PAFFS_RESULT destroy_tree(p_dev* dev, treeNode * root);

#endif
