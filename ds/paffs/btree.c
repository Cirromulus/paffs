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


static int btree_branch_order = BRANCH_ORDER;
static int btree_leaf_order = LEAF_ORDER-1;



PAFFS_RESULT insertInode( p_dev* dev, pInode* inode){
	return insert(dev, inode);
}

PAFFS_RESULT getInode( p_dev* dev, pInode_no number, pInode* outInode){
	return find(dev, number, outInode);
}

PAFFS_RESULT updateExistingInode( p_dev* dev, pInode* inode){
	treeCacheNode node;
	PAFFS_RESULT r = find_leaf(dev, inode->no, &node);
	if(r != PAFFS_OK)
		return r;

	int pos;
	for(pos = 0; pos < node.raw.num_keys; pos++){
		if(node.raw.as_leaf.keys[pos] == inode->no)
			break;
	}

	if(pos == node.raw.num_keys)
		return PAFFS_BUG;	//This Key did not exist

	//todo: update node

	//todo: check cache memory consumption, possibly flush
	return PAFFS_NIMPL;
}

PAFFS_RESULT deleteInode( p_dev* dev, pInode_no number){

	pInode key;
	treeCacheNode key_leaf;

	//Safety or Speed ? Search is not really necessary
	PAFFS_RESULT r = find_leaf(dev, number, &key_leaf);
	if(r != PAFFS_OK)
		return r;
	r = find_in_leaf (&key_leaf, number, &key);
	if(r != PAFFS_OK)
		return r;
	return delete_entry(dev, &key_leaf, number);
}

PAFFS_RESULT findFirstFreeNo(p_dev* dev, pInode_no* outNumber){
	treeCacheNode c;
	*outNumber = 0;
	PAFFS_RESULT r = readtreeCacheNode(dev, getRootnodeAddr(dev), &c);
	if(r != PAFFS_OK)
		return r;
	while(!c.is_leaf){
		PAFFS_RESULT r = readtreeCacheNode(dev, *getPointerAsAddr(c.pointers, c.num_keys), &c);
		if(r != PAFFS_OK)
			return r;
	}
	if(c.num_keys > 0){
		*outNumber = getPointerAsInode(c.pointers, c.num_keys -1)->no + 1;
	}
	return PAFFS_OK;
}

/**
 * Compares addresses
 */
bool isEqual(treeCacheNode* left, treeCacheNode* right){
	return left->self == right->self;
}

/* Utility function to give the height
 * of the tree, which length in number of edges
 * of the path from the root to any leaf.
 */
