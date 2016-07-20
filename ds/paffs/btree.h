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

// Default order is 4.
#define DEFAULT_ORDER 4

// Minimum order is necessarily 3.  We set the maximum
// order arbitrarily.  You may change the maximum order.
#define MIN_ORDER 3
#define MAX_ORDER 20


typedef struct node {
        void ** pointers;
        pInode_no * keys;
        struct node * parent;
        bool is_leaf;
        int num_keys;
        struct node * next; // Used for queue.
} node;




// GLOBALS.

/* The order determines the maximum and minimum
 * number of entries (keys and pointers) in any
 * node.  Every node has at most order - 1 keys and
 * at least (roughly speaking) half that number.
 * Every leaf has as many pointers to data as keys,
 * and every internal node has one more pointer
 * to a subtree than the number of keys.
 * This global variable is initialized to the
 * default value.
 */

static int order = DEFAULT_ORDER;


// FUNCTION PROTOTYPES.

void enqueue(node* queue, node * new_node );
node * dequeue( node* queue  );
int height( node * root );
int path_to_root( node * root, node * child );
void print_leaves( node * root,  bool verbose_output);
void print_tree( node * root,bool verbose_output );
void find_and_print(node * root, pInode_no key, bool verbose);
void find_and_print_range(node * root, pInode_no key_start, pInode_no key_end, bool verbose);
int find_range( node * root, pInode_no key_start, pInode_no key_end, bool verbose,
                int returned_keys[], void * returned_pointers[]); 
node * find_leaf( node * root, pInode_no key, bool verbose );
pInode * find_v( node * root, pInode_no key, bool verbose );
pInode * find( node * root, pInode_no key);
pInode_no find_first_free_key( node * root );
int cut( int length );

// Insertion.

pInode * make_pinode(pInode pn);
node * make_node( void );
node * make_leaf( void );
int get_left_index(node * parent, node * left);
node * insert_into_leaf( node * leaf, pInode_no key, pInode * pointer );
node * insert_into_leaf_after_splitting(node * root, node * leaf, pInode_no key, pInode * pointer);
node * insert_into_node(node * root, node * parent, 
                int left_index, pInode_no key, node * right);
node * insert_into_node_after_splitting(node * root, node * parent, int left_index, 
                pInode_no key, node * right);
node * insert_into_parent(node * root, node * left, pInode_no key, node * right);
node * insert_into_new_root(node * left, pInode_no key, node * right);
node * start_new_tree(pInode_no key, pInode * pointer);
node * insert( node * root, pInode_no key, pInode value);
node * insert_direct( node * root, pInode* inode);

// Deletion.

int get_neighbor_index( node * n );
node * adjust_root(node * root);
node * coalesce_nodes(node * root, node * n, node * neighbor, int neighbor_index, int k_prime);
node * redistribute_nodes(node * root, node * n, node * neighbor, int neighbor_index, 
                int k_prime_index, int k_prime);
node * delete_entry( node * root, node * n, pInode_no key, void * pointer );
node * delete( node * root, pInode_no key );
node * destroy_tree(node * root);

#endif
