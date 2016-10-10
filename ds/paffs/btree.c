/**
 * File created on 18.05.2016
 * Author: Pascal Pieper
 */

#include "btree.h"
#include "paffs_flash.h"
#include "treequeue.h"
#include "treeCache.h"
#include <stdio.h>
#include <stdlib.h> 
#include <string.h>
#include <inttypes.h>

bool isTreeCacheNodeEqual(treeCacheNode* left, treeCacheNode* right){
	for(int i = 0; i <= left->raw.num_keys; i++)
		if(left->raw.as_branch.keys[i] != right->raw.as_branch.keys[i])
			return false;

	return true;
}


PAFFS_RESULT insertInode( p_dev* dev, pInode* inode){
	return insert(dev, inode);
}

PAFFS_RESULT getInode( p_dev* dev, pInode_no number, pInode* outInode){
	return find(dev, number, outInode);
}

PAFFS_RESULT updateExistingInode( p_dev* dev, pInode* inode){
	treeCacheNode *node = NULL;
	PAFFS_RESULT r = find_leaf(dev, inode->no, &node);
	if(r != PAFFS_OK)
		return r;

	int pos;
	for(pos = 0; pos < node->raw.num_keys; pos++){
		if(node->raw.as_leaf.keys[pos] == inode->no)
			break;
	}

	if(pos == node->raw.num_keys)
		return PAFFS_BUG;	//This Key did not exist

	node->raw.as_leaf.pInodes[pos] = *inode;
	node->dirty = true;

	//todo: check cache memory consumption, possibly flush
	return PAFFS_OK;
}

PAFFS_RESULT deleteInode( p_dev* dev, pInode_no number){

	pInode key;
	treeCacheNode* key_leaf;

	PAFFS_RESULT r = find_leaf(dev, number, &key_leaf);
	if(r != PAFFS_OK)
		return r;
	r = find_in_leaf (key_leaf, number, &key);
	if(r != PAFFS_OK)
		return r;
	return delete_entry(dev, key_leaf, number);
}

PAFFS_RESULT findFirstFreeNo(p_dev* dev, pInode_no* outNumber){
	treeCacheNode *c = NULL;
	*outNumber = 0;
	PAFFS_RESULT r = getRootNodeFromCache(dev, &c);
	if(r != PAFFS_OK)
		return r;
	while(!c->raw.is_leaf){
		r = getTreeNodeAtIndexFrom(dev, c->raw.num_keys, c, &c);
		if(r != PAFFS_OK)
			return r;
	}
	if(c->raw.num_keys > 0){
		*outNumber = c->raw.as_leaf.pInodes[c->raw.num_keys -1].no + 1;
	}
	return PAFFS_OK;
}

/**
 * Compares addresses
 */
bool isEqual(treeCacheNode* left, treeCacheNode* right){
	return left->raw.self == right->raw.self;
}

/* Utility function to give the height
 * of the tree, which length in number of edges
 * of the path from the root to any leaf.
 */
int height( p_dev* dev, treeCacheNode * root ) {
        int h = 0;
        treeCacheNode *curr = root;
        while (!curr->raw.is_leaf) {
                PAFFS_RESULT r = getTreeNodeAtIndexFrom(dev, 0, curr, &curr);
                if(r != PAFFS_OK){
                	paffs_lasterr = r;
                	return -1;
                }
                h++;
        }
        return h;
}


/* Utility function to give the length in edges
 * of the path from any treeCacheNode to the root.
 */
int length_to_root( p_dev* dev, treeCacheNode * child ){
	unsigned int length;
	while(child->parent != child){
		length++;
		child = child->parent;
	}
	return length;
}

/* Traces the path from the root to a branch, searching
 * by key.
 * Returns the branch containing the given key.
 */
PAFFS_RESULT find_branch( p_dev* dev, treeCacheNode* target, treeCacheNode** outtreeCacheNode) {
	int i = 0;
	treeCacheNode *c = NULL;

	PAFFS_RESULT r = getRootNodeFromCache(dev, &c);
	if(r != PAFFS_OK)
		return r;


	while (!isTreeCacheNodeEqual(c, target)) {

		i = 0;
		while (i < c->raw.num_keys) {
			if (target->raw.as_branch.keys[0] >= c->raw.as_branch.keys[i]) i++;
			else break;
		}

		//printf("%d ->\n", i);
		PAFFS_RESULT r = getTreeNodeAtIndexFrom(dev, i, c, &c);
		if(r != PAFFS_OK)
			return r;
	}

	*outtreeCacheNode = c;
	return PAFFS_OK;
}

