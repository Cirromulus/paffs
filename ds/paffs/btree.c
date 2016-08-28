/**
 * File created on 18.05.2016
 * Author: Pascal Pieper
 */

#include "btree.h"
#include "paffs_flash.h"
#include <stdio.h>
#include <stdlib.h> 
#include <string.h>

//Out-of-bounds check has to be made from outside
p_addr* getPointerAsAddr(char* pointers, unsigned int pos){
	return (p_addr*)&pointers[pos * sizeof(p_addr)];
}
//Out-of-bounds check has to be made from outside
pInode* getPointerAsInode(char* pointers, unsigned int pos){
	return (pInode*)&pointers[pos * sizeof(pInode)];
}

void insertAddrInPointer(char* pointers, p_addr* addr, unsigned int pos){
	memcpy(&pointers[pos * sizeof(p_addr)], addr, sizeof(p_addr));
}

void insertInodeInPointer(char* pointers, pInode* inode, unsigned int pos){
	memcpy(&pointers[pos * sizeof(pInode)], inode, sizeof(pInode ));
}

PAFFS_RESULT updateAddrInTreenode(treeNode* node, p_addr* old, p_addr* newAddress){
	int pos = 0;
	while(*getPointerAsAddr(node->pointers, pos) != *old){
		if(pos > node->num_keys){
			PAFFS_DBG( PAFFS_TRACE_BUG, "BUG: Did not find old address");
			return PAFFS_BUG;
		}
		pos++;
	}
	*getPointerAsAddr(node->pointers, pos) = *newAddress;
	return PAFFS_OK;
}


PAFFS_RESULT insertInode( p_dev* dev, pInode* inode){
	return insert(dev, inode);
}

PAFFS_RESULT getInode( p_dev* dev, pInode_no number, pInode* outInode){
	return find(dev, number, outInode);
}

PAFFS_RESULT updateExistingInode( p_dev* dev, pInode* inode){
	treeNode node;
	PAFFS_RESULT r = find_leaf(dev, inode->no, &node);
	if(r != PAFFS_OK)
		return r;

	int pos;
	for(pos = 0; pos < node.num_keys; pos++){
		if(getPointerAsInode(node.pointers, pos)->no == inode->no)
			break;
	}

	if(pos == node.num_keys)
		return PAFFS_BUG;	//This Key did not exist

	*getPointerAsInode(node.pointers, pos) = *inode;



	return updateTreeNodePath(dev, &node);
}

