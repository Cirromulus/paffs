/**
 * File created on 18.05.2016
 * Author: Pascal Pieper
 */

#ifndef __PAFFS_BTREE_H__
#define __PAFFS_BTREE_H__

#include <stdbool.h>
#include <stddef.h>
#include <stdlib.h>
#include "paffs.h"
#include "paffs_flash.h"

//Calculates how many pointers a node can hold in one page
#define MAX_ORDER ((512 - sizeof(p_addr)\
		- sizeof(unsigned int) - sizeof(p_addr))\
		/ (sizeof(p_addr) + sizeof(pInode_no) )) //todo: '512' Dynamisch machen


typedef struct treeNode {
        char pointers[MAX_ORDER * sizeof(p_addr)];
        pInode_no keys[MAX_ORDER];
        p_addr parent;
        bool is_leaf:1;
        unsigned int num_keys:31;
        p_addr next;
} treeNode;

static int btree_order = MAX_ORDER;

p_addr getPointerAsAddr(char* pointers, unsigned int pos);
pInode getPointerAsInode(char* pointers, unsigned int pos);
void insertAddrInPointer(char* pointers, p_addr* addr, unsigned int pos);
void insertInodeInPointer(char* pointers, pInode* inode, unsigned int pos);

// FUNCTION PROTOTYPES.

void enqueue(treeNode* queue, treeNode * new_node );
treeNode * dequeue( treeNode* queue  );
int height( treeNode * root );
int path_to_root( treeNode * root, treeNode * child );
void print_leaves( treeNode * root,  bool verbose_output);
void print_tree( treeNode * root,bool verbose_output );
void find_and_print(treeNode * root, pInode_no key, bool verbose);
void find_and_print_range(treeNode * root, pInode_no key_start, pInode_no key_end, bool verbose);
int find_range( treeNode * root, pInode_no key_start, pInode_no key_end, bool verbose,
                int returned_keys[], void * returned_pointers[]); 
treeNode * find_leaf( treeNode * root, pInode_no key, bool verbose );
pInode * find_v( treeNode * root, pInode_no key, bool verbose );
pInode * find( treeNode * root, pInode_no key);
pInode_no find_first_free_key( treeNode * root );
int cut( int length );

// Insertion.

pInode * make_pinode(pInode pn);
treeNode * make_node( void );
treeNode * make_leaf( void );
int get_left_index(treeNode * parent, treeNode * left);
treeNode * insert_into_leaf( treeNode * leaf, pInode_no key, pInode * pointer );
treeNode * insert_into_leaf_after_splitting(treeNode * root, treeNode * leaf, pInode_no key, pInode * pointer);
treeNode * insert_into_node(treeNode * root, treeNode * parent, 
                int left_index, pInode_no key, treeNode * right);
treeNode * insert_into_node_after_splitting(treeNode * root, treeNode * parent, int left_index, 
                pInode_no key, treeNode * right);
treeNode * insert_into_parent(treeNode * root, treeNode * left, pInode_no key, treeNode * right);
treeNode * insert_into_new_root(treeNode * left, pInode_no key, treeNode * right);
treeNode * start_new_tree(pInode_no key, pInode * pointer);
PAFFS_RESULT insertInode( pInode* inode);

// Deletion.

int get_neighbor_index( treeNode * n );
treeNode * adjust_root(treeNode * root);
treeNode * coalesce_nodes(treeNode * root, treeNode * n, treeNode * neighbor, int neighbor_index, int k_prime);
treeNode * redistribute_nodes(treeNode * root, treeNode * n, treeNode * neighbor, int neighbor_index, 
                int k_prime_index, int k_prime);
treeNode * delete_entry( treeNode * root, treeNode * n, pInode_no key, void * pointer );
treeNode * delete( treeNode * root, pInode_no key );
treeNode * destroy_tree(treeNode * root);

#endif