/* Traces the path from the root to a leaf, searching
 * by key.
 * Returns the leaf containing the given key.
 */
PAFFS_RESULT find_leaf( p_dev* dev, pInode_no key, treeCacheNode** outtreeCacheNode) {
	int i = 0;
	treeCacheNode *c = NULL;

	PAFFS_RESULT r = getRootNodeFromCache(dev, &c);
	if(r != PAFFS_OK)
		return r;


	while (!c->raw.is_leaf) {

		i = 0;
		while (i < c->raw.num_keys) {
			if (key >= c->raw.as_branch.keys[i]) i++;
			else break;
		}

		//printf("%d ->\n", i);
		PAFFS_RESULT r = getTreeNodeAtIndexFrom(dev, i, c, &c);
		if(r != PAFFS_OK)
			return r;
	}

	*outtreeCacheNode = c;
	return PAFFS_OK;
}

PAFFS_RESULT find_in_leaf (treeCacheNode* leaf, pInode_no key, pInode* outInode){
	int i;
    for (i = 0; i < leaf->raw.num_keys; i++)
            if (leaf->raw.as_leaf.keys[i] == key) break;
    if (i == leaf->raw.num_keys)
            return PAFFS_NF;
	*outInode = leaf->raw.as_leaf.pInodes[i];
	return PAFFS_OK;
}

/* Finds and returns the pinode to which
 * a key refers.
 */
PAFFS_RESULT find( p_dev* dev, pInode_no key, pInode* outInode){
    treeCacheNode *c = NULL;
    PAFFS_RESULT r = find_leaf( dev, key, &c);
    if(r != PAFFS_OK)
    	return r;
    return find_in_leaf(c, key, outInode);
}

/* Finds the appropriate place to
 * split a treeCacheNode that is too big into two.
 */
int cut( int length ) {
        if (length % 2 == 0)
                return length/2;
        else
                return length/2 + 1;
}


// INSERTION

/* Helper function used in insert_into_parent
 * to find the index of the parent's pointer to 
 * the treeCacheNode to the left of the key to be inserted.
 */
int get_left_index(treeCacheNode * parent, treeCacheNode * left) {
        int left_index = 0;
        while (left_index < parent->raw.num_keys &&
                        parent->raw.as_branch.pointers[left_index] != left->raw.self)
                left_index++;
        return left_index;
}

/* Inserts a new pointer to a pinode and its corresponding
 * key into a leaf when it has enough space free.
 * (No further Tree-action Required)
 */
PAFFS_RESULT insert_into_leaf( p_dev* dev, treeCacheNode * leaf, pInode * newInode ) {

        int i, insertion_point;

        insertion_point = 0;
        while (insertion_point < leaf->raw.num_keys && leaf->raw.as_leaf.keys[insertion_point] < newInode->no)
                insertion_point++;

        for (i = leaf->raw.num_keys; i > insertion_point; i--) {
                leaf->raw.as_leaf.keys[i] = leaf->raw.as_leaf.keys[i - 1];
                leaf->raw.as_leaf.pInodes[i] = leaf->raw.as_leaf.pInodes[i - 1];
        }
        leaf->raw.as_leaf.keys[insertion_point] = newInode->no;
        leaf->raw.num_keys++;
        leaf->raw.as_leaf.pInodes[insertion_point] = *newInode;

        leaf->dirty = true;

        return PAFFS_OK;
}


/* Inserts a new key and pointer
 * to a new pinode into a leaf so as to exceed
 * the tree's order, causing the leaf to be split
 * in half.
 */
