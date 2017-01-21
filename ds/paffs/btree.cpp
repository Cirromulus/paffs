/**
 * File created on 18.05.2016
 * Author: Pascal Pieper
 */

#include "btree.hpp"
#include "paffs_flash.hpp"
#include "treequeue.hpp"
#include "treeCache.hpp"
#include <stdio.h>
#include <stdlib.h> 
#include <string.h>
#include <inttypes.h>

namespace paffs{

bool isTreeCacheNodeEqual(TreeCacheNode* left, TreeCacheNode* right){
	for(int i = 0; i <= left->raw.num_keys; i++)
		if(left->raw.as.branch.keys[i] != right->raw.as.branch.keys[i])
			return false;

	return true;
}


Result insertInode( Dev* dev, Inode* inode){
	return insert(dev, inode);
}

Result getInode( Dev* dev, InodeNo number, Inode* outInode){
	return find(dev, number, outInode);
}

Result updateExistingInode( Dev* dev, Inode* inode){
	PAFFS_DBG_S(PAFFS_TRACE_TREE, "Update existing inode n° %d", inode->no);

	TreeCacheNode *node = NULL;
	Result r = find_leaf(dev, inode->no, &node);
	if(r != Result::ok)
		return r;

	int pos;
	for(pos = 0; pos < node->raw.num_keys; pos++){
		if(node->raw.as.leaf.keys[pos] == inode->no)
			break;
	}

	if(pos == node->raw.num_keys){
		PAFFS_DBG(PAFFS_TRACE_ERROR, "Tried to update existing Inode %u, but could not find it!", inode->no);
		return Result::bug;	//This Key did not exist
	}

	node->raw.as.leaf.pInodes[pos] = *inode;
	node->dirty = true;

	//todo: check cache memory consumption, possibly flush
	return Result::ok;
}

Result deleteInode( Dev* dev, InodeNo number){

	Inode key;
	TreeCacheNode* key_leaf;

	Result r = find_leaf(dev, number, &key_leaf);
	if(r != Result::ok)
		return r;
	r = find_in_leaf (key_leaf, number, &key);
	if(r != Result::ok)
		return r;
	return delete_entry(dev, key_leaf, number);
}

Result findFirstFreeNo(Dev* dev, InodeNo* outNumber){
	TreeCacheNode *c = NULL;
	*outNumber = 0;
	Result r = getRootNodeFromCache(dev, &c);
	if(r != Result::ok)
		return r;
	while(!c->raw.is_leaf){
		r = getTreeNodeAtIndexFrom(dev, c->raw.num_keys, c, &c);
		if(r != Result::ok && r != Result::flushedcache)
			return r;
	}
	if(c->raw.num_keys > 0){
		*outNumber = c->raw.as.leaf.pInodes[c->raw.num_keys -1].no + 1;
	}
	return Result::ok;
}

/**
 * Compares addresses
 */
/*bool isEqual(TreeCacheNode* left, TreeCacheNode* right){
	return left->raw.self == right->raw.self;
}*/

/* Utility function to give the height
 * of the tree, which length in number of edges
 * of the path from the root to any leaf.
 */
int height( Dev* dev, TreeCacheNode * root ) {
        int h = 0;
        TreeCacheNode *curr = root;
        while (!curr->raw.is_leaf) {
                Result r = getTreeNodeAtIndexFrom(dev, 0, curr, &curr);
                if(r != Result::ok && r != Result::flushedcache){
                	lasterr = r;
                	return -1;
                }
                h++;
        }
        return h;
}


/* Utility function to give the length in edges
 * of the path from any TreeCacheNode to the root.
 */
int length_to_root( Dev* dev, TreeCacheNode * child ){
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
 * This function is used to build up cache to a given leaf after a cache clean.
 */
Result find_branch( Dev* dev, TreeCacheNode* target, TreeCacheNode** outtreeCacheNode) {
	int i = 0;
	TreeCacheNode *c = NULL;

	Result r = getRootNodeFromCache(dev, &c);
	if(r != Result::ok)
		return r;


	while (!isTreeCacheNodeEqual(c, target)) {

		i = 0;
		while (i < c->raw.num_keys) {
			if (target->raw.as.branch.keys[0] >= c->raw.as.branch.keys[i]) i++;
			else break;
		}

		//printf("%d ->\n", i);
		Result r = getTreeNodeAtIndexFrom(dev, i, c, &c);
		if(r != Result::ok && r != Result::flushedcache)
			return r;
	}

	*outtreeCacheNode = c;
	return Result::ok;
}

/* Traces the path from the root to a leaf, searching
 * by key.
 * Returns the leaf containing the given key.
 */
Result find_leaf( Dev* dev, InodeNo key, TreeCacheNode** outtreeCacheNode) {
	int i = 0;
	TreeCacheNode *c = NULL;

	Result r = getRootNodeFromCache(dev, &c);
	if(r != Result::ok)
		return r;

	unsigned int depth = 0;
	while (!c->raw.is_leaf) {
		depth++;
		if(depth >= TREENODECACHESIZE-1){	//-1 because one node is needed for insert functions.
			PAFFS_DBG(PAFFS_TRACE_ERROR, "Cache size (%d) too small for depth %d!", TREENODECACHESIZE, depth);
			return Result::lowmem;
		}

		i = 0;
		while (i < c->raw.num_keys) {
			if (key >= c->raw.as.branch.keys[i]) i++;
			else break;
		}

		Result r = getTreeNodeAtIndexFrom(dev, i, c, &c);
		if(r != Result::ok && r != Result::flushedcache)
			return r;
	}

	*outtreeCacheNode = c;
	return Result::ok;
}

Result find_in_leaf (TreeCacheNode* leaf, InodeNo key, Inode* outInode){
	int i;
    for (i = 0; i < leaf->raw.num_keys; i++)
            if (leaf->raw.as.leaf.keys[i] == key) break;
    if (i == leaf->raw.num_keys)
            return Result::nf;
	*outInode = leaf->raw.as.leaf.pInodes[i];
	return Result::ok;
}

/* Finds and returns the Inode to which
 * a key refers.
 */
Result find( Dev* dev, InodeNo key, Inode* outInode){
    TreeCacheNode *c = NULL;
    Result r = find_leaf( dev, key, &c);
    if(r != Result::ok)
    	return r;
    return find_in_leaf(c, key, outInode);
}

/* Finds the appropriate place to
 * split a TreeCacheNode that is too big into two.
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
 * the TreeCacheNode to the left of the key to be inserted.
 */
int get_left_index(TreeCacheNode * parent, TreeCacheNode * left) {
	int left_index = 0;
	while (left_index < parent->raw.num_keys){
		if(parent->raw.as.branch.pointers[left_index] != 0)
			if(parent->raw.as.branch.pointers[left_index] == left->raw.self)
				break;
		if(parent->pointers[left_index] != 0)
			if(parent->pointers[left_index] == left)
				break;
		left_index++;
	}
	return left_index;
}

/* Inserts a new pointer to a Inode and its corresponding
 * key into a leaf when it has enough space free.
 * (No further Tree-action Required)
 */
Result insert_into_leaf( Dev* dev, TreeCacheNode * leaf, Inode * newInode ) {

        int i, insertion_point;

        insertion_point = 0;
        while (insertion_point < leaf->raw.num_keys && leaf->raw.as.leaf.keys[insertion_point] < newInode->no)
                insertion_point++;

        for (i = leaf->raw.num_keys; i > insertion_point; i--) {
                leaf->raw.as.leaf.keys[i] = leaf->raw.as.leaf.keys[i - 1];
                leaf->raw.as.leaf.pInodes[i] = leaf->raw.as.leaf.pInodes[i - 1];
        }
        leaf->raw.as.leaf.keys[insertion_point] = newInode->no;
        leaf->raw.num_keys++;
        leaf->raw.as.leaf.pInodes[insertion_point] = *newInode;

        leaf->dirty = true;

        return Result::ok;
}


/* Inserts a new key and pointer
 * to a new Inode into a leaf so as to exceed
 * the tree's order, causing the leaf to be split
 * in half.
 */
Result insert_into_leaf_after_splitting(Dev* dev, TreeCacheNode * leaf, Inode * newInode) {

	PAFFS_DBG_S(PAFFS_TRACE_TREE, "Insert into leaf after splitting");
	InodeNo temp_keys[btree_leaf_order+1];
	Inode temp_pInodes[btree_leaf_order+1];
	int insertion_index, split, new_key, i, j;
	memset(temp_keys, 0, btree_leaf_order+1 * sizeof(InodeNo));
	memset(temp_pInodes, 0, btree_leaf_order+1 * sizeof(Inode));

	TreeCacheNode *new_leaf = NULL;

	lockTreeCacheNode(dev, leaf);
	Result r = addNewCacheNodeWithPossibleFlush(dev, &new_leaf);
	if(r == Result::flushedcache){

	}
	else if(r != Result::ok)
		return r;
	unlockTreeCacheNode(dev, leaf);

	new_leaf->raw.is_leaf = true;

	insertion_index = 0;
	while (insertion_index < btree_leaf_order && leaf->raw.as.leaf.keys[insertion_index] < newInode->no)
		insertion_index++;

	for (i = 0, j = 0; i < leaf->raw.num_keys; i++, j++) {
		if (j == insertion_index) j++;
		temp_keys[j] = leaf->raw.as.leaf.keys[i];
		temp_pInodes[j] = leaf->raw.as.leaf.pInodes[i];
	}

	temp_keys[insertion_index] = newInode->no;
	temp_pInodes[insertion_index] = *newInode;

	leaf->raw.num_keys = 0;

	split = cut(btree_leaf_order);

	for (i = 0; i < split; i++) {
		leaf->raw.as.leaf.pInodes[i] = temp_pInodes[i];
		leaf->raw.as.leaf.keys[i] = temp_keys[i];
		leaf->raw.num_keys++;
	}

	for (i = split, j = 0; i <= btree_leaf_order; i++, j++) {
		new_leaf->raw.as.leaf.pInodes[j] = temp_pInodes[i];
		new_leaf->raw.as.leaf.keys[j] = temp_keys[i];
		new_leaf->raw.num_keys++;
	}

	for (i = leaf->raw.num_keys; i < btree_leaf_order; i++){
		memset(&leaf->raw.as.leaf.pInodes[i], 0, sizeof(Inode));
		leaf->raw.as.leaf.keys[i] = 0;
	}
	for (i = new_leaf->raw.num_keys; i < btree_leaf_order; i++){
		memset(&new_leaf->raw.as.leaf.pInodes[i], 0, sizeof(Inode));
		new_leaf->raw.as.leaf.keys[i] = 0;
	}

	new_leaf->dirty = true;
	new_leaf->parent = leaf->parent;
	new_key = new_leaf->raw.as.leaf.keys[0];
	leaf->dirty = true;

	return insert_into_parent(dev, leaf, new_key, new_leaf);
}


/* Inserts a new key and pointer to a TreeCacheNode
 * into a TreeCacheNode into which these can fit
 * without violating the B+ tree properties.
 * (No further Tree-action Required)
 */
Result insert_into_node(Dev *dev, TreeCacheNode * node,
	int left_index, InodeNo key, TreeCacheNode * right) {
	int i;

	for (i = node->raw.num_keys; i > left_index; i--) {
		node->raw.as.branch.pointers[i + 1] = node->raw.as.branch.pointers[i];
		node->pointers[i + 1] = node->pointers[i];
		node->raw.as.branch.keys[i] = node->raw.as.branch.keys[i - 1];
	}
	node->raw.as.branch.pointers[left_index + 1] = right->raw.self;
	node->pointers[left_index + 1] = right;

	node->raw.as.branch.keys[left_index] = key;
	node->raw.num_keys++;
	node->dirty = true;
	right->parent = node;
	right->dirty = true;

	return Result::ok;
}


/* Inserts a new key and pointer to a TreeCacheNode
 * into a TreeCacheNode, causing the TreeCacheNode's size to exceed
 * the order, and causing the TreeCacheNode to split into two.
 */
Result insert_into_node_after_splitting(Dev* dev, TreeCacheNode * old_node, int left_index,
                InodeNo key, TreeCacheNode * right) {
	PAFFS_DBG_S(PAFFS_TRACE_TREE, "Insert into node after splitting at key %u, index %d", key, left_index);
	int i, j, split, k_prime;
	TreeCacheNode *new_node;
	InodeNo temp_keys[btree_branch_order+1];
	TreeCacheNode* temp_RAMaddresses[btree_branch_order+1];
	Addr temp_addresses[btree_branch_order+1];


	lockTreeCacheNode(dev, old_node);
	lockTreeCacheNode(dev, right);
	Result r = addNewCacheNodeWithPossibleFlush(dev, &new_node);
	if(r != Result::flushedcache && r != Result::ok)
		return r;
	unlockTreeCacheNode(dev, old_node);
	unlockTreeCacheNode(dev, right);

	/* First create a temporary set of keys and pointers
	 * to hold everything in order, including
	 * the new key and pointer, inserted in their
	 * correct places.
	 * Then create a new TreeCacheNode and copy half of the
	 * keys and pointers to the old TreeCacheNode and
	 * the other half to the new.
	 */
	for (i = 0, j = 0; i < old_node->raw.num_keys + 1; i++, j++) {
		if (j == left_index + 1) j++;
		temp_addresses[j] = old_node->raw.as.branch.pointers[i];
		temp_RAMaddresses[j] = old_node->pointers[i];
	}

	for (i = 0, j = 0; i < old_node->raw.num_keys; i++, j++) {
		if (j == left_index) j++;
		temp_keys[j] = old_node->raw.as.branch.keys[i];
	}

	temp_addresses[left_index + 1] = right->raw.self;
	temp_RAMaddresses[left_index + 1] = right;
	temp_keys[left_index] = key;

	/* Create the new TreeCacheNode and copy
	 * half the keys and pointers to the
	 * old and half to the new.
	 */
	split = cut(btree_branch_order);

	old_node->raw.num_keys = 0;
	for (i = 0; i < split - 1; i++) {
		old_node->raw.as.branch.pointers[i] = temp_addresses[i];
		old_node->pointers[i] = temp_RAMaddresses[i];
		old_node->raw.as.branch.keys[i] = temp_keys[i];
		old_node->raw.num_keys++;


	}
	old_node->raw.as.branch.pointers[i] = temp_addresses[i];
	old_node->pointers[i] = temp_RAMaddresses[i];
	k_prime = temp_keys[split - 1];
	for (++i, j = 0; i < btree_branch_order; i++, j++) {
		new_node->pointers[j] = temp_RAMaddresses[i];
		new_node->raw.as.branch.pointers[j] = temp_addresses[i];
		new_node->raw.as.branch.keys[j] = temp_keys[i];
		new_node->raw.num_keys++;
		if(new_node->pointers[j] != NULL)
			new_node->pointers[j]->parent = new_node;
		//cleanup
		old_node->pointers[i] = 0;
		old_node->raw.as.branch.pointers[i] = 0;
		if(i < btree_branch_order - 1)
			old_node->raw.as.branch.keys[i] = 0;
	}

	new_node->pointers[j] = temp_RAMaddresses[i];
	new_node->raw.as.branch.pointers[j] = temp_addresses[i];
	if(new_node->pointers[j] != NULL)
		new_node->pointers[j]->parent = new_node;
	new_node->parent = old_node->parent;

	old_node->dirty = true;
	new_node->dirty = true;

	/* Insert a new key into the parent of the two
	 * nodes resulting from the split, with
	 * the old TreeCacheNode to the left and the new to the right.
	 */

	return insert_into_parent(dev, old_node, k_prime, new_node);
}



/* Inserts a new TreeCacheNode (leaf or internal TreeCacheNode) into the B+ tree.
 * Returns the root of the tree after insertion.
 */
Result insert_into_parent(Dev* dev, TreeCacheNode * left, InodeNo key, TreeCacheNode * right) {

	int left_index;
	TreeCacheNode *parent = left->parent;

	if (left == parent)
		return insert_into_new_root(dev, left, key, right);


	left_index = get_left_index(parent, left);


	if (parent->raw.num_keys < btree_branch_order - 1)
			return insert_into_node(dev, parent, left_index, key, right);


	return insert_into_node_after_splitting(dev, parent, left_index, key, right);
}



/* Creates a new root for two subtrees
 * and inserts the appropriate key into
 * the new root.
 * COULD INITIATE A CACHE FLUSH
 */
Result insert_into_new_root(Dev* dev, TreeCacheNode * left, InodeNo key, TreeCacheNode * right) {
	PAFFS_DBG_S(PAFFS_TRACE_TREE, "Insert into new root at key %u", key);
	TreeCacheNode *new_root = NULL;
	lockTreeCacheNode(dev, left);
	lockTreeCacheNode(dev, right);
	Result r = addNewCacheNodeWithPossibleFlush(dev, &new_root);
	if(r != Result::ok && r != Result::flushedcache){
		return r;
	}
	unlockTreeCacheNode(dev, left);
	unlockTreeCacheNode(dev, right);

	new_root->raw.is_leaf = false;
	new_root->raw.as.branch.keys[0] = key;
	new_root->raw.as.branch.pointers[0] = left->raw.self;
	new_root->pointers[0] = left;
	left->parent = new_root;
	new_root->raw.as.branch.pointers[1] = right->raw.self;
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
Result start_new_tree(Dev* dev) {
	initCache();
	TreeCacheNode *new_root = NULL;
	Result r = addNewCacheNodeWithPossibleFlush(dev, &new_root);
	if(r != Result::ok)
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
Result insert( Dev* dev, Inode* value) {

	TreeCacheNode *node = NULL;
	Result r;

	/* The current implementation ignores
	 * duplicates.
	 */
	PAFFS_DBG_S(PAFFS_TRACE_TREE, "Insert Inode n° %d", value->no);

	r = find(dev, value->no, value);
	if(r == Result::ok){
		PAFFS_DBG(PAFFS_TRACE_BUG, "BUG: Inode already existing with n° %d", value->no);
		return Result::bug;
	}else if(r != Result::nf){
		return r;
	}

	/* Not really necessary */
	r = getRootNodeFromCache(dev, &node);
	if (r != Result::ok)
		return r;

	/**  rootnode not used  **/


	/* Case: the tree already exists.
	 * (Rest of function body.)
	 */

	r = find_leaf(dev, value->no, &node);
	if(r != Result::ok)
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
 * the index of a TreeCacheNode's nearest neighbor (sibling)
 * to the left if one exists.  If not (the TreeCacheNode
 * is the leftmost child), returns -1 to signify
 * this special case.
 */
int get_neighbor_index( TreeCacheNode * n ){

        int i;
        TreeCacheNode *parent = n->parent;

        for (i = 0; i <= parent->raw.num_keys; i++)
                if (parent->pointers[i] == n)		//It is allowed for all other pointers to be invalid
                        return i - 1;

        // Error state.
        return -1;
}


/**
 * Does not realign
 */
Result remove_entry_from_node(Dev* dev, TreeCacheNode * n, InodeNo key) {

	int i;

	// Remove the key and shift other keys accordingly.
	i = 0;
	while (n->raw.as.branch.keys[i] != key  && i < n->raw.num_keys)		//as.branch is OK, because it is same memory as as.leaf
		i++;
	if(key < n->raw.as.branch.keys[i-1]){
		PAFFS_DBG(PAFFS_TRACE_BUG, "Key to delete (%lu) not found!", (long unsigned) key);
		return Result::bug;
	}


	if(n->raw.is_leaf){
		for (++i; i < n->raw.num_keys; i++){
			n->raw.as.leaf.keys[i - 1] = n->raw.as.leaf.keys[i];
			n->raw.as.leaf.pInodes[i - 1] = n->raw.as.leaf.pInodes[i];
		}
	}else{
		for (++i; i < n->raw.num_keys; i++){
			n->raw.as.branch.keys[i - 1] = n->raw.as.branch.keys[i];
			n->raw.as.branch.pointers[i] = n->raw.as.branch.pointers[i + 1];
			n->pointers[i] = n->pointers[i + 1];
		}
	}


	// One key fewer.
	n->raw.num_keys--;

	// Set the other pointers to NULL for tidiness.
	if (n->raw.is_leaf)
		for (i = n->raw.num_keys; i < btree_leaf_order; i++){
			memset(&n->raw.as.leaf.pInodes[i], 0, sizeof(Inode));
			n->raw.as.leaf.keys[i] = 0;
		}
	else
		for (i = n->raw.num_keys + 1; i < btree_branch_order; i++){
			n->raw.as.branch.pointers[i] = 0;
			n->pointers[i] = NULL;
			n->raw.as.branch.keys[i - 1] = 0;
		}

	n->dirty = true;

	return Result::ok;
}


Result adjust_root(Dev* dev, TreeCacheNode * root) {

	/* Case: nonempty root.
	 * Key and pointer have already been deleted,
	 * so just commit dirty changes.
	 */

	if (root->raw.num_keys > 0)
			return Result::ok;

	/* Case: empty root.
	 */

	// If it has a child, promote
	// the first (only) child
	// as the new root.

	if (!root->raw.is_leaf) {
		root->pointers[0]->parent = root->pointers[0];
		Result r = setCacheRoot(dev, root->pointers[0]);
		if(r != Result::ok)
			return r;
		return removeCacheNode(dev, root);
	}

	// If it is a leaf (has no children),
	// then the whole tree is empty.

	return removeCacheNode(dev, root);
}


/* Coalesces a TreeCacheNode (n) that has become
 * too small after deletion
 * with a neighboring TreeCacheNode that
 * can accept the additional entries
 * without exceeding the maximum.
 */
Result coalesce_nodes(Dev* dev, TreeCacheNode * n, TreeCacheNode * neighbor, int neighbor_index, int k_prime) {

	int i, j, neighbor_insertion_index, n_end;
	TreeCacheNode *tmp;

	PAFFS_DBG_S(PAFFS_TRACE_TREE, "Coalesce nodes at %d", k_prime);

	/* Swap neighbor with TreeCacheNode if TreeCacheNode is on the
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

	/* Case:  nonleaf TreeCacheNode.
	 * Append k_prime and the following pointer.
	 * Append all pointers and keys from the neighbor.
	 */

	if (!n->raw.is_leaf) {

		/* Append k_prime.
		 */

		neighbor->raw.as.branch.keys[neighbor_insertion_index] = k_prime;
		neighbor->raw.num_keys++;


		n_end = n->raw.num_keys;

		for (i = neighbor_insertion_index + 1, j = 0; j < n_end; i++, j++) {
				neighbor->raw.as.branch.keys[i] = n->raw.as.branch.keys[j];
				neighbor->raw.as.branch.pointers[i] = n->raw.as.branch.pointers[j];
				neighbor->pointers[i] = n->pointers[j];
				neighbor->raw.num_keys++;
				n->raw.num_keys--;
		}

		/* The number of pointers is always
		 * one more than the number of keys.
		 */

		neighbor->raw.as.branch.pointers[i] = n->raw.as.branch.pointers[j];
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
			neighbor->raw.as.leaf.keys[i] = n->raw.as.leaf.keys[j];
			neighbor->raw.as.leaf.pInodes[i] = n->raw.as.leaf.pInodes[j];
			neighbor->raw.num_keys++;
		}
	}

	neighbor->dirty = true;

	Result r =  delete_entry(dev, n->parent, k_prime);
	if(r != Result::ok)
		return r;

	return removeCacheNode(dev, n);
}


/* Redistributes entries between two nodes when
 * one has become too small after deletion
 * but its neighbor is too big to append the
 * small TreeCacheNode's entries without exceeding the
 * maximum
 */
Result redistribute_nodes(Dev* dev, TreeCacheNode * n, TreeCacheNode * neighbor,
			int neighbor_index, int k_prime_index, int k_prime) {
	int i;

	PAFFS_DBG_S(PAFFS_TRACE_TREE, "Redistribute Nodes at k_prime %d", k_prime);
	/* Case: n has a neighbor to the left.
	 * Pull the neighbor's last key-pointer pair over
	 * from the neighbor's right end to n's left end.
	 */

	if (neighbor_index != -1) {
		if (!n->raw.is_leaf){
			n->raw.as.branch.pointers[n->raw.num_keys + 1] = n->raw.as.branch.pointers[n->raw.num_keys];
			n->pointers[n->raw.num_keys + 1] = n->pointers[n->raw.num_keys];
			for (i = n->raw.num_keys; i > 0; i--) {
				n->raw.as.branch.keys[i] = n->raw.as.branch.keys[i - 1];
				n->pointers[i] = n->pointers[i - 1];
				n->raw.as.branch.pointers[i] = n->raw.as.branch.pointers[i - 1];
			}
		}else{
			for (i = n->raw.num_keys; i > 0; i--) {
				n->raw.as.leaf.keys[i] = n->raw.as.leaf.keys[i - 1];
				n->raw.as.leaf.pInodes[i] = n->raw.as.leaf.pInodes[i - 1];
			}
		}

		if (!n->raw.is_leaf) {
			n->pointers[0] = neighbor->pointers[neighbor->raw.num_keys];	//getTreeNodeIndex not needed, NULL is also allowed
			n->raw.as.branch.pointers[0] = neighbor->raw.as.branch.pointers[neighbor->raw.num_keys];
			n->pointers[0]->parent = n;
			neighbor->pointers[neighbor->raw.num_keys] = NULL;
			neighbor->raw.as.branch.pointers[neighbor->raw.num_keys] = 0;
			n->raw.as.branch.keys[0] = k_prime;
			n->parent->raw.as.branch.keys[k_prime_index] = neighbor->raw.as.branch.keys[neighbor->raw.num_keys - 1];
		}
		else {
			n->raw.as.leaf.pInodes[0] = neighbor->raw.as.leaf.pInodes[neighbor->raw.num_keys - 1];
			memset(&neighbor->raw.as.leaf.pInodes[neighbor->raw.num_keys - 1], 0, sizeof(Inode));
			n->raw.as.leaf.keys[0] = neighbor->raw.as.leaf.keys[neighbor->raw.num_keys - 1];
			n->parent->raw.as.leaf.keys[k_prime_index] = n->raw.as.leaf.keys[0];
		}

	}

	/* Case: n is the leftmost child.
	 * Take a key-pointer pair from the neighbor to the right.
	 * Move the neighbor's leftmost key-pointer pair
	 * to n's rightmost position.
	 */

	else {
		if (n->raw.is_leaf) {
			n->raw.as.leaf.keys[n->raw.num_keys] = neighbor->raw.as.leaf.keys[0];
			n->raw.as.leaf.pInodes[n->raw.num_keys] = neighbor->raw.as.leaf.pInodes[0];
			n->parent->raw.as.leaf.keys[k_prime_index] = neighbor->raw.as.leaf.keys[1];
			for (i = 0; i < neighbor->raw.num_keys - 1; i++) {
				neighbor->raw.as.leaf.keys[i] = neighbor->raw.as.leaf.keys[i + 1];
				neighbor->raw.as.leaf.pInodes[i] = neighbor->raw.as.leaf.pInodes[i + 1];
			}
		}
		else {
			n->raw.as.branch.keys[n->raw.num_keys] = k_prime;
			n->pointers[n->raw.num_keys + 1] = neighbor->pointers[0];
			n->raw.as.branch.pointers[n->raw.num_keys + 1] = neighbor->raw.as.branch.pointers[0];
			n->pointers[n->raw.num_keys + 1]->parent = n;
			n->parent->raw.as.branch.keys[k_prime_index] = neighbor->raw.as.branch.keys[0];
			for (i = 0; i < neighbor->raw.num_keys - 1; i++) {
				neighbor->raw.as.branch.keys[i] = neighbor->raw.as.branch.keys[i + 1];
				neighbor->pointers[i] = neighbor->pointers[i + 1];
				neighbor->raw.as.branch.pointers[i] = neighbor->raw.as.branch.pointers[i + 1];
			}
		}

		if (!n->raw.is_leaf){
			neighbor->pointers[i] = neighbor->pointers[i + 1];
			neighbor->raw.as.branch.pointers[i] = neighbor->raw.as.branch.pointers[i + 1];
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

	return Result::ok;
}


/* Deletes an entry from the B+ tree.
 * Removes the Inode and its key and pointer
 * from the leaf, and then makes all appropriate
 * changes to preserve the B+ tree properties.
 */
Result delete_entry( Dev* dev, TreeCacheNode * n, InodeNo key){

	int min_keys;
	TreeCacheNode *neighbor = NULL;
	int neighbor_index;
	int k_prime_index, k_prime;
	int capacity;

	// Remove key and pointer from TreeCacheNode.

	Result r = remove_entry_from_node(dev, n, key);
	if(r != Result::ok)
		return r;

	/* Case:  deletion from root.
	 */

	if (n->parent == n)
		return adjust_root(dev, n);


	/* Case:  deletion from a TreeCacheNode below the root.
	 * (Rest of function body.)
	 */

	/* Determine minimum allowable size of TreeCacheNode,
	 * to be preserved after deletion.
	 */


	min_keys = n->raw.is_leaf ? cut(btree_leaf_order) : cut(btree_branch_order) - 1;

	/* Case:  TreeCacheNode stays at or above minimum.
	 * (The simple case.)
	 */

	if (n->raw.num_keys >= min_keys)
			return Result::ok;


	/* Case:  TreeCacheNode falls below minimum.
	 * Either coalescence or redistribution
	 * is needed.
	 */

	/* Find the appropriate neighbor TreeCacheNode with which
	 * to coalesce.
	 * Also find the key (k_prime) in the parent
	 * between the pointer to TreeCacheNode n and the pointer
	 * to the neighbor.
	 */

	neighbor_index = get_neighbor_index(n);
	k_prime_index = neighbor_index == -1 ? 0 : neighbor_index;
	k_prime = n->parent->raw.as.branch.keys[k_prime_index];
	r = neighbor_index == -1 ? getTreeNodeAtIndexFrom(dev, 1, n->parent, &neighbor) :
			getTreeNodeAtIndexFrom(dev, neighbor_index, n->parent, &neighbor);
	if(r != Result::ok && r != Result::flushedcache)
		return r;

	capacity = neighbor->raw.is_leaf ? btree_leaf_order : btree_branch_order -1;

	/* Coalescence. */

	if (neighbor->raw.num_keys + n->raw.num_keys <= capacity)
		return coalesce_nodes(dev, n, neighbor, neighbor_index, k_prime);

	/* Redistribution. */

	else
		return redistribute_nodes(dev, n, neighbor, neighbor_index, k_prime_index, k_prime);
}


/* Prints the B+ tree in the command
 * line in level (rank) order, with the
 * keys in each TreeCacheNode and the '|' symbol
 * to separate nodes.
 */
void print_tree( Dev* dev) {
	TreeCacheNode *n = NULL;
	Result r = getRootNodeFromCache(dev, &n);
	if(r != Result::ok){
		printf("%s!\n", err_msg(r));
		return;
	}
	print_keys(dev, n);
	//print_leaves(dev, &n);
}

/* Prints the bottom row of keys
 * of the tree (with their respective
 * pointers, if the verbose_output flag is set.
 */
void print_leaves(Dev* dev, TreeCacheNode* c) {
	if(c->raw.is_leaf){
		printf("| ");
		for(int i = 0; i < c->raw.num_keys; i++)
			printf("%" PRIu32 " ", (uint32_t) c->raw.as.leaf.pInodes[i].no);
	}else{
		for(int i = 0; i <= c->raw.num_keys; i++){
			TreeCacheNode *n = NULL;
			Result r = getTreeNodeAtIndexFrom(dev, i, c, &n);
			if(r != Result::ok && r != Result::flushedcache){
				printf("%s!\n", err_msg(r));
				return;
			}
			print_leaves(dev, n);
			fflush(stdout);
		}
	}
}

/**
 * This only works to depth 'n' if RAM cache is big enough to at least hold all nodes in Path to depth 'n-1'
 */
void print_queued_keys_r(Dev* dev, queue_s* q){
	queue_s* new_q = queue_new();
	printf("|");
	while(!queue_empty(q)){
		TreeCacheNode *n = (TreeCacheNode*) queue_dequeue(q);
		for(int i = 0; i <= n->raw.num_keys; i++){
			if(!n->raw.is_leaf){
				TreeCacheNode *nn = NULL;			//next node
				TreeCacheNode *n_cache = NULL;		//cache version of the copy of the former cache entry...

				//Build up cache to current branch.
				//This is not very efficient, but doing that once per branch would require
				//cache to fit all child nodes of the current branch.
				Result r = find_branch(dev, n, &n_cache);
				if(r != Result::ok){
					printf("%s!\n", err_msg(r));
					return;
				}
				r = getTreeNodeAtIndexFrom(dev, i, n_cache, &nn);
				if(r != Result::ok && r != Result::flushedcache){
					printf("%s!\n", err_msg(r));
					return;
				}
				TreeCacheNode* nn_copy = (TreeCacheNode*) malloc(sizeof(TreeCacheNode));
				*nn_copy = *nn;
				queue_enqueue(new_q, nn_copy);
				if(i == 0)
					printf(".");
				if(i < n->raw.num_keys) printf("%" PRIu32 ".", (uint32_t) n->raw.as.branch.keys[i]);
			}else{
				if(i == 0)
					printf(" ");
				if(i < n->raw.num_keys) printf("%" PRIu32 " ", (uint32_t) n->raw.as.leaf.keys[i]);
			}
		}
		printf("|");
		free(n);
	}
	printf("\n");
	queue_destroy(q);
	if(!queue_empty(new_q))
		print_queued_keys_r(dev, new_q);
	else
		queue_destroy(new_q);
}

void print_keys(Dev* dev, TreeCacheNode* c){
	queue_s* q = queue_new();
	TreeCacheNode* c_copy = (TreeCacheNode*) malloc(sizeof(TreeCacheNode));
	*c_copy = *c;
	queue_enqueue(q, c_copy);
	print_queued_keys_r(dev, q);
}

}