int height( p_dev* dev, treeCacheNode * root ) {
        int h = 0;
        treeCacheNode curr = *root;
        while (!curr.is_leaf) {
        		p_addr* addr = getPointerAsAddr(curr.pointers, 0);
                PAFFS_RESULT r = readtreeCacheNode(dev, *addr, &curr);
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

/* Traces the path from the root to a leaf, searching
 * by key.  Displays information about the path
 * if the verbose flag is set.
 * Returns the leaf containing the given key.
 */
PAFFS_RESULT find_leaf( p_dev* dev, pInode_no key, treeCacheNode* outtreeCacheNode) {
	int i = 0;
	treeCacheNode *c;
	memset(&c, 0, sizeof(treeCacheNode));

	PAFFS_RESULT r = getRootNodeFromCache(dev, c);
	if(r != PAFFS_OK)
		return r;


	while (!c->raw.is_leaf) {

		i = 0;
		while (i < c->raw.num_keys) {
			if (key >= c->raw.as_branch.keys[i]) i++;
			else break;
		}

		//printf("%d ->\n", i);
		PAFFS_RESULT r = gettreeCacheNodeAtIndexFrom(dev, i, c, c);
		if(r != PAFFS_OK)
			return r;
	}

	outtreeCacheNode = c;
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
    treeCacheNode *c;
    PAFFS_RESULT r = find_leaf( dev, key, c);
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

/* Creates a new general treeCacheNode, which can be adapted
 * to serve as either a leaf or an internal treeCacheNode.
 *
treeCacheNode * make_node( void ) {
        treeCacheNode * new_node;
        new_node = malloc(sizeof(treeCacheNode));
        if (new_node == NULL) {
                perror("treeCacheNode creation.");
                return NULL;
        }
        new_node->keys = malloc( (btree_branch_order - 1) * sizeof(int) );
        if (new_node->keys == NULL) {
                perror("New treeCacheNode keys array.");
                return NULL;
        }
        new_node->pointers = malloc( btree_branch_order * sizeof(void *) );
        if (new_node->pointers == NULL) {
                perror("New treeCacheNode pointers array.");
                return NULL;
        }
        new_node->is_leaf = false;
        new_node->num_keys = 0;
        new_node->parent = NULL;
        new_node->next = NULL;
        return new_node;
}*/

/* Creates a new leaf by creating a treeCacheNode
 * and then adapting it appropriately.
 *
treeCacheNode * make_leaf( void ) {
        treeCacheNode * leaf = make_node();
        leaf->is_leaf = true;
        return leaf;
}*/


/*PAFFS_RESULT updatetreeCacheNodePath( p_dev* dev, treeCacheNode* node){

	PAFFS_RESULT r;
	if(node->self != getRootnodeAddr(dev)){	//We are non-root
		unsigned int length = length_to_root(dev, node) + 1;
		p_addr *nodes = (p_addr*) malloc(length * sizeof(p_addr));
		r = path_from_root(dev, node, nodes, &length);
		if(r != PAFFS_OK)
			return r;
		treeCacheNode prev = *node;
		treeCacheNode c = {{0}};
		for(int i = length -1; i >= 0; i--){	//Skip first entry, is own node
			r = readtreeCacheNode(dev, nodes[i], &c);
			if(r != PAFFS_OK)
				return r;

			//find outdated entry
			int pos = 0;
			while(*getPointerAsAddr(c.pointers, pos) != prev.self){
				if(pos > c.num_keys){
					PAFFS_DBG( PAFFS_TRACE_BUG, "BUG: Did not find old address");
					return PAFFS_BUG;
				}
				pos++;
			}

			//Write previous node and register its new address in current node
			r = writetreeCacheNode(dev, &prev);
			if(r != PAFFS_OK)
				return r;
			*getPointerAsAddr(c.pointers, pos) = prev.self;

			prev = c;
		}
		if(c.self != getRootnodeAddr(dev)){
			PAFFS_DBG(PAFFS_TRACE_BUG, "Last Element in path_from_root is not actually root.");
			return PAFFS_BUG;
		}
		r = writetreeCacheNode(dev, &c);
		if(r != PAFFS_OK)
			return r;
		registerRootnode(dev, c.self);

	}else{
		//first case would do it as well, but
		//this is more efficient
		r = writetreeCacheNode(dev, node);
		if(r != PAFFS_OK)
			return r;
		registerRootnode(dev, node->self);
	}
	return PAFFS_OK;
}*/


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

	treeCacheNode *new_leaf;

	PAFFS_RESULT r = addNewCacheNode(dev, new_leaf);
	if(r != PAFFS_OK)
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
		memset(&new_leaf->raw.as_leaf.pInodes[i], 0, sizeof(pInode));	//fixme: new_leaf or just leaf?
		new_leaf->raw.as_leaf.keys[i] = 0;
	}

	new_key = new_leaf->raw.as_leaf.keys[0];

	return insert_into_parent(dev, leaf->parent, leaf, new_key, &new_leaf) ;
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
			node->raw.as_branch.keys[i] = node->raw.as_branch.keys[i - 1];
	}
	node->raw.as_branch.pointers[left_index + 1] = right->raw.self;	//Fixme: Is this really needed?
	node->pointers[left_index + 1] = right;
	node->raw.as_branch.keys[left_index] = key;
	node->raw.num_keys++;
	node->dirty = true;

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
	p_addr temp_addresses[btree_branch_order+1];


	PAFFS_RESULT r = addNewCacheNode(dev, new_node);
	if(r != PAFFS_OK)
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
	}

	for (i = 0, j = 0; i < old_node->raw.num_keys; i++, j++) {
			if (j == left_index) j++;
			temp_keys[j] = old_node->keys[i];
	}

	temp_addresses[left_index + 1] = right->self;
	temp_keys[left_index] = key;

	/* Create the new treeCacheNode and copy
	 * half the keys and pointers to the
	 * old and half to the new.
	 */
	split = cut(btree_branch_order);

	old_node->num_keys = 0;
	for (i = 0; i < split - 1; i++) {
		*getPointerAsAddr(old_node->pointers, i) = temp_addresses[i];
		old_node->keys[i] = temp_keys[i];
		old_node->num_keys++;
	}
	*getPointerAsAddr(old_node->pointers, i) = temp_addresses[i];
	k_prime = temp_keys[split - 1];
	for (++i, j = 0; i < btree_branch_order; i++, j++) {
			*getPointerAsAddr(new_node.pointers, j) = temp_addresses[i];
			new_node.keys[j] = temp_keys[i];
			new_node.num_keys++;
	}
	new_node.pointers[j] = temp_addresses[i];


	PAFFS_RESULT r = writetreeCacheNode(dev, old_node);
	if(r != PAFFS_OK)
		return r;
	r = writetreeCacheNode(dev, &new_node);

	/* Insert a new key into the parent of the two
	 * nodes resulting from the split, with
	 * the old treeCacheNode to the left and the new to the right.
	 */

	return pr == PAFFS_NOPARENT ?
			insert_into_former_parent(dev, NULL, old_node, k_prime, &new_node) :
			insert_into_former_parent(dev, &parent, old_node, k_prime, &new_node);
}