PAFFS_RESULT insert_into_leaf_after_splitting(p_dev* dev, treeCacheNode * leaf, pInode * newInode) {

	pInode_no temp_keys[btree_leaf_order+1];
	pInode temp_pInodes[btree_leaf_order+1];
	int insertion_index, split, new_key, i, j;
	memset(temp_keys, 0, btree_leaf_order+1 * sizeof(pInode_no));
	memset(temp_pInodes, 0, btree_leaf_order+1 * sizeof(pInode));

	treeCacheNode *new_leaf = NULL;

	treeCacheNode leaf_c = *leaf;
	PAFFS_RESULT r = addNewCacheNodeWithPossibleFlush(dev, &new_leaf);
	if(r == PAFFS_FLUSHEDCACHE){
		//Read back nodes from flash
		r = buildUpCacheToNode(dev, &leaf_c, leaf);
		if(r != PAFFS_OK)
			return r;
	}
	else if(r != PAFFS_OK)
		return r;

	new_leaf->raw.is_leaf = true;
	new_leaf->dirty = true;

	insertion_index = 0;
	while (insertion_index < btree_leaf_order && leaf->raw.as_leaf.keys[insertion_index] < newInode->no)
		insertion_index++;

	for (i = 0, j = 0; i < leaf->raw.num_keys; i++, j++) {
		if (j == insertion_index) j++;
		temp_keys[j] = leaf->raw.as_leaf.keys[i];
		temp_pInodes[j] = leaf->raw.as_leaf.pInodes[i];
	}

	temp_keys[insertion_index] = newInode->no;
	temp_pInodes[insertion_index] = *newInode;

	leaf->raw.num_keys = 0;

	split = cut(btree_leaf_order);

	for (i = 0; i < split; i++) {
		leaf->raw.as_leaf.pInodes[i] = temp_pInodes[i];
		leaf->raw.as_leaf.keys[i] = temp_keys[i];
		leaf->raw.num_keys++;
	}

	for (i = split, j = 0; i <= btree_leaf_order; i++, j++) {
		new_leaf->raw.as_leaf.pInodes[j] = temp_pInodes[i];
		new_leaf->raw.as_leaf.keys[j] = temp_keys[i];
		new_leaf->raw.num_keys++;
	}

	/*Next cousin is no longer supported
	new_leaf.pointers[btree_branch_order - 1] = leaf->pointers[btree_branch_order - 1];
	leaf->pointers[btree_branch_order - 1] = new_leaf;
	*/

	for (i = leaf->raw.num_keys; i < btree_leaf_order; i++){
		memset(&leaf->raw.as_leaf.pInodes[i], 0, sizeof(pInode));
		leaf->raw.as_leaf.keys[i] = 0;
	}
	for (i = new_leaf->raw.num_keys; i < btree_leaf_order; i++){
		memset(&new_leaf->raw.as_leaf.pInodes[i], 0, sizeof(pInode));
		new_leaf->raw.as_leaf.keys[i] = 0;
	}

	new_leaf->parent = leaf->parent;
	new_key = new_leaf->raw.as_leaf.keys[0];

	return insert_into_parent(dev, leaf, new_key, new_leaf);
}


/* Inserts a new key and pointer to a treeCacheNode
 * into a treeCacheNode into which these can fit
 * without violating the B+ tree properties.
 * (No further Tree-action Required)
 */
PAFFS_RESULT insert_into_node(p_dev *dev, treeCacheNode * node,
	int left_index, pInode_no key, treeCacheNode * right) {
	int i;

	for (i = node->raw.num_keys; i > left_index; i--) {
		node->raw.as_branch.pointers[i + 1] = node->raw.as_branch.pointers[i];
		node->pointers[i + 1] = node->pointers[i];
		node->raw.as_branch.keys[i] = node->raw.as_branch.keys[i - 1];
	}
	node->raw.as_branch.pointers[left_index + 1] = right->raw.self;
	node->pointers[left_index + 1] = right;

	node->raw.as_branch.keys[left_index] = key;
	node->raw.num_keys++;
	node->dirty = true;
	right->parent = node;
	right->dirty = true;

	return PAFFS_OK;
}


/* Inserts a new key and pointer to a treeCacheNode
 * into a treeCacheNode, causing the treeCacheNode's size to exceed
 * the order, and causing the treeCacheNode to split into two.
 */
