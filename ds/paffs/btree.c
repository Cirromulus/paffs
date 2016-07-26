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


PAFFS_RESULT insertInode( p_dev* dev, pInode* inode){
	p_addr rootnode_addr = getRootnodeAddr(dev);
	treeNode root;
	PAFFS_RESULT r = readTreeNode(dev, rootnode_addr, &root);
	if(r != PAFFS_OK)
		return r;

	return PAFFS_NIMPL;
}

PAFFS_RESULT getInode( p_dev* dev, pInode_no number, pInode* outInode){
	return PAFFS_NIMPL;
}

PAFFS_RESULT modifyInode( p_dev* dev, pInode* inode){
	return PAFFS_NIMPL;
}

PAFFS_RESULT deleteInode( p_dev* dev, pInode_no number){
	return PAFFS_NIMPL;
}


/* Prints the bottom row of keys
 * of the tree (with their respective
 * pointers, if the verbose_output flag is set.
 */
void print_leaves(p_dev* dev, treeNode * root) {
/*        int i;
        treeNode * c = root;
        if (root == NULL) {
                printf("Empty tree.\n");
                return;
        }
        while (!c->is_leaf)
                c = c->pointers[0];
        while (true) {
                for (i = 0; i < c->num_keys; i++) {
                        if (verbose_output)
                                printf("%lx ", (unsigned long)c->pointers[i]);
                        printf("%d ", c->keys[i]);
                }
                if (verbose_output)
                        printf("%lx ", (unsigned long)c->pointers[btree_order - 1]);
                if (c->pointers[btree_order - 1] != NULL) {
                        printf(" | ");
                        c = c->pointers[btree_order - 1];
                }
                else
                        break;
        }
        printf("\n");*/
	printf("Not Implemented");
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
int path_to_root( p_dev* dev, treeNode * root, treeNode * child ) {
        int length = 0;
        treeNode c = *child;
        while (c.parent != 0) {
                PAFFS_RESULT r = readTreeNode(dev, c.parent, &c);
                if(r != PAFFS_OK){
                	paffs_lasterr = r;
                	return -1;
                }
                length++;
        }
        return length;
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
void print_tree( p_dev* dev, treeNode * root) {

/*        treeNode * n = NULL;
        int i = 0;
        int rank = 0;
        int new_rank = 0;

        if (root == NULL) {
                printf("Empty tree.\n");
                return;
        }
        treeNode *queue = NULL;
        enqueue(queue, root);
        while( queue != NULL ) {
                n = dequeue(queue);
                if (n->parent != NULL && n == n->parent->pointers[0]) {
                        new_rank = path_to_root( root, n );
                        if (new_rank != rank) {
                                rank = new_rank;
                                printf("\n");
                        }
                }
                //printf("(%lx)", (unsigned long)n);
                for (i = 0; i < n->num_keys; i++) {
                	    //printf("%lx ", (unsigned long)n->pointers[i]);
                        printf("%d ", n->keys[i]);
                }
                if (!n->is_leaf)
                        for (i = 0; i <= n->num_keys; i++)
                                enqueue(queue, n->pointers[i]);
                if (verbose_output) {
                        if (n->is_leaf) 
                                printf("%lx ", (unsigned long)n->pointers[btree_order - 1]);
                        else
                                printf("%lx ", (unsigned long)n->pointers[n->num_keys]);
                }
                printf("| ");
        }
        printf("\n");*/
	printf("Not Implemented");
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
                n = n->pointers[btree_order - 1];
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
        treeNode c;
        PAFFS_RESULT r = readTreeNode(dev, getRootnodeAddr(dev), &c);
        if(r != PAFFS_OK)
        	return r;


        if (c.pointers[0] == 0) {
                PAFFS_DBG(PAFFS_TRACE_TREE, "Empty tree.");
                return PAFFS_NF;
        }
        while (!c.is_leaf) {
/*                if (verbose) {
                        printf("[");
                        for (i = 0; i < c->num_keys - 1; i++)
                                printf("%d ", c->keys[i]);
                        printf("%d] ", c->keys[i]);
                }*/
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
		/*printf("Leaf [");
		for (i = 0; i < c->num_keys - 1; i++)
				printf("%d ", c->keys[i]);
		printf("%d] ->\n", c->keys[i]);*/
        * outTreenode = c;
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
    treeNode c;
    PAFFS_RESULT r = find_leaf( dev, key, &c);
    if(r != PAFFS_OK)
    	return r;
    return find_in_leaf(&c, key, outInode);
}

PAFFS_RESULT find_first_free_key( p_dev* dev, treeNode * root, pInode_no* outNumber){
	treeNode c;
	c = *root;
	pInode_no free_no = 0;
	while(c.pointers[0] != 0){
		if(!c.is_leaf){
			p_addr* addr = getPointerAsAddr(c.pointers, 0); //leftmost child
			PAFFS_RESULT r = readTreeNode(dev, *addr, &c);
			if(r != PAFFS_OK)
				return r;
			continue;
		}
		for(unsigned int k = 0; k < c.num_keys; k ++){
			if(c.keys[k] > free_no)
				return free_no;
			free_no ++;
		}
		p_addr* addr = getPointerAsAddr(c.pointers, btree_order-1); //Next leaf ("cousin")
		PAFFS_RESULT r = readTreeNode(dev, *addr, &c);
		if(r != PAFFS_OK)
			return r;
	}
	return free_no;
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
        new_node->keys = malloc( (btree_order - 1) * sizeof(int) );
        if (new_node->keys == NULL) {
                perror("New treeNode keys array.");
                return NULL;
        }
        new_node->pointers = malloc( btree_order * sizeof(void *) );
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


PAFFS_RESULT updateTreeNode( p_dev* dev, treeNode* node){
	if(node == NULL)
		return PAFFS_EINVAL;

	p_addr old_addr = node->self;
	PAFFS_RESULT r = writeTreeNode(dev, node);
	if(r != PAFFS_OK)
		return r;

	if(node->parent != 0){	//We are non-root
		treeNode parent;
		r = readTreeNode(dev, node->parent, &parent);
		int i;
	    for (i = 0; i < parent.num_keys; i++)
	            if (*getPointerAsAddr(parent.pointers, i) == old_addr) break;
	    if (i == parent.num_keys){
	    	PAFFS_DBG(PAFFS_TRACE_ERROR, "ERROR (BUG?): Detached Treenode");
			return PAFFS_FAIL;
	    }
	    insertAddrInPointer(parent.pointers, &node->self, i);
	    +#ÖÖ
		//What about siblings pointing to parent?!
		return updateTreeNode(dev, &parent);
	}
	return PAFFS_OK;
}


/* Helper function used in insert_into_parent
 * to find the index of the parent's pointer to 
 * the treeNode to the left of the key to be inserted.
 */
int get_left_index(treeNode * parent, treeNode * left) {

        int left_index = 0;
        while (left_index <= parent->num_keys && 
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
                leaf->pointers[i] = leaf->pointers[i - 1];
        }
        leaf->keys[insertion_point] = newInode->no;
        leaf->num_keys++;
        insertInodeInPointer(leaf->pointers, newInode, insertion_point);

        return updateTreeNode(dev, leaf);
}


/* Inserts a new key and pointer
 * to a new pinode into a leaf so as to exceed
 * the tree's order, causing the leaf to be split
 * in half.
 * ***further Tree-action required ***
 */
treeNode * insert_into_leaf_after_splitting(treeNode * root, treeNode * leaf, pInode_no key, pInode * pointer) {

        treeNode new_leaf = {0};
        int * temp_keys;
        void ** temp_pointers;
        int insertion_index, split, new_key, i, j;

        new_leaf.is_leaf = true;

        temp_keys = malloc( btree_order * sizeof(int) );
        if (temp_keys == NULL) {
                perror("Temporary keys array.");
                return NULL;
        }

        temp_pointers = malloc( btree_order * sizeof(void *) );
        if (temp_pointers == NULL) {
                perror("Temporary pointers array.");
                return NULL;
        }

        insertion_index = 0;
        while (insertion_index < btree_order - 1 && leaf->keys[insertion_index] < key)
                insertion_index++;

        for (i = 0, j = 0; i < leaf->num_keys; i++, j++) {
                if (j == insertion_index) j++;
                temp_keys[j] = leaf->keys[i];
                temp_pointers[j] = leaf->pointers[i];
        }

        temp_keys[insertion_index] = key;
        temp_pointers[insertion_index] = pointer;

        leaf->num_keys = 0;

        split = cut(btree_order - 1);

        for (i = 0; i < split; i++) {
                leaf->pointers[i] = temp_pointers[i];
                leaf->keys[i] = temp_keys[i];
                leaf->num_keys++;
        }

        for (i = split, j = 0; i < btree_order; i++, j++) {
                new_leaf->pointers[j] = temp_pointers[i];
                new_leaf->keys[j] = temp_keys[i];
                new_leaf->num_keys++;
        }

        free(temp_pointers);
        free(temp_keys);

        new_leaf->pointers[btree_order - 1] = leaf->pointers[btree_order - 1];
        leaf->pointers[btree_order - 1] = new_leaf;

        for (i = leaf->num_keys; i < btree_order - 1; i++)
                leaf->pointers[i] = NULL;
        for (i = new_leaf->num_keys; i < btree_order - 1; i++)
                new_leaf->pointers[i] = NULL;

        new_leaf->parent = leaf->parent;
        new_key = new_leaf->keys[0];

        return insert_into_parent(root, leaf, new_key, new_leaf);
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
                node->pointers[i + 1] = node->pointers[i];
                node->keys[i] = node->keys[i - 1];
        }
        insertAddrInPointer(node->pointers, &right->self, left_index + 1);
        node->keys[left_index] = key;
        node->num_keys++;
        return updateTreeNode(dev, node);
}


/* Inserts a new key and pointer to a treeNode
 * into a treeNode, causing the treeNode's size to exceed
 * the order, and causing the treeNode to split into two.
 * *** further Tree-action pending ***
 */
treeNode * insert_into_node_after_splitting(treeNode * root, treeNode * old_node, int left_index,
                pInode_no key, treeNode * right) {

        int i, j, split, k_prime;
        treeNode * new_node, * child;
        int * temp_keys;
        treeNode ** temp_pointers;

        /* First create a temporary set of keys and pointers
         * to hold everything in order, including
         * the new key and pointer, inserted in their
         * correct places. 
         * Then create a new treeNode and copy half of the
         * keys and pointers to the old treeNode and
         * the other half to the new.
         */

        temp_pointers = malloc( (btree_order + 1) * sizeof(treeNode *) );
        if (temp_pointers == NULL) {
                perror("Temporary pointers array for splitting nodes.");
                return NULL;
        }
        temp_keys = malloc( btree_order * sizeof(int) );
        if (temp_keys == NULL) {
                perror("Temporary keys array for splitting nodes.");
                return NULL;
        }

        for (i = 0, j = 0; i < old_node->num_keys + 1; i++, j++) {
                if (j == left_index + 1) j++;
                temp_pointers[j] = old_node->pointers[i];
        }

        for (i = 0, j = 0; i < old_node->num_keys; i++, j++) {
                if (j == left_index) j++;
                temp_keys[j] = old_node->keys[i];
        }

        temp_pointers[left_index + 1] = right;
        temp_keys[left_index] = key;

        /* Create the new treeNode and copy
         * half the keys and pointers to the
         * old and half to the new.
         */  
        split = cut(btree_order);
        new_node = make_node();
        old_node->num_keys = 0;
        for (i = 0; i < split - 1; i++) {
                old_node->pointers[i] = temp_pointers[i];
                old_node->keys[i] = temp_keys[i];
                old_node->num_keys++;
        }
        old_node->pointers[i] = temp_pointers[i];
        k_prime = temp_keys[split - 1];
        for (++i, j = 0; i < btree_order; i++, j++) {
                new_node->pointers[j] = temp_pointers[i];
                new_node->keys[j] = temp_keys[i];
                new_node->num_keys++;
        }
        new_node->pointers[j] = temp_pointers[i];
        free(temp_pointers);
        free(temp_keys);
        new_node->parent = old_node->parent;
        for (i = 0; i <= new_node->num_keys; i++) {
                child = new_node->pointers[i];
                child->parent = new_node;
        }

        /* Insert a new key into the parent of the two
         * nodes resulting from the split, with
         * the old treeNode to the left and the new to the right.
         */

        return insert_into_parent(root, old_node, k_prime, new_node);
}



/* Inserts a new treeNode (leaf or internal treeNode) into the B+ tree.
 * Returns the root of the tree after insertion.
 * *** further Tree-action pending ***
 */
treeNode * insert_into_parent(treeNode * root, treeNode * left, pInode_no key, treeNode * right) {

        int left_index;
        treeNode * parent;

        parent = left->parent;

        /* Case: new root. */

        if (parent == NULL)
                return insert_into_new_root(left, key, right);

        /* Case: leaf or treeNode. (Remainder of
         * function body.)  
         */

        /* Find the parent's pointer to the left 
         * treeNode.
         */

        left_index = get_left_index(parent, left);


        /* Simple case: the new key fits into the treeNode.
         */

        if (parent->num_keys < btree_order - 1)
                return insert_into_node(root, parent, left_index, key, right);

        /* Harder case:  split a treeNode in order
         * to preserve the B+ tree properties.
         */

        return insert_into_node_after_splitting(root, parent, left_index, key, right);
}


/* Creates a new root for two subtrees
 * and inserts the appropriate key into
 * the new root.
 */
PAFFS_RESULT insert_into_new_root(p_dev* dev, treeNode * left, pInode_no key, treeNode * right) {

        treeNode root;
        PAFFS_RESULT r;
        root.keys[0] = key;
        insertAddrInPointer(root.pointers, &left->self, 0);
        insertAddrInPointer(root.pointers, &right->self, 1);
        root.num_keys++;
        root.parent = 0;
        p_addr future_root_addr = 0;
        r = getNextUsedAddr(dev, INDEXAREA, &future_root_addr);
		if(r != PAFFS_OK){
			PAFFS_DBG(PAFFS_TRACE_ERROR, "ERROR: Not handled exception, next Address indeterminable");
			return r;
		}
        left->parent = future_root_addr;
        right->parent = future_root_addr;
        r = writeTreeNode(dev, left);
		if(r != PAFFS_OK)
			return r;
		r = writeTreeNode(dev, right);
		if(r != PAFFS_OK)
			return r;

        r = writeTreeNode(dev, &root);
        if(r != PAFFS_OK)
        	return r;

        return PAFFS_OK;
}



/* start a new tree.
 * So init rootnode
 */
PAFFS_RESULT start_new_tree(p_dev* dev) {

        treeNode root;
        memset(&root, 0, sizeof(treeNode));
        return writeTreeNode(dev, &root);
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
        /* Case: the tree does not exist yet.
         * Start a new tree.
         */
        r = readTreeNode(dev, getRootnodeAddr(dev), &node);
        if (r != PAFFS_OK)
        	return r;

        if(extractLogicalArea(node.self) > dev->param.areas_no){	//Rootnode not initialized
        	PAFFS_DBG(PAFFS_TRACE_BUG, "BUG: Rootnode not initialized");
			return PAFFS_BUG;
        }


        /* Case: the tree already exists.
         * (Rest of function body.)
         */

        r = find_leaf(dev, value->no, &node);
        if(r != PAFFS_OK)
        	return r;

        /* Case: leaf has room for key and pointer.
         */

        if (node.num_keys < btree_order - 1) {
                return insert_into_leaf(dev, &node, value);
        }


        /* Case:  leaf must be split.
         */

        return insert_into_leaf_after_splitting(root, leaf, key, new);
}



// DELETION.

/* Utility function for deletion.  Retrieves
 * the index of a treeNode's nearest neighbor (sibling)
 * to the left if one exists.  If not (the treeNode
 * is the leftmost child), returns -1 to signify
 * this special case.
 */
int get_neighbor_index( treeNode * n ) {

        int i;

        /* Return the index of the key to the left
         * of the pointer in the parent pointing
         * to n.  
         * If n is the leftmost child, this means
         * return -1.
         */
        for (i = 0; i <= n->parent->num_keys; i++)
                if (n->parent->pointers[i] == n)
                        return i - 1;

        // Error state.
        printf("Search for nonexistent pointer to treeNode in parent.\n");
        printf("treeNode:  %#lx\n", (unsigned long)n);
        return -1;
}


treeNode * remove_entry_from_node(treeNode * n, pInode_no key, treeNode * pointer) {

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
        while (n->pointers[i] != pointer)
                i++;
        for (++i; i < num_pointers; i++)
                n->pointers[i - 1] = n->pointers[i];


        // One key fewer.
        n->num_keys--;

        // Set the other pointers to NULL for tidiness.
        // A leaf uses the last pointer to point to the next leaf.
        if (n->is_leaf)
                for (i = n->num_keys; i < btree_order - 1; i++)
                        n->pointers[i] = NULL;
        else
                for (i = n->num_keys + 1; i < btree_order; i++)
                        n->pointers[i] = NULL;

        return n;
}


treeNode * adjust_root(treeNode * root) {

        treeNode * new_root;

        /* Case: nonempty root.
         * Key and pointer have already been deleted,
         * so nothing to be done.
         */

        if (root->num_keys > 0)
                return root;

        /* Case: empty root. 
         */

        // If it has a child, promote 
        // the first (only) child
        // as the new root.

        if (!root->is_leaf) {
                new_root = root->pointers[0];
                new_root->parent = NULL;
        }

        // If it is a leaf (has no children),
        // then the whole tree is empty.

        else
                new_root = NULL;

        free(root->keys);
        free(root->pointers);
        free(root);

        return new_root;
}


/* Coalesces a treeNode that has become
 * too small after deletion
 * with a neighboring treeNode that
 * can accept the additional entries
 * without exceeding the maximum.
 */
treeNode * coalesce_nodes(treeNode * root, treeNode * n, treeNode * neighbor, int neighbor_index, int k_prime) {

        int i, j, neighbor_insertion_index, n_end;
        treeNode * tmp;

        /* Swap neighbor with treeNode if treeNode is on the
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
                        neighbor->pointers[i] = n->pointers[j];
                        neighbor->num_keys++;
                        n->num_keys--;
                }

                /* The number of pointers is always
                 * one more than the number of keys.
                 */

                neighbor->pointers[i] = n->pointers[j];

                /* All children must now point up to the same parent.
                 */

                for (i = 0; i < neighbor->num_keys + 1; i++) {
                        tmp = (treeNode *)neighbor->pointers[i];
                        tmp->parent = neighbor;
                }
        }

        /* In a leaf, append the keys and pointers of
         * n to the neighbor.
         * Set the neighbor's last pointer to point to
         * what had been n's right neighbor.
         */

        else {
                for (i = neighbor_insertion_index, j = 0; j < n->num_keys; i++, j++) {
                        neighbor->keys[i] = n->keys[j];
                        neighbor->pointers[i] = n->pointers[j];
                        neighbor->num_keys++;
                }
                neighbor->pointers[btree_order - 1] = n->pointers[btree_order - 1];
        }

        root = delete_entry(root, n->parent, k_prime, n);
        free(n->keys);
        free(n->pointers);
        free(n); 
        return root;
}


/* Redistributes entries between two nodes when
 * one has become too small after deletion
 * but its neighbor is too big to append the
 * small treeNode's entries without exceeding the
 * maximum
 */
treeNode * redistribute_nodes(treeNode * root, treeNode * n, treeNode * neighbor, int neighbor_index,
                int k_prime_index, int k_prime) {  

        int i;
        treeNode * tmp;

        /* Case: n has a neighbor to the left. 
         * Pull the neighbor's last key-pointer pair over
         * from the neighbor's right end to n's left end.
         */

        if (neighbor_index != -1) {
                if (!n->is_leaf)
                        n->pointers[n->num_keys + 1] = n->pointers[n->num_keys];
                for (i = n->num_keys; i > 0; i--) {
                        n->keys[i] = n->keys[i - 1];
                        n->pointers[i] = n->pointers[i - 1];
                }
                if (!n->is_leaf) {
                        n->pointers[0] = neighbor->pointers[neighbor->num_keys];
                        tmp = (treeNode *)n->pointers[0];
                        tmp->parent = n;
                        neighbor->pointers[neighbor->num_keys] = NULL;
                        n->keys[0] = k_prime;
                        n->parent->keys[k_prime_index] = neighbor->keys[neighbor->num_keys - 1];
                }
                else {
                        n->pointers[0] = neighbor->pointers[neighbor->num_keys - 1];
                        neighbor->pointers[neighbor->num_keys - 1] = NULL;
                        n->keys[0] = neighbor->keys[neighbor->num_keys - 1];
                        n->parent->keys[k_prime_index] = n->keys[0];
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
                        n->pointers[n->num_keys] = neighbor->pointers[0];
                        n->parent->keys[k_prime_index] = neighbor->keys[1];
                }
                else {
                        n->keys[n->num_keys] = k_prime;
                        n->pointers[n->num_keys + 1] = neighbor->pointers[0];
                        tmp = (treeNode *)n->pointers[n->num_keys + 1];
                        tmp->parent = n;
                        n->parent->keys[k_prime_index] = neighbor->keys[0];
                }
                for (i = 0; i < neighbor->num_keys - 1; i++) {
                        neighbor->keys[i] = neighbor->keys[i + 1];
                        neighbor->pointers[i] = neighbor->pointers[i + 1];
                }
                if (!n->is_leaf)
                        neighbor->pointers[i] = neighbor->pointers[i + 1];
        }

        /* n now has one more key and one more pointer;
         * the neighbor has one fewer of each.
         */

        n->num_keys++;
        neighbor->num_keys--;

        return root;
}


/* Deletes an entry from the B+ tree.
 * Removes the pinode and its key and pointer
 * from the leaf, and then makes all appropriate
 * changes to preserve the B+ tree properties.
 */
treeNode * delete_entry( treeNode * root, treeNode * n, pInode_no key, void * pointer ) {

        int min_keys;
        treeNode * neighbor;
        int neighbor_index;
        int k_prime_index, k_prime;
        int capacity;

        // Remove key and pointer from treeNode.

        n = remove_entry_from_node(n, key, pointer);

        /* Case:  deletion from the root. 
         */

        if (n == root) 
                return adjust_root(root);


        /* Case:  deletion from a treeNode below the root.
         * (Rest of function body.)
         */

        /* Determine minimum allowable size of treeNode,
         * to be preserved after deletion.
         */

        min_keys = n->is_leaf ? cut(btree_order - 1) : cut(btree_order) - 1;

        /* Case:  treeNode stays at or above minimum.
         * (The simple case.)
         */

        if (n->num_keys >= min_keys)
                return root;

        /* Case:  treeNode falls below minimum.
         * Either coalescence or redistribution
         * is needed.
         */

        /* Find the appropriate neighbor treeNode with which
         * to coalesce.
         * Also find the key (k_prime) in the parent
         * between the pointer to treeNode n and the pointer
         * to the neighbor.
         */

        neighbor_index = get_neighbor_index( n );
        k_prime_index = neighbor_index == -1 ? 0 : neighbor_index;
        k_prime = n->parent->keys[k_prime_index];
        neighbor = neighbor_index == -1 ? n->parent->pointers[1] : 
                n->parent->pointers[neighbor_index];

        capacity = n->is_leaf ? btree_order : btree_order - 1;

        /* Coalescence. */

        if (neighbor->num_keys + n->num_keys < capacity)
                return coalesce_nodes(root, n, neighbor, neighbor_index, k_prime);

        /* Redistribution. */

        else
                return redistribute_nodes(root, n, neighbor, neighbor_index, k_prime_index, k_prime);
}



/* Master deletion function.
 */
treeNode * delete(treeNode * root, pInode_no key) {

        treeNode * key_leaf;
        pInode * key_pinode;

        key_pinode = find(root, key);
        key_leaf = find_leaf(root, key, false);
        if (key_pinode != NULL && key_leaf != NULL) {
                root = delete_entry(root, key_leaf, key, key_pinode);
                free(key_pinode);
        }
        return root;
}


void destroy_tree_nodes(treeNode * root) {
        int i;
        if (root->is_leaf)
                for (i = 0; i < root->num_keys; i++)
                        free(root->pointers[i]);
        else
                for (i = 0; i < root->num_keys + 1; i++)
                        destroy_tree_nodes(root->pointers[i]);
        free(root->pointers);
        free(root->keys);
        free(root);
}


treeNode * destroy_tree(treeNode * root) {
        destroy_tree_nodes(root);
        return NULL;
}