/* Inserts a new treeCacheNode (leaf or internal treeCacheNode) into the B+ tree.
 * Returns the root of the tree after insertion.
 * *** obsoleted ***
 */
/*PAFFS_RESULT insert_into_parent(p_dev* dev, treeCacheNode * left, pInode_no key, treeCacheNode * right) {

        int left_index;
        treeCacheNode parent = {{0}};

        PAFFS_RESULT r = getParent(dev, left, &parent);

        if (r == PAFFS_NOPARENT)
                return insert_into_new_root(dev, left, key, right);

        if(r != PAFFS_OK)
        	return r;



        left_index = get_left_index(&parent, left);


        if (parent.num_keys < btree_branch_order)
                return insert_into_node(dev, &parent, left_index, key, right);


        return insert_into_node_after_splitting(dev, &parent, left_index, key, right);
}
*/


/* Due to the removed parent-member, parent has to be determined before changes to Child-Nodes are done
 * if formerParent == NULL, NOPARENT is assumed
 * Inserts a new treeCacheNode (leaf or internal treeCacheNode) into the B+ tree.
 * Returns the root of the tree after insertion.
 * *** further Tree-action pending ***
 */
PAFFS_RESULT insert_into_former_parent(p_dev* dev, treeCacheNode* formerParent, treeCacheNode* left, pInode_no key, treeCacheNode* right) {

        int left_index;


        /* Case: new root. */
        if (formerParent == NULL)
                return insert_into_new_root(dev, left, key, right);



        /* Case: leaf or treeCacheNode. (Remainder of
         * function body.)
         */

        /* Find the parent's pointer to the left
         * treeCacheNode. (is done before changes were made)
         */

        left_index = get_left_index(formerParent, left);


        /* Simple case: the new key fits into the treeCacheNode.
         */

        if (formerParent->num_keys < btree_branch_order)
                return insert_into_node(dev, formerParent, left_index, key, right);

        /* Harder case:  split a treeCacheNode in order
         * to preserve the B+ tree properties.
         */

        return insert_into_node_after_splitting(dev, formerParent, left_index, key, right);
}


/* Creates a new root for two subtrees
 * and inserts the appropriate key into
 * the new root.
 */
PAFFS_RESULT insert_into_new_root(p_dev* dev, treeCacheNode * left, pInode_no key, treeCacheNode * right) {

	treeCacheNode root = {{0}};
	root.is_leaf = false;
	root.keys[0] = key;
	insertAddrInPointer(root.pointers, &left->self, 0);
	insertAddrInPointer(root.pointers, &right->self, 1);
	root.num_keys = 1;
	PAFFS_RESULT r = writetreeCacheNode(dev, &root);
	if(r != PAFFS_OK)
		return r;
	registerRootnode(dev, root.self);
	return PAFFS_OK;
}