PAFFS_RESULT insert_into_node_after_splitting(p_dev* dev, treeCacheNode * old_node, int left_index,
                pInode_no key, treeCacheNode * right) {

	int i, j, split, k_prime;
	treeCacheNode *new_node;
	pInode_no temp_keys[btree_branch_order+1];
	treeCacheNode* temp_RAMaddresses[btree_branch_order+1];
	p_addr temp_addresses[btree_branch_order+1];


	treeCacheNode old_node_c = *old_node, right_c = *right;
	PAFFS_RESULT r = addNewCacheNodeWithPossibleFlush(dev, &new_node);
	if(r == PAFFS_FLUSHEDCACHE){
		//Read back nodes from flash
		r = buildUpCacheToNode(dev, &old_node_c, old_node);
		if(r != PAFFS_OK)
			return r;
		r = buildUpCacheToNode(dev, &right_c, right);
		if(r != PAFFS_OK)
			return r;
	}
	else if(r != PAFFS_OK)
		return r;

	/* First create a temporary set of keys and pointers
	 * to hold everything in order, including
	 * the new key and pointer, inserted in their
	 * correct places.
	 * Then create a new treeCacheNode and copy half of the
	 * keys and pointers to the old treeCacheNode and
	 * the other half to the new.
	 */
	for (i = 0, j = 0; i < old_node->raw.num_keys + 1; i++, j++) {
			if (j == left_index + 1) j++;
			temp_addresses[j] = old_node->raw.as_branch.pointers[i];
			temp_RAMaddresses[j] = old_node->pointers[i];
	}

	for (i = 0, j = 0; i < old_node->raw.num_keys; i++, j++) {
			if (j == left_index) j++;
			temp_keys[j] = old_node->raw.as_branch.keys[i];
	}

	temp_addresses[left_index + 1] = right->raw.self;
	temp_RAMaddresses[left_index + 1] = right;
	temp_keys[left_index] = key;

	/* Create the new treeCacheNode and copy
	 * half the keys and pointers to the
	 * old and half to the new.
	 */
	split = cut(btree_branch_order);

	old_node->raw.num_keys = 0;
	for (i = 0; i < split - 1; i++) {
		old_node->raw.as_branch.pointers[i] = temp_addresses[i];
		old_node->pointers[i] = temp_RAMaddresses[i];
		old_node->raw.as_branch.keys[i] = temp_keys[i];
		old_node->raw.num_keys++;
	}
	old_node->raw.as_branch.pointers[i] = temp_addresses[i];
	old_node->pointers[i] = temp_RAMaddresses[i];
	k_prime = temp_keys[split - 1];
	for (++i, j = 0; i < btree_branch_order; i++, j++) {
			new_node->pointers[j] = temp_RAMaddresses[i];
			new_node->raw.as_branch.pointers[j] = temp_addresses[i];
			new_node->raw.as_branch.keys[j] = temp_keys[i];
			new_node->raw.num_keys++;
	}
	new_node->pointers[j] = temp_RAMaddresses[i];
	new_node->raw.as_branch.pointers[j] = temp_addresses[i];

	new_node->parent = old_node->parent;

	old_node->dirty = true;
	new_node->dirty = true;

	/* Insert a new key into the parent of the two
	 * nodes resulting from the split, with
	 * the old treeCacheNode to the left and the new to the right.
	 */

	return insert_into_parent(dev, old_node, k_prime, new_node);
}



/* Inserts a new treeCacheNode (leaf or internal treeCacheNode) into the B+ tree.
 * Returns the root of the tree after insertion.
 */
PAFFS_RESULT insert_into_parent(p_dev* dev, treeCacheNode * left, pInode_no key, treeCacheNode * right) {

	int left_index;
	treeCacheNode *parent = left->parent;

	if (left == parent)
		return insert_into_new_root(dev, left, key, right);


	left_index = get_left_index(parent, left);


	if (parent->raw.num_keys < btree_branch_order)
			return insert_into_node(dev, parent, left_index, key, right);


	return insert_into_node_after_splitting(dev, parent, left_index, key, right);
}



/* Creates a new root for two subtrees
 * and inserts the appropriate key into
 * the new root.
 * COULD INITIATE A CACHE FLUSH
 */
PAFFS_RESULT insert_into_new_root(p_dev* dev, treeCacheNode * left, pInode_no key, treeCacheNode * right) {
	treeCacheNode *new_root = NULL;
	treeCacheNode left_c = *left, right_c = *right;
	PAFFS_RESULT r = addNewCacheNodeWithPossibleFlush(dev, &new_root);
	if(r == PAFFS_FLUSHEDCACHE){
		//Rescan for left and right treeCache node
		r = buildUpCacheToNode(dev, &left_c, left);
		if(r != PAFFS_OK)
			return r;
		r = buildUpCacheToNode(dev, &right_c, right);
		if(r != PAFFS_OK)
			return r;
	}else if (r != PAFFS_OK){
		return r;
	}

	new_root->raw.is_leaf = false;
	new_root->raw.as_branch.keys[0] = key;
	new_root->raw.as_branch.pointers[0] = left->raw.self;
	new_root->pointers[0] = left;
	left->parent = new_root;
	new_root->raw.as_branch.pointers[1] = right->raw.self;
	new_root->pointers[1] = right;
	right->parent = new_root;

	new_root->raw.num_keys = 1;
	new_root->dirty = true;
	new_root->parent = new_root;

	return setCacheRoot(dev, new_root);
}



/* start a new tree.
 * So init rootnode
 */