PAFFS_RESULT deleteInode( p_dev* dev, pInode_no number){

	pInode key;
	treeNode key_leaf;

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
	treeNode c;
	*outNumber = 0;
	PAFFS_RESULT r = readTreeNode(dev, getRootnodeAddr(dev), &c);
	if(r != PAFFS_OK)
		return r;
	while(!c.is_leaf){
		PAFFS_RESULT r = readTreeNode(dev, *getPointerAsAddr(c.pointers, c.num_keys), &c);
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
bool isEqual(treeNode* left, treeNode* right){
	return left->self == right->self;
}

/* Utility function to give the height
 * of the tree, which length in number of edges
 * of the path from the root to any leaf.
 */
int height( p_dev* dev, treeNode * root ) {
        int h = 0;
        treeNode curr = *root;
        while (!curr.is_leaf) {
        		p_addr* addr = getPointerAsAddr(curr.pointers, 0);
                PAFFS_RESULT r = readTreeNode(dev, *addr, &curr);
                if(r != PAFFS_OK){
                	paffs_lasterr = r;
                	return -1;
                }
                h++;
        }
        return h;
}


/* Utility function to give the length in edges
 * of the path from any treeNode to the root.
 */
int length_to_root( p_dev* dev, treeNode * child ){
	unsigned int length;
	PAFFS_RESULT r = path_from_root(dev, child, NULL, &length);
	if(r != PAFFS_OK){
		paffs_lasterr = r;
		return -1;
	}
	return length;
}

PAFFS_RESULT path_from_root( p_dev* dev, treeNode * child, p_addr* path, unsigned int* lengthOut){
	if(child->self == 0){
		PAFFS_DBG(PAFFS_TRACE_BUG, "BUG: Child has no valid address");
		return PAFFS_BUG;
	}

	*lengthOut = 0;
	treeNode c;
	PAFFS_RESULT r = readTreeNode(dev, getRootnodeAddr(dev), &c);
	if(r != PAFFS_OK){
		return r;
	}
	if(path != NULL)
		path[*lengthOut] = c.self;
	while (!isEqual(&c, child)) {
		if(c.num_keys == 0){
			PAFFS_DBG(PAFFS_TRACE_BUG, "BUG: Non_root Node has no keys");
			return PAFFS_BUG;
		}
		if(c.is_leaf){
			PAFFS_DBG(PAFFS_TRACE_BUG, "BUG: Detached Child?");
			return PAFFS_BUG;
		}
		unsigned int next = 0;
		while(next < c.num_keys){
			if(child->keys[0] >= c.keys[next]) next++;
			else break;
		}
		r = readTreeNode(dev, *getPointerAsAddr(c.pointers, next), &c);
		if(r != PAFFS_OK){
			return r;
		}
		++*lengthOut;
		if(path != NULL)
			path[*lengthOut] = c.self;
	}
	return PAFFS_OK;
}

PAFFS_RESULT getParent(p_dev* dev, treeNode * node, treeNode* parentOut){
	if(node->self == 0){
		PAFFS_DBG(PAFFS_TRACE_BUG, "BUG: Node has no valid address");
		return PAFFS_BUG;
	}
	treeNode c;
	PAFFS_RESULT r = readTreeNode(dev, getRootnodeAddr(dev), &c);
	if(r != PAFFS_OK){
		return r;
	}
	if(isEqual(&c, node)){
		return PAFFS_NOPARENT;
	}

	while (!isEqual(&c, node)) {
		*parentOut = c;
		if(c.num_keys == 0){
			PAFFS_DBG(PAFFS_TRACE_BUG, "BUG: Non_root Node has no keys");
			return PAFFS_BUG;
		}
		if(c.is_leaf){
			PAFFS_DBG(PAFFS_TRACE_BUG, "BUG: Detached Child?");
			return PAFFS_BUG;
		}
		unsigned int next = 0;
		while(next < c.num_keys){
			if(node->keys[0] >= c.keys[next]) next++;
			else break;
		}
		r = readTreeNode(dev, *getPointerAsAddr(c.pointers, next), &c);
		if(r != PAFFS_OK){
			return r;
		}
	}
	return PAFFS_OK;
}

/* Prints the B+ tree in the command
 * line in level (rank) order, with the 
 * keys in each treeNode and the '|' symbol
 * to separate nodes.
 * With the verbose_output flag set.
 * the values of the pointers corresponding
 * to the keys also appear next to their respective
 * keys, in hexadecimal notation.
 */
void print_tree( p_dev* dev) {
	treeNode n = {{0}};
	PAFFS_RESULT r = readTreeNode(dev, getRootnodeAddr(dev), &n);
	if(r != PAFFS_OK){
		printf("%s!\n", paffs_err_msg(r));
		return;
	}
	print_leaves(dev, &n);
	printf("|\n");
}

/* Prints the bottom row of keys
 * of the tree (with their respective
 * pointers, if the verbose_output flag is set.
 */
void print_leaves(p_dev* dev, treeNode* c) {
	if(c->is_leaf){
		printf("| ");
		for(int i = 0; i < c->num_keys; i++)
			printf("%lu ", getPointerAsInode(c->pointers, i)->no);
	}else{
		for(int i = 0; i <= c->num_keys; i++){
			treeNode n = {{0}};
			PAFFS_RESULT r = readTreeNode(dev, *getPointerAsAddr(c->pointers, i), &n);
			if(r != PAFFS_OK){
				printf("%s!\n", paffs_err_msg(r));
				return;
			}
			print_leaves(dev, &n);
			fflush(stdout);
		}
	}
}



/* Finds keys and their pointers, if present, in the range specified
 * by key_start and key_end, inclusive.  Places these in the arrays
 * returned_keys and returned_pointers, and returns the number of
 * entries found.
 */
int find_range( p_dev* dev, treeNode * root, pInode_no key_start, pInode_no key_end,
        int returned_keys[], void * returned_pointers[]) {
/*        int i, num_found;
        num_found = 0;
        treeNode * n = find_leaf( root, key_start, verbose );
        if (n == NULL) return 0;
        for (i = 0; i < n->num_keys && n->keys[i] < key_start; i++) ;
        if (i == n->num_keys) return 0;
        while (n != NULL) {
                for ( ; i < n->num_keys && n->keys[i] <= key_end; i++) {
                        returned_keys[num_found] = n->keys[i];
                        returned_pointers[num_found] = n->pointers[i];
                        num_found++;
                }
                n = n->pointers[btree_branch_order - 1];
                i = 0;
        }
        return num_found;*/
	printf("Not implemented\n");
	return -1;
}


/* Traces the path from the root to a leaf, searching
 * by key.  Displays information about the path
 * if the verbose flag is set.
 * Returns the leaf containing the given key.
 */
PAFFS_RESULT find_leaf( p_dev* dev, pInode_no key, treeNode* outTreenode) {
	int i = 0;
	treeNode c = {{0}};
	PAFFS_RESULT r = readTreeNode(dev, getRootnodeAddr(dev), &c);
	if(r != PAFFS_OK)
		return r;


	while (!c.is_leaf) {

		i = 0;
		while (i < c.num_keys) {
			if (key >= c.keys[i]) i++;
			else break;
		}

		//printf("%d ->\n", i);
		p_addr *addr = getPointerAsAddr(c.pointers, i);
		PAFFS_RESULT r = readTreeNode(dev, *addr, &c);
		if(r != PAFFS_OK)
			return r;
	}

	*outTreenode = c;
	return PAFFS_OK;
}

PAFFS_RESULT find_in_leaf (treeNode* leaf, pInode_no key, pInode* outInode){
	int i;
    for (i = 0; i < leaf->num_keys; i++)
            if (leaf->keys[i] == key) break;
    if (i == leaf->num_keys)
            return PAFFS_NF;
	*outInode = *getPointerAsInode(leaf->pointers, i);
	return PAFFS_OK;
}

/* Finds and returns the pinode to which
 * a key refers.
 */
PAFFS_RESULT find( p_dev* dev, pInode_no key, pInode* outInode){
    treeNode c = {{0}};
    PAFFS_RESULT r = find_leaf( dev, key, &c);
    if(r != PAFFS_OK)
    	return r;
    return find_in_leaf(&c, key, outInode);
}

/* Finds the appropriate place to
 * split a treeNode that is too big into two.
 */
int cut( int length ) {
        if (length % 2 == 0)
                return length/2;
        else
                return length/2 + 1;
}


// INSERTION

/* Creates a new pinode to hold the value
 * to which a key refers.
 */
pInode * make_pinode(pInode pn) {
        pInode * new_pinode = (pInode *)malloc(sizeof(pInode));
        if (new_pinode == NULL) {
                perror("pinode creation.");
                return NULL;
        }
        else {
                memcpy(new_pinode, &pn, sizeof(pInode));
        }
        return new_pinode;
}


/* Creates a new general treeNode, which can be adapted
 * to serve as either a leaf or an internal treeNode.
 *
treeNode * make_node( void ) {
        treeNode * new_node;
        new_node = malloc(sizeof(treeNode));
        if (new_node == NULL) {
                perror("treeNode creation.");
                return NULL;
        }
        new_node->keys = malloc( (btree_branch_order - 1) * sizeof(int) );
        if (new_node->keys == NULL) {
                perror("New treeNode keys array.");
                return NULL;
        }
        new_node->pointers = malloc( btree_branch_order * sizeof(void *) );
        if (new_node->pointers == NULL) {
                perror("New treeNode pointers array.");
                return NULL;
        }
        new_node->is_leaf = false;
        new_node->num_keys = 0;
        new_node->parent = NULL;
        new_node->next = NULL;
        return new_node;
}*/

/* Creates a new leaf by creating a treeNode
 * and then adapting it appropriately.
 *
treeNode * make_leaf( void ) {
        treeNode * leaf = make_node();
        leaf->is_leaf = true;
        return leaf;
}*/


PAFFS_RESULT updateTreeNodePath( p_dev* dev, treeNode* node){

	PAFFS_RESULT r;
	if(node->self != getRootnodeAddr(dev)){	//We are non-root
		unsigned int length = length_to_root(dev, node) + 1;
		p_addr *nodes = (p_addr*) malloc(length * sizeof(p_addr));
		r = path_from_root(dev, node, nodes, &length);
		if(r != PAFFS_OK)
			return r;
		treeNode prev = *node;
		treeNode c = {{0}};
		for(int i = length -1; i >= 0; i--){	//Skip first entry, is own node
			r = readTreeNode(dev, nodes[i], &c);
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
			r = writeTreeNode(dev, &prev);
			if(r != PAFFS_OK)
				return r;
			*getPointerAsAddr(c.pointers, pos) = prev.self;

			prev = c;
		}
		if(c.self != getRootnodeAddr(dev)){
			PAFFS_DBG(PAFFS_TRACE_BUG, "Last Element in path_from_root is not actually root.");
			return PAFFS_BUG;
		}
		r = writeTreeNode(dev, &c);
		if(r != PAFFS_OK)
			return r;
		registerRootnode(dev, c.self);

	}else{
		//first case would do it as well, but
		//this is more efficient
		r = writeTreeNode(dev, node);
		if(r != PAFFS_OK)
			return r;
		registerRootnode(dev, node->self);
	}
	return PAFFS_OK;
}


/* Helper function used in insert_into_parent
 * to find the index of the parent's pointer to 
 * the treeNode to the left of the key to be inserted.
 */
int get_left_index(treeNode * parent, treeNode * left) {

        int left_index = 0;
        while (left_index < parent->num_keys &&
                        *getPointerAsAddr(parent->pointers, left_index) != left->self)
                left_index++;
        return left_index;
}

/* Inserts a new pointer to a pinode and its corresponding
 * key into a leaf when it has enough space free.
 * (No further Tree-action Required)
 */
PAFFS_RESULT insert_into_leaf( p_dev* dev, treeNode * leaf, pInode * newInode ) {

        int i, insertion_point;

        insertion_point = 0;
        while (insertion_point < leaf->num_keys && leaf->keys[insertion_point] < newInode->no)
                insertion_point++;

        for (i = leaf->num_keys; i > insertion_point; i--) {
                leaf->keys[i] = leaf->keys[i - 1];
                *getPointerAsInode(leaf->pointers, i) = *getPointerAsInode(leaf->pointers, i - 1) ;
        }
        leaf->keys[insertion_point] = newInode->no;
        leaf->num_keys++;
        *getPointerAsInode(leaf->pointers, insertion_point) = *newInode;

        return updateTreeNodePath(dev, leaf);
}


/* Inserts a new key and pointer
 * to a new pinode into a leaf so as to exceed
 * the tree's order, causing the leaf to be split
 * in half.
 */
PAFFS_RESULT insert_into_leaf_after_splitting(p_dev* dev, treeNode * leaf, pInode * newInode) {

	treeNode new_leaf = {{0}};
	pInode_no temp_keys[btree_leaf_order+1];
	pInode temp_pInodes[btree_leaf_order+1];
	int insertion_index, split, new_key, i, j;
	memset(temp_keys, 0, btree_leaf_order+1 * sizeof(pInode_no));
	memset(temp_pInodes, 0, btree_leaf_order+1 * sizeof(pInode));

	//Because Tree will be invalidated after write,
	//and parent is no longer determinable,
	//it is determined now.
	treeNode parent = {{0}};
	PAFFS_RESULT pr = getParent(dev, leaf, &parent);
	if(pr != PAFFS_OK && pr != PAFFS_NOPARENT)
		return pr;

	new_leaf.is_leaf = true;

	insertion_index = 0;
	while (insertion_index < btree_leaf_order && leaf->keys[insertion_index] < newInode->no)
		insertion_index++;

	for (i = 0, j = 0; i < leaf->num_keys; i++, j++) {
		if (j == insertion_index) j++;
		temp_keys[j] = leaf->keys[i];
		temp_pInodes[j] = *getPointerAsInode(leaf->pointers, i);
	}

	temp_keys[insertion_index] = newInode->no;
	temp_pInodes[insertion_index] = *newInode;

	leaf->num_keys = 0;

	split = cut(btree_leaf_order);

	for (i = 0; i < split; i++) {
		*getPointerAsInode(leaf->pointers, i) = temp_pInodes[i];
		leaf->keys[i] = temp_keys[i];
		leaf->num_keys++;
	}

	for (i = split, j = 0; i <= btree_leaf_order; i++, j++) {
		*getPointerAsInode(new_leaf.pointers, j) = temp_pInodes[i];
		new_leaf.keys[j] = temp_keys[i];
		new_leaf.num_keys++;
	}

	/*Next cousin is no longer supported
	new_leaf.pointers[btree_branch_order - 1] = leaf->pointers[btree_branch_order - 1];
	leaf->pointers[btree_branch_order - 1] = new_leaf;
	*/

	for (i = leaf->num_keys; i < btree_leaf_order; i++){
		memset(getPointerAsInode(leaf->pointers, i), 0, sizeof(pInode));
		leaf->keys[i] = 0;
	}
	for (i = new_leaf.num_keys; i < btree_leaf_order; i++){
		memset(getPointerAsInode(leaf->pointers, i), 0, sizeof(pInode));
		new_leaf.keys[i] = 0;
	}

	p_addr oldAddr = leaf->self;
	PAFFS_RESULT r = writeTreeNode(dev, leaf);
	if(r != PAFFS_OK)
		return r;

	//These changes on parent dont have to be commited just now
	//because insert_into_former_parent will do this
	if(pr != PAFFS_NOPARENT && updateAddrInTreenode(&parent, &oldAddr, &leaf->self) != PAFFS_OK)
		return PAFFS_BUG;

	r = writeTreeNode(dev, &new_leaf);
	if(r != PAFFS_OK)
		return r;


	new_key = new_leaf.keys[0];

	return pr == PAFFS_NOPARENT ?
			insert_into_former_parent(dev, NULL, leaf, new_key, &new_leaf) :
			insert_into_former_parent(dev, &parent, leaf, new_key, &new_leaf) ;
}


/* Inserts a new key and pointer to a treeNode
 * into a treeNode into which these can fit
 * without violating the B+ tree properties.
 * (No further Tree-action Required)
 */
PAFFS_RESULT insert_into_node(p_dev *dev, treeNode * node,
	int left_index, pInode_no key, treeNode * right) {
	int i;

	for (i = node->num_keys; i > left_index; i--) {
			*getPointerAsAddr(node->pointers, i + 1) = *getPointerAsAddr(node->pointers, i);
			node->keys[i] = node->keys[i - 1];
	}
	insertAddrInPointer(node->pointers, &right->self, left_index + 1);
	node->keys[left_index] = key;
	node->num_keys++;
	return updateTreeNodePath(dev, node);
}


/* Inserts a new key and pointer to a treeNode
 * into a treeNode, causing the treeNode's size to exceed
 * the order, and causing the treeNode to split into two.
 */
PAFFS_RESULT insert_into_node_after_splitting(p_dev* dev, treeNode * old_node, int left_index,
                pInode_no key, treeNode * right) {

	int i, j, split, k_prime;
	treeNode new_node = {{0}};
	pInode_no temp_keys[btree_branch_order+1];
	p_addr temp_addresses[btree_branch_order+1];

	//Because Tree will be invalidated after write,
	//and parent is no longer determinable,
	//it is extracted now.
	treeNode parent = {{0}};
	PAFFS_RESULT pr = getParent(dev, old_node, &parent);
	if(pr != PAFFS_OK && pr != PAFFS_NOPARENT)
		return pr;

	/* First create a temporary set of keys and pointers
	 * to hold everything in order, including
	 * the new key and pointer, inserted in their
	 * correct places.
	 * Then create a new treeNode and copy half of the
	 * keys and pointers to the old treeNode and
	 * the other half to the new.
	 */

	for (i = 0, j = 0; i < old_node->num_keys + 1; i++, j++) {
			if (j == left_index + 1) j++;
			temp_addresses[j] = *getPointerAsAddr(old_node->pointers, i);
	}

	for (i = 0, j = 0; i < old_node->num_keys; i++, j++) {
			if (j == left_index) j++;
			temp_keys[j] = old_node->keys[i];
	}

	temp_addresses[left_index + 1] = right->self;
	temp_keys[left_index] = key;

	/* Create the new treeNode and copy
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


	PAFFS_RESULT r = writeTreeNode(dev, old_node);
	if(r != PAFFS_OK)
		return r;
	r = writeTreeNode(dev, &new_node);

	/* Insert a new key into the parent of the two
	 * nodes resulting from the split, with
	 * the old treeNode to the left and the new to the right.
	 */

	return pr == PAFFS_NOPARENT ?
			insert_into_former_parent(dev, NULL, old_node, k_prime, &new_node) :
			insert_into_former_parent(dev, &parent, old_node, k_prime, &new_node);
}



/* Inserts a new treeNode (leaf or internal treeNode) into the B+ tree.
 * Returns the root of the tree after insertion.
 * *** obsoleted ***
 */
/*PAFFS_RESULT insert_into_parent(p_dev* dev, treeNode * left, pInode_no key, treeNode * right) {

        int left_index;
        treeNode parent = {{0}};

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
 * Inserts a new treeNode (leaf or internal treeNode) into the B+ tree.
 * Returns the root of the tree after insertion.
 * *** further Tree-action pending ***
 */
PAFFS_RESULT insert_into_former_parent(p_dev* dev, treeNode* formerParent, treeNode* left, pInode_no key, treeNode* right) {

        int left_index;


        /* Case: new root. */
        if (formerParent == NULL)
                return insert_into_new_root(dev, left, key, right);



        /* Case: leaf or treeNode. (Remainder of
         * function body.)
         */

        /* Find the parent's pointer to the left
         * treeNode. (is done before changes were made)
         */

        left_index = get_left_index(formerParent, left);


        /* Simple case: the new key fits into the treeNode.
         */

        if (formerParent->num_keys < btree_branch_order)
                return insert_into_node(dev, formerParent, left_index, key, right);

        /* Harder case:  split a treeNode in order
         * to preserve the B+ tree properties.
         */

        return insert_into_node_after_splitting(dev, formerParent, left_index, key, right);
}


/* Creates a new root for two subtrees
 * and inserts the appropriate key into
 * the new root.
 */
PAFFS_RESULT insert_into_new_root(p_dev* dev, treeNode * left, pInode_no key, treeNode * right) {

	treeNode root = {{0}};
	root.is_leaf = false;
	root.keys[0] = key;
	insertAddrInPointer(root.pointers, &left->self, 0);
	insertAddrInPointer(root.pointers, &right->self, 1);
	root.num_keys = 1;
	PAFFS_RESULT r = writeTreeNode(dev, &root);
	if(r != PAFFS_OK)
		return r;
	registerRootnode(dev, root.self);
	return PAFFS_OK;
}



/* start a new tree.
 * So init rootnode
 */
PAFFS_RESULT start_new_tree(p_dev* dev) {

        treeNode root = {{0}};
        root.is_leaf = true;
        PAFFS_RESULT r = writeTreeNode(dev, &root);
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

        treeNode node;
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
        r = readTreeNode(dev, getRootnodeAddr(dev), &node);
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
 * the index of a treeNode's nearest neighbor (sibling)
 * to the left if one exists.  If not (the treeNode
 * is the leftmost child), returns -1 to signify
 * this special case.
 */
int get_neighbor_index( p_dev* dev, treeNode * n ){

        int i;
        treeNode parent;
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
        PAFFS_DBG(PAFFS_TRACE_ERROR, "Search for nonexistent pointer to treeNode (%d) in parent (%d).\n", n->self, parent.self);
        return -1;
}

/**
 * Does not actually commit changes to filesystem
 */
PAFFS_RESULT remove_entry_from_node(p_dev* dev, treeNode * n, pInode_no key) {

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


PAFFS_RESULT adjust_root(p_dev* dev, treeNode * root) {

        /* Case: nonempty root.
         * Key and pointer have already been deleted,
         * so just commit dirty changes.
         */

        if (root->num_keys > 0)
                return updateTreeNodePath(dev, root);

        /* Case: empty root. 
         */

        // If it has a child, promote 
        // the first (only) child
        // as the new root.

        if (!root->is_leaf) {
                registerRootnode(dev, *getPointerAsAddr(root->pointers, 0));
                deleteTreeNode(dev, root);
        }

        // If it is a leaf (has no children),
        // then the whole tree is empty.

        return updateTreeNodePath(dev, root);
}


/* Coalesces a treeNode (n) that has become
 * too small after deletion
 * with a neighboring treeNode that
 * can accept the additional entries
 * without exceeding the maximum.
 */
PAFFS_RESULT coalesce_nodes(p_dev* dev, treeNode * n, treeNode * neighbor, int neighbor_index, int k_prime) {

	int i, j, neighbor_insertion_index, n_end;
	treeNode tmp;

	/* Swap neighbor with treeNode if treeNode is on the
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

	/* Case:  nonleaf treeNode.
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
	PAFFS_RESULT r = writeTreeNode(dev, neighbor);
	if(r != PAFFS_OK)
		return r;
	treeNode parent = {{0}};
	r = getParent(dev, n, &parent);
	if(r != PAFFS_OK)
		return r;
	//lets hope, n->key is k_prime
	return delete_entry(dev, &parent, k_prime);

}


/* Redistributes entries between two nodes when
 * one has become too small after deletion
 * but its neighbor is too big to append the
 * small treeNode's entries without exceeding the
 * maximum
 */
PAFFS_RESULT redistribute_nodes(p_dev* dev, treeNode * n, treeNode * neighbor, int neighbor_index,
                int k_prime_index, int k_prime) {  

	int i;
	treeNode tmp, parent;
	PAFFS_RESULT r;

	r = getParent(dev, n, &parent);
	if(r != PAFFS_OK)
		return r;
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
					r = readTreeNode(dev, *getPointerAsAddr(n->pointers, 0), &tmp);
					if(r != PAFFS_OK)
						return r;
					*getPointerAsAddr(neighbor->pointers, neighbor->num_keys) = 0;
					n->keys[0] = k_prime;
					parent.keys[k_prime_index] = neighbor->keys[neighbor->num_keys - 1];
			}
			else {
					*getPointerAsInode(n->pointers, 0) = *getPointerAsInode(neighbor->pointers, neighbor->num_keys - 1);
					memset(getPointerAsInode(neighbor->pointers, neighbor->num_keys - 1), 0, sizeof(pInode));
					n->keys[0] = neighbor->keys[neighbor->num_keys - 1];
					parent.keys[k_prime_index] = n->keys[0];
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
			parent.keys[k_prime_index] = neighbor->keys[1];
			for (i = 0; i < neighbor->num_keys - 1; i++) {
				neighbor->keys[i] = neighbor->keys[i + 1];
				*getPointerAsInode(neighbor->pointers, i) = *getPointerAsInode(neighbor->pointers, i + 1);
			}
		}
		else {
			n->keys[n->num_keys] = k_prime;
			n->pointers[n->num_keys + 1] = neighbor->pointers[0];
			r = readTreeNode(dev, *getPointerAsAddr(n->pointers, n->num_keys + 1), &tmp);
			if(r != PAFFS_OK)
				return r;
			//tmp->parent = n; Das lassen wir mal aus...
			parent.keys[k_prime_index] = neighbor->keys[0];
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

	//TODO: Wrap three (expensive) uses of updateTreeNode to one
	r = updateTreeNodePath(dev, n);
	if(r != PAFFS_OK)
		return r;
	r = updateTreeNodePath(dev, neighbor);
	if(r != PAFFS_OK)
		return r;
	r = updateTreeNodePath(dev, &parent);
	if(r != PAFFS_OK)
		return r;

	return PAFFS_OK;
}


PAFFS_RESULT removeTreeNodePath( p_dev* dev, treeNode* node, pInode_no key){
	//Intention is to remove whole path to deleted entry,
	//but is not needed (?) when tree is conform with rules
	return PAFFS_NIMPL;
}

/* Deletes an entry from the B+ tree.
 * Removes the pinode and its key and pointer
 * from the leaf, and then makes all appropriate
 * changes to preserve the B+ tree properties.
 */
PAFFS_RESULT delete_entry( p_dev* dev, treeNode * n, pInode_no key){

	int min_keys;
	treeNode neighbor;
	int neighbor_index;
	int k_prime_index, k_prime;
	int capacity;

	/* Find the appropriate neighbor treeNode with which
	 * to coalesce.
	 * Also find the key (k_prime) in the parent
	 * between the pointer to treeNode n and the pointer
	 * to the neighbor.
	 */
	treeNode nParent = {{0}};
	PAFFS_RESULT parent_r = getParent(dev, n, &nParent);
	if(parent_r != PAFFS_OK && parent_r != PAFFS_NOPARENT)
		return parent_r;

	// Remove key and pointer from treeNode.

	PAFFS_RESULT r = remove_entry_from_node(dev, n, key);
	if(r != PAFFS_OK)
		return r;

	/* Case:  deletion from root.
	 */

	if (parent_r == PAFFS_NOPARENT)
		return adjust_root(dev, n);


	/* Case:  deletion from a treeNode below the root.
	 * (Rest of function body.)
	 */

	/* Determine minimum allowable size of treeNode,
	 * to be preserved after deletion.
	 */

	//cut(btree_leaf/branch_order) - 1 oder ohne -1?
	min_keys = n->is_leaf ? cut(btree_leaf_order) : cut(btree_branch_order);

	/* Case:  treeNode stays at or above minimum.
	 * (The simple case.)
	 */

	if (n->num_keys >= min_keys)
			return updateTreeNodePath(dev, n);


	/* Case:  treeNode falls below minimum.
	 * Either coalescence or redistribution
	 * is needed.
	 */

	neighbor_index = get_neighbor_index( dev, n );
	k_prime_index = neighbor_index == -1 ? 0 : neighbor_index;
	k_prime = nParent.keys[k_prime_index];
	p_addr neighbor_addr = neighbor_index == -1 ? *getPointerAsAddr(nParent.pointers, 1) :
			*getPointerAsAddr(nParent.pointers, neighbor_index);
	r = readTreeNode(dev, neighbor_addr, &neighbor);
	if(r != PAFFS_OK)
		return r;

	capacity = neighbor.is_leaf ? btree_leaf_order : btree_branch_order;	//-1?

	/* Coalescence. */

	if (neighbor.num_keys + n->num_keys < capacity)
		//Write of Nodes happens in coalesce_nodes
		return coalesce_nodes(dev, n, &neighbor, neighbor_index, k_prime);

	/* Redistribution. */

	else
		return redistribute_nodes(dev, n, &neighbor, neighbor_index, k_prime_index, k_prime);
}