/* start a new tree.
 * So init rootnode
 */
PAFFS_RESULT start_new_tree(p_dev* dev) {

        treeCacheNode root = {{0}};
        root.is_leaf = true;
        PAFFS_RESULT r = writetreeCacheNode(dev, &root);
        if(r != PAFFS_OK)
        	return r;
        registerRootnode(dev, root.self);
        return PAFFS_OK;
}



/* Master insertion function.
 * Inserts a key and an associated value into
 * the B+ tree, causing the tree to be adjusted
 * however necessary to maintain the B+ tree
 * properties.
 */
PAFFS_RESULT insert( p_dev* dev, pInode* value) {

        treeCacheNode node;
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
        r = readtreeCacheNode(dev, getRootnodeAddr(dev), &node);
        if (r != PAFFS_OK)
        	return r;

        if(extractLogicalArea(node.self) > dev->param.areas_no){	//Rootnode not initialized
        	PAFFS_DBG(PAFFS_TRACE_BUG, "BUG: Rootnode not initialized");
			return PAFFS_BUG;
        }
        /**  rootnode not used  **/


        /* Case: the tree already exists.
         * (Rest of function body.)
         */

        r = find_leaf(dev, value->no, &node);
        if(r != PAFFS_OK)
        	return r;

        /* Case: leaf has room for key and pointer.
         */

        if (node.num_keys < btree_leaf_order) {
                return insert_into_leaf(dev, &node, value);
        }


        /* Case:  leaf must be split.
         */

        return insert_into_leaf_after_splitting(dev, &node, value);
}



// DELETION.

/* Utility function for deletion.  Retrieves
 * the index of a treeCacheNode's nearest neighbor (sibling)
 * to the left if one exists.  If not (the treeCacheNode
 * is the leftmost child), returns -1 to signify
 * this special case.
 */
int get_neighbor_index( p_dev* dev, treeCacheNode * n ){

        int i;
        treeCacheNode parent;
        PAFFS_RESULT r = getParent(dev, n, &parent);
        if(r == PAFFS_NOPARENT)
        	return -1;
        if(r != PAFFS_OK){
        	paffs_lasterr = r;
        	return -2;
        }

        for (i = 0; i <= parent.num_keys; i++)
                if (*getPointerAsAddr(parent.pointers, i) == n->self)
                        return i - 1;

        // Error state.
        PAFFS_DBG(PAFFS_TRACE_ERROR, "Search for nonexistent pointer to treeCacheNode (%lu) in parent (%lu).\n", n->self, parent.self);
        return -1;
}

/**
 * Does not actually commit changes to filesystem
 */
PAFFS_RESULT remove_entry_from_node(p_dev* dev, treeCacheNode * n, pInode_no key) {

	int i, num_pointers;

	// Remove the key and shift other keys accordingly.
	i = 0;
	while (n->keys[i] != key)
			i++;
	for (++i; i < n->num_keys; i++)
			n->keys[i - 1] = n->keys[i];

	// Remove the pointer and shift other pointers accordingly.
	// First determine number of pointers.
	num_pointers = n->is_leaf ? n->num_keys : n->num_keys + 1;
	i = 0;
	while (n->keys[i] != key)
		i++;
	for (++i; i < num_pointers; i++)
		if (n->is_leaf)
			*getPointerAsInode(n->pointers, i - 1) = *getPointerAsInode(n->pointers, i);
		else
			*getPointerAsAddr(n->pointers, i - 1) = *getPointerAsAddr(n->pointers, i);


	// One key fewer.
	n->num_keys--;

	// Set the other pointers to NULL for tidiness.
	if (n->is_leaf)
		for (i = n->num_keys; i < btree_leaf_order; i++)
			memset(getPointerAsInode(n->pointers, i), 0, sizeof(pInode));
	else
		for (i = n->num_keys + 1; i < btree_branch_order; i++)
				*getPointerAsAddr(n->pointers, i) = 0;

	return PAFFS_OK;
}