PAFFS_RESULT start_new_tree(p_dev* dev) {

	treeCacheNode *new_root = NULL;
	PAFFS_RESULT r = addNewCacheNodeWithPossibleFlush(dev, &new_root);
	if(r != PAFFS_OK)
		return r;
	new_root->raw.is_leaf = true;
	new_root->dirty = true;
	new_root->parent = new_root;
    return setCacheRoot(dev, new_root);
}



/* Master insertion function.
 * Inserts a key and an associated value into
 * the B+ tree, causing the tree to be adjusted
 * however necessary to maintain the B+ tree
 * properties.
 */
PAFFS_RESULT insert( p_dev* dev, pInode* value) {

	treeCacheNode *node = NULL;
	PAFFS_RESULT r;

	/* The current implementation ignores
	 * duplicates.
	 */


	r = find(dev, value->no, value);
	if(r == PAFFS_OK){
		PAFFS_DBG(PAFFS_TRACE_BUG, "BUG: Pinode already existing with n° %d", value->no);
		return PAFFS_BUG;
	}else if(r != PAFFS_NF){
		return r;
	}

	/* Not really necessary */
	r = getRootNodeFromCache(dev, &node);
	if (r != PAFFS_OK)
		return r;

	/**  rootnode not used  **/


	/* Case: the tree already exists.
	 * (Rest of function body.)
	 */

	r = find_leaf(dev, value->no, &node);
	if(r != PAFFS_OK)
		return r;

	/* Case: leaf has room for key and pointer.
	 */

	if (node->raw.num_keys < btree_leaf_order) {
			return insert_into_leaf(dev, node, value);
	}


	/* Case:  leaf must be split.
	 */

	return insert_into_leaf_after_splitting(dev, node, value);
}



// DELETION.

/* Utility function for deletion.  Retrieves
 * the index of a treeCacheNode's nearest neighbor (sibling)
 * to the left if one exists.  If not (the treeCacheNode
 * is the leftmost child), returns -1 to signify
 * this special case.
 */
int get_neighbor_index( treeCacheNode * n ){

        int i;
        treeCacheNode *parent = n->parent;

        for (i = 0; i <= parent->raw.num_keys; i++)
                if (parent->pointers[i] == n)		//It is allowed for all other pointers to be invalid
                        return i - 1;

        // Error state.
        return -1;
}


/**
 * Does not realign
 */
PAFFS_RESULT remove_entry_from_node(p_dev* dev, treeCacheNode * n, pInode_no key) {

	int i;

	// Remove the key and shift other keys accordingly.
	i = 0;
	while (n->raw.as_branch.keys[i] != key  && i < n->raw.num_keys)		//as_branch is OK, because it is same memory as as_leaf
		i++;
	if(key < n->raw.as_branch.keys[i-1]){
		PAFFS_DBG(PAFFS_BUG, "Key to delete (%lu) not found!", (long unsigned) key);
		return PAFFS_BUG;
	}


	if(n->raw.is_leaf){
		for (++i; i < n->raw.num_keys; i++){
			n->raw.as_leaf.keys[i - 1] = n->raw.as_leaf.keys[i];
			n->raw.as_leaf.pInodes[i - 1] = n->raw.as_leaf.pInodes[i];
		}
	}else{
		for (++i; i < n->raw.num_keys; i++){
			n->raw.as_branch.keys[i - 1] = n->raw.as_branch.keys[i];
			n->raw.as_branch.pointers[i] = n->raw.as_branch.pointers[i + 1];
			n->pointers[i] = n->pointers[i + 1];
		}
	}


	// One key fewer.
	n->raw.num_keys--;

	// Set the other pointers to NULL for tidiness.
	if (n->raw.is_leaf)
		for (i = n->raw.num_keys; i < btree_leaf_order; i++){
			memset(&n->raw.as_leaf.pInodes[i], 0, sizeof(pInode));
			n->raw.as_leaf.keys[i] = 0;
		}
	else
		for (i = n->raw.num_keys + 1; i < btree_branch_order; i++){
			n->raw.as_branch.pointers[i] = 0;
			n->pointers[i] = NULL;
			n->raw.as_branch.keys[i - 1] = 0;
		}

	n->dirty = true;

	return PAFFS_OK;
}