PAFFS_RESULT adjust_root(p_dev* dev, treeCacheNode * root) {

        /* Case: nonempty root.
         * Key and pointer have already been deleted,
         * so just commit dirty changes.
         */

        if (root->num_keys > 0)
                return updatetreeCacheNodePath(dev, root);

        /* Case: empty root. 
         */

        // If it has a child, promote 
        // the first (only) child
        // as the new root.

        if (!root->is_leaf) {
                registerRootnode(dev, *getPointerAsAddr(root->pointers, 0));
                deletetreeCacheNode(dev, root);
        }

        // If it is a leaf (has no children),
        // then the whole tree is empty.

        return updatetreeCacheNodePath(dev, root);
}


/* Coalesces a treeCacheNode (n) that has become
 * too small after deletion
 * with a neighboring treeCacheNode that
 * can accept the additional entries
 * without exceeding the maximum.
 */
PAFFS_RESULT coalesce_nodes(p_dev* dev, treeCacheNode * n, treeCacheNode * neighbor, treeCacheNode* parent, int neighbor_index, int k_prime) {

	int i, j, neighbor_insertion_index, n_end;
	treeCacheNode tmp;

	/* Swap neighbor with treeCacheNode if treeCacheNode is on the
	 * extreme left and neighbor is to its right.
	 */

	if (neighbor_index == -1) {
			tmp = *n;
			n = neighbor;
			*neighbor = tmp;
	}

	/* Starting point in the neighbor for copying
	 * keys and pointers from n.
	 * Recall that n and neighbor have swapped places
	 * in the special case of n being a leftmost child.
	 */

	neighbor_insertion_index = neighbor->num_keys;

	/* Case:  nonleaf treeCacheNode.
	 * Append k_prime and the following pointer.
	 * Append all pointers and keys from the neighbor.
	 */

	if (!n->is_leaf) {

		/* Append k_prime.
		 */

		neighbor->keys[neighbor_insertion_index] = k_prime;
		neighbor->num_keys++;


		n_end = n->num_keys;

		for (i = neighbor_insertion_index + 1, j = 0; j < n_end; i++, j++) {
				neighbor->keys[i] = n->keys[j];
				*getPointerAsAddr(neighbor->pointers, i) = *getPointerAsAddr(n->pointers, j);
				neighbor->num_keys++;
				n->num_keys--;
		}

		/* The number of pointers is always
		 * one more than the number of keys.
		 */

		*getPointerAsAddr(neighbor->pointers, i) = *getPointerAsAddr(n->pointers, j);
	}

	/* In a leaf, append the keys and pointers of
	 * n to the neighbor.
	 */

	else {
		for (i = neighbor_insertion_index, j = 0; j < n->num_keys; i++, j++) {
			neighbor->keys[i] = n->keys[j];
			*getPointerAsAddr(neighbor->pointers, i) = *getPointerAsAddr(n->pointers, j);
			neighbor->num_keys++;
		}
	}

	PAFFS_RESULT r = deletetreeCacheNode(dev, n);
	if(r != PAFFS_OK)
			return r;

	//If changes were made...
	if(n->num_keys > 0 || true){	//TODO: true is only for debugging
		r = updatetreeCacheNodePath(dev, neighbor);
		if(r != PAFFS_OK)
				return r;
		//FIXME: Get Parent fails, because neighbor now has additional key
		//			And can't be found, because parent has still old key (is not changed yet)
		r = getParent(dev, neighbor, parent);
	}

	//lets hope, n->key is k_prime
	return delete_entry(dev, parent, k_prime);

}


/* Redistributes entries between two nodes when
 * one has become too small after deletion
 * but its neighbor is too big to append the
 * small treeCacheNode's entries without exceeding the
 * maximum
 */