PAFFS_RESULT adjust_root(p_dev* dev, treeCacheNode * root) {

	/* Case: nonempty root.
	 * Key and pointer have already been deleted,
	 * so just commit dirty changes.
	 */

	if (root->raw.num_keys > 0)
			return PAFFS_OK;

	/* Case: empty root.
	 */

	// If it has a child, promote
	// the first (only) child
	// as the new root.

	if (!root->raw.is_leaf) {
		root->pointers[0]->parent = root->pointers[0];
		PAFFS_RESULT r = setCacheRoot(dev, root->pointers[0]);
		if(r != PAFFS_OK)
			return r;
		return removeCacheNode(dev, root);
	}

	// If it is a leaf (has no children),
	// then the whole tree is empty.

	return removeCacheNode(dev, root);
}


/* Coalesces a treeCacheNode (n) that has become
 * too small after deletion
 * with a neighboring treeCacheNode that
 * can accept the additional entries
 * without exceeding the maximum.
 */
PAFFS_RESULT coalesce_nodes(p_dev* dev, treeCacheNode * n, treeCacheNode * neighbor, int neighbor_index, int k_prime) {

	int i, j, neighbor_insertion_index, n_end;
	treeCacheNode *tmp;

	/* Swap neighbor with treeCacheNode if treeCacheNode is on the
	 * extreme left and neighbor is to its right.
	 */

	if (neighbor_index == -1) {
			tmp = n;
			n = neighbor;
			neighbor = tmp;
	}

	/* Starting point in the neighbor for copying
	 * keys and pointers from n.
	 * Recall that n and neighbor have swapped places
	 * in the special case of n being a leftmost child.
	 */

	neighbor_insertion_index = neighbor->raw.num_keys;

	/* Case:  nonleaf treeCacheNode.
	 * Append k_prime and the following pointer.
	 * Append all pointers and keys from the neighbor.
	 */

	if (!n->raw.is_leaf) {

		/* Append k_prime.
		 */

		neighbor->raw.as_branch.keys[neighbor_insertion_index] = k_prime;
		neighbor->raw.num_keys++;


		n_end = n->raw.num_keys;

		for (i = neighbor_insertion_index + 1, j = 0; j < n_end; i++, j++) {
				neighbor->raw.as_branch.keys[i] = n->raw.as_branch.keys[j];
				neighbor->raw.as_branch.pointers[i] = n->raw.as_branch.pointers[j];
				neighbor->pointers[i] = n->pointers[j];
				neighbor->raw.num_keys++;
				n->raw.num_keys--;
		}

		/* The number of pointers is always
		 * one more than the number of keys.
		 */

		neighbor->raw.as_branch.pointers[i] = n->raw.as_branch.pointers[j];
		neighbor->pointers[i] = n->pointers[j];


		/* All children must now point up to the same parent.
		 */

		for (i = 0; i < neighbor->raw.num_keys + 1; i++) {
			tmp = neighbor->pointers[i];
			tmp->parent = neighbor;
		}

	}

	/* In a leaf, append the keys and pointers of
	 * n to the neighbor.
	 */

	else {
		for (i = neighbor_insertion_index, j = 0; j < n->raw.num_keys; i++, j++) {
			neighbor->raw.as_leaf.keys[i] = n->raw.as_leaf.keys[j];
			neighbor->raw.as_leaf.pInodes[i] = n->raw.as_leaf.pInodes[j];
			neighbor->raw.num_keys++;
		}
	}

	neighbor->dirty = true;

	PAFFS_RESULT r =  delete_entry(dev, n->parent, k_prime);
	if(r != PAFFS_OK)
		return r;

	return removeCacheNode(dev, n);
}


/* Redistributes entries between two nodes when
 * one has become too small after deletion
 * but its neighbor is too big to append the
 * small treeCacheNode's entries without exceeding the
 * maximum
 */