PAFFS_RESULT redistribute_nodes(p_dev* dev, treeCacheNode * n, treeCacheNode * neighbor, treeCacheNode* parent, int neighbor_index,
                int k_prime_index, int k_prime) {  

	int i;
	PAFFS_RESULT r;


	/* Case: n has a neighbor to the left.
	 * Pull the neighbor's last key-pointer pair over
	 * from the neighbor's right end to n's left end.
	 */

	if (neighbor_index != -1) {
			if (!n->is_leaf){
				*getPointerAsInode(n->pointers, n->num_keys + 1) = *getPointerAsInode(n->pointers, n->num_keys);
				for (i = n->num_keys; i > 0; i--) {
					n->keys[i] = n->keys[i - 1];
					*getPointerAsInode(n->pointers, i) = *getPointerAsInode(n->pointers, i - 1);
				}
			}else{
				for (i = n->num_keys; i > 0; i--) {
					n->keys[i] = n->keys[i - 1];
					*getPointerAsAddr(n->pointers, i) = *getPointerAsAddr(n->pointers, i - 1);
				}
			}
			if (!n->is_leaf) {
					*getPointerAsAddr(n->pointers, 0) = *getPointerAsAddr(neighbor->pointers, neighbor->num_keys);
					*getPointerAsAddr(neighbor->pointers, neighbor->num_keys) = 0;
					n->keys[0] = k_prime;
					parent->keys[k_prime_index] = neighbor->keys[neighbor->num_keys - 1];
			}
			else {
					*getPointerAsInode(n->pointers, 0) = *getPointerAsInode(neighbor->pointers, neighbor->num_keys - 1);
					memset(getPointerAsInode(neighbor->pointers, neighbor->num_keys - 1), 0, sizeof(pInode));
					n->keys[0] = neighbor->keys[neighbor->num_keys - 1];
					parent->keys[k_prime_index] = n->keys[0];
			}

	}

	/* Case: n is the leftmost child.
	 * Take a key-pointer pair from the neighbor to the right.
	 * Move the neighbor's leftmost key-pointer pair
	 * to n's rightmost position.
	 */

	else {
		if (n->is_leaf) {
			n->keys[n->num_keys] = neighbor->keys[0];
			*getPointerAsInode(n->pointers, n->num_keys) = *getPointerAsInode(neighbor->pointers, 0);
			parent->keys[k_prime_index] = neighbor->keys[1];
			for (i = 0; i < neighbor->num_keys - 1; i++) {
				neighbor->keys[i] = neighbor->keys[i + 1];
				*getPointerAsInode(neighbor->pointers, i) = *getPointerAsInode(neighbor->pointers, i + 1);
			}
		}
		else {
			n->keys[n->num_keys] = k_prime;
			n->pointers[n->num_keys + 1] = neighbor->pointers[0];
			parent->keys[k_prime_index] = neighbor->keys[0];
			for (i = 0; i < neighbor->num_keys - 1; i++) {
				neighbor->keys[i] = neighbor->keys[i + 1];
				*getPointerAsAddr(neighbor->pointers, i) = *getPointerAsAddr(neighbor->pointers, i + 1);
			}
		}

		if (!n->is_leaf)
				neighbor->pointers[i] = neighbor->pointers[i + 1];
	}

	/* n now has one more key and one more pointer;
	 * the neighbor has one fewer of each.
	 */

	n->num_keys++;
	neighbor->num_keys--;


	p_addr oldAddrNode = n->self;
	r = writetreeCacheNode(dev, n);
	if(r != PAFFS_OK)
		return r;
	p_addr oldAddrNeigh = neighbor->self;
	r = writetreeCacheNode(dev, neighbor);
	if(r != PAFFS_OK)
		return r;

	for(int i = 0; i <= parent->num_keys; i++){
		if(*getPointerAsAddr(parent->pointers, i) == oldAddrNode)
				*getPointerAsAddr(parent->pointers, i) = n->self;
		if(*getPointerAsAddr(parent->pointers, i) == oldAddrNeigh)
				*getPointerAsAddr(parent->pointers, i) = neighbor->self;
	}

	r = updatetreeCacheNodePath(dev, parent);
	if(r != PAFFS_OK)
		return r;

	return PAFFS_OK;
}


/* Deletes an entry from the B+ tree.
 * Removes the pinode and its key and pointer
 * from the leaf, and then makes all appropriate
 * changes to preserve the B+ tree properties.
 */
PAFFS_RESULT delete_entry( p_dev* dev, treeCacheNode * n, pInode_no key){

	int min_keys;
	treeCacheNode neighbor;
	int neighbor_index;
	int k_prime_index, k_prime;
	int capacity;

	treeCacheNode nParent = {{0}};
	PAFFS_RESULT parent_r = getParent(dev, n, &nParent);
	if(parent_r != PAFFS_OK && parent_r != PAFFS_NOPARENT)
		return parent_r;

	// Remove key and pointer from treeCacheNode.

	PAFFS_RESULT r = remove_entry_from_node(dev, n, key);
	if(r != PAFFS_OK)
		return r;

	/* Case:  deletion from root.
	 */

	if (parent_r == PAFFS_NOPARENT)
		return adjust_root(dev, n);


	/* Case:  deletion from a treeCacheNode below the root.
	 * (Rest of function body.)
	 */

	/* Determine minimum allowable size of treeCacheNode,
	 * to be preserved after deletion.
	 */

	//cut(btree_leaf/branch_order) - 1 oder ohne -1?
	min_keys = n->is_leaf ? cut(btree_leaf_order) : cut(btree_branch_order);

	/* Case:  treeCacheNode stays at or above minimum.
	 * (The simple case.)
	 */

	if (n->num_keys >= min_keys)
			return updatetreeCacheNodePath(dev, n);


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

	neighbor_index = get_neighbor_index( dev, n );
	k_prime_index = neighbor_index == -1 ? 0 : neighbor_index;
	k_prime = nParent.keys[k_prime_index];
	p_addr neighbor_addr = neighbor_index == -1 ? *getPointerAsAddr(nParent.pointers, 1) :
			*getPointerAsAddr(nParent.pointers, neighbor_index);
	r = readtreeCacheNode(dev, neighbor_addr, &neighbor);
	if(r != PAFFS_OK)
		return r;

	capacity = neighbor.is_leaf ? btree_leaf_order : btree_branch_order;	//-1?

	/* Coalescence. */

	if (neighbor.num_keys + n->num_keys < capacity)
		//Write of Nodes happens in coalesce_nodes
		return coalesce_nodes(dev, n, &neighbor, &nParent, neighbor_index, k_prime);

	/* Redistribution. */

	else
		return redistribute_nodes(dev, n, &neighbor, &nParent, neighbor_index, k_prime_index, k_prime);
}


/* Prints the B+ tree in the command
 * line in level (rank) order, with the
 * keys in each treeCacheNode and the '|' symbol
 * to separate nodes.
 */
void print_tree( p_dev* dev) {
	treeCacheNode n = {{0}};
	PAFFS_RESULT r = readtreeCacheNode(dev, getRootnodeAddr(dev), &n);
	if(r != PAFFS_OK){
		printf("%s!\n", paffs_err_msg(r));
		return;
	}
	print_keys(dev, &n);
	//print_leaves(dev, &n);
}

/* Prints the bottom row of keys
 * of the tree (with their respective
 * pointers, if the verbose_output flag is set.
 */
void print_leaves(p_dev* dev, treeCacheNode* c) {
	if(c->is_leaf){
		printf("| ");
		for(int i = 0; i < c->num_keys; i++)
			printf("%lu ", getPointerAsInode(c->pointers, i)->no);
	}else{
		for(int i = 0; i <= c->num_keys; i++){
			treeCacheNode n = {{0}};
			PAFFS_RESULT r = readtreeCacheNode(dev, *getPointerAsAddr(c->pointers, i), &n);
			if(r != PAFFS_OK){
				printf("%s!\n", paffs_err_msg(r));
				return;
			}
			print_leaves(dev, &n);
			fflush(stdout);
		}
	}
}

void print_queued_keys_r(p_dev* dev, queue_s* q){
	queue_s* new_q = queue_new();
	printf("|");
	while(!queue_empty(q)){
		treeCacheNode *n = queue_dequeue(q);
		for(int i = 0; i <= n->num_keys; i++){
			if(!n->is_leaf){
				treeCacheNode *nn = (treeCacheNode*) malloc(sizeof(treeCacheNode));
				PAFFS_RESULT r = readtreeCacheNode(dev, *getPointerAsAddr(n->pointers, i), nn);
				if(r != PAFFS_OK){
					printf("%s!\n", paffs_err_msg(r));
					return;
				}
				queue_enqueue(new_q, nn);
				if(i < n->num_keys) printf(".%lu.", n->keys[i]);
			}else{
				if(i < n->num_keys) printf(" %lu ", n->keys[i]);
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