PAFFS_RESULT redistribute_nodes(p_dev* dev, treeCacheNode * n, treeCacheNode * neighbor,
			int neighbor_index, int k_prime_index, int k_prime) {
	int i;


	/* Case: n has a neighbor to the left.
	 * Pull the neighbor's last key-pointer pair over
	 * from the neighbor's right end to n's left end.
	 */

	if (neighbor_index != -1) {
		if (!n->raw.is_leaf){
			n->raw.as_branch.pointers[n->raw.num_keys + 1] = n->raw.as_branch.pointers[n->raw.num_keys];
			n->pointers[n->raw.num_keys + 1] = n->pointers[n->raw.num_keys];
			for (i = n->raw.num_keys; i > 0; i--) {
				n->raw.as_branch.keys[i] = n->raw.as_branch.keys[i - 1];
				n->pointers[i] = n->pointers[i - 1];
				n->raw.as_branch.pointers[i] = n->raw.as_branch.pointers[i - 1];
			}
		}else{
			for (i = n->raw.num_keys; i > 0; i--) {
				n->raw.as_leaf.keys[i] = n->raw.as_leaf.keys[i - 1];
				n->raw.as_leaf.pInodes[i] = n->raw.as_leaf.pInodes[i - 1];
			}
		}

		if (!n->raw.is_leaf) {
			n->pointers[0] = neighbor->pointers[neighbor->raw.num_keys];
			n->raw.as_branch.pointers[0] = neighbor->raw.as_branch.pointers[neighbor->raw.num_keys];
			n->pointers[0]->parent = n;
			neighbor->pointers[neighbor->raw.num_keys] = NULL;
			neighbor->raw.as_branch.pointers[neighbor->raw.num_keys] = 0;
			n->raw.as_branch.keys[0] = k_prime;
			n->parent->raw.as_branch.keys[k_prime_index] = neighbor->raw.as_branch.keys[neighbor->raw.num_keys - 1];
		}
		else {
			n->raw.as_leaf.pInodes[0] = neighbor->raw.as_leaf.pInodes[neighbor->raw.num_keys - 1];
			memset(&neighbor->raw.as_leaf.pInodes[neighbor->raw.num_keys - 1], 0, sizeof(pInode));
			n->raw.as_leaf.keys[0] = neighbor->raw.as_leaf.keys[neighbor->raw.num_keys - 1];
			n->parent->raw.as_leaf.keys[k_prime_index] = n->raw.as_leaf.keys[0];
		}

	}

	/* Case: n is the leftmost child.
	 * Take a key-pointer pair from the neighbor to the right.
	 * Move the neighbor's leftmost key-pointer pair
	 * to n's rightmost position.
	 */

	else {
		if (n->raw.is_leaf) {
			n->raw.as_leaf.keys[n->raw.num_keys] = neighbor->raw.as_leaf.keys[0];
			n->raw.as_leaf.pInodes[n->raw.num_keys] = neighbor->raw.as_leaf.pInodes[0];
			n->parent->raw.as_leaf.keys[k_prime_index] = neighbor->raw.as_leaf.keys[1];
			for (i = 0; i < neighbor->raw.num_keys - 1; i++) {
				neighbor->raw.as_leaf.keys[i] = neighbor->raw.as_leaf.keys[i + 1];
				neighbor->raw.as_leaf.pInodes[i] = neighbor->raw.as_leaf.pInodes[i + 1];
			}
		}
		else {
			n->raw.as_branch.keys[n->raw.num_keys] = k_prime;
			n->pointers[n->raw.num_keys + 1] = neighbor->pointers[0];
			n->raw.as_branch.pointers[n->raw.num_keys + 1] = neighbor->raw.as_branch.pointers[0];
			n->pointers[n->raw.num_keys + 1]->parent = n;
			n->parent->raw.as_branch.keys[k_prime_index] = neighbor->raw.as_branch.keys[0];
			for (i = 0; i < neighbor->raw.num_keys - 1; i++) {
				neighbor->raw.as_branch.keys[i] = neighbor->raw.as_branch.keys[i + 1];
				neighbor->pointers[i] = neighbor->pointers[i + 1];
				neighbor->raw.as_branch.pointers[i] = neighbor->raw.as_branch.pointers[i + 1];
			}
		}

		if (!n->raw.is_leaf){
			neighbor->pointers[i] = neighbor->pointers[i + 1];
			neighbor->raw.as_branch.pointers[i] = neighbor->raw.as_branch.pointers[i + 1];
		}
	}

	/* n now has one more key and one more pointer;
	 * the neighbor has one fewer of each.
	 */

	n->raw.num_keys++;
	neighbor->raw.num_keys--;


	n->dirty = true;
	neighbor->dirty = true;
	n->parent->dirty = true;

	return PAFFS_OK;
}


/* Deletes an entry from the B+ tree.
 * Removes the pinode and its key and pointer
 * from the leaf, and then makes all appropriate
 * changes to preserve the B+ tree properties.
 */
PAFFS_RESULT delete_entry( p_dev* dev, treeCacheNode * n, pInode_no key){

	int min_keys;
	treeCacheNode *neighbor = NULL;
	int neighbor_index;
	int k_prime_index, k_prime;
	int capacity;

	// Remove key and pointer from treeCacheNode.

	PAFFS_RESULT r = remove_entry_from_node(dev, n, key);
	if(r != PAFFS_OK)
		return r;

	/* Case:  deletion from root.
	 */

	if (n->parent == n)
		return adjust_root(dev, n);


	/* Case:  deletion from a treeCacheNode below the root.
	 * (Rest of function body.)
	 */

	/* Determine minimum allowable size of treeCacheNode,
	 * to be preserved after deletion.
	 */


	min_keys = n->raw.is_leaf ? cut(btree_leaf_order) : cut(btree_branch_order) - 1;

	/* Case:  treeCacheNode stays at or above minimum.
	 * (The simple case.)
	 */

	if (n->raw.num_keys >= min_keys)
			return PAFFS_OK;


	/* Case:  treeCacheNode falls below minimum.
	 * Either coalescence or redistribution
	 * is needed.
	 */

	/* Find the appropriate neighbor treeCacheNode with which
	 * to coalesce.
	 * Also find the key (k_prime) in the parent
	 * between the pointer to treeCacheNode n and the pointer
	 * to the neighbor.
	 */

	neighbor_index = get_neighbor_index(n);
	k_prime_index = neighbor_index == -1 ? 0 : neighbor_index;
	k_prime = n->parent->raw.as_branch.keys[k_prime_index];
	r = neighbor_index == -1 ? getTreeNodeAtIndexFrom(dev, 1, n->parent, &neighbor) :
			getTreeNodeAtIndexFrom(dev, neighbor_index, n->parent, &neighbor);
	if(r != PAFFS_OK)
		return r;

	capacity = neighbor->raw.is_leaf ? btree_leaf_order : btree_branch_order;	//-1?

	/* Coalescence. */

	if (neighbor->raw.num_keys + n->raw.num_keys <= capacity)
		return coalesce_nodes(dev, n, neighbor, neighbor_index, k_prime);

	/* Redistribution. */

	else
		return redistribute_nodes(dev, n, neighbor, neighbor_index, k_prime_index, k_prime);
}


/* Prints the B+ tree in the command
 * line in level (rank) order, with the
 * keys in each treeCacheNode and the '|' symbol
 * to separate nodes.
 */
void print_tree( p_dev* dev) {
	treeCacheNode *n = NULL;
	PAFFS_RESULT r = getRootNodeFromCache(dev, &n);
	if(r != PAFFS_OK){
		printf("%s!\n", paffs_err_msg(r));
		return;
	}
	print_keys(dev, n);
	//print_leaves(dev, &n);
}

/* Prints the bottom row of keys
 * of the tree (with their respective
 * pointers, if the verbose_output flag is set.
 */
void print_leaves(p_dev* dev, treeCacheNode* c) {
	if(c->raw.is_leaf){
		printf("| ");
		for(int i = 0; i < c->raw.num_keys; i++)
			printf("%" PRIu32 " ", (uint32_t) c->raw.as_leaf.pInodes[i].no);
	}else{
		for(int i = 0; i <= c->raw.num_keys; i++){
			treeCacheNode *n = NULL;
			PAFFS_RESULT r = getTreeNodeAtIndexFrom(dev, i, c, &n);
			if(r != PAFFS_OK){
				printf("%s!\n", paffs_err_msg(r));
				return;
			}
			print_leaves(dev, n);
			fflush(stdout);
		}
	}
}

/**
 * Function only valid if whole tree fits in cache due to memory footprint reduce
 */
void print_queued_keys_r(p_dev* dev, queue_s* q){
	queue_s* new_q = queue_new();
	printf("|");
	while(!queue_empty(q)){
		treeCacheNode *n = queue_dequeue(q);
		for(int i = 0; i <= n->raw.num_keys; i++){
			if(!n->raw.is_leaf){
				treeCacheNode *nn = NULL;
				PAFFS_RESULT r = getTreeNodeAtIndexFrom(dev, i, n, &nn);
				if(r != PAFFS_OK){
					printf("%s!\n", paffs_err_msg(r));
					return;
				}
				queue_enqueue(new_q, nn);
				if(i == 0)
					printf(".");
				if(i < n->raw.num_keys) printf("%" PRIu32 ".", (uint32_t) n->raw.as_branch.keys[i]);
			}else{
				if(i == 0)
					printf(" ");
				if(i < n->raw.num_keys) printf("%" PRIu32 " ", (uint32_t) n->raw.as_leaf.keys[i]);
			}
		}
		printf("|");
	}
	printf("\n");
	queue_destroy(q);
	if(!queue_empty(new_q))
		print_queued_keys_r(dev, new_q);
}

void print_keys(p_dev* dev, treeCacheNode* c){
	queue_s* q = queue_new();
	queue_enqueue(q, c);
	print_queued_keys_r(dev, q);
}

