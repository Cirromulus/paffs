/**
 * File created on 18.05.2016
 * Author: Pascal Pieper
 */

#ifndef __PAFFS_BTREE_H__
#define __PAFFS_BTREE_H__

#include <stdbool.h>
#include <stddef.h>

#include "paffs.hpp"
#include "treeCache.hpp"
#include "treeTypes.hpp"
#include "treequeue.hpp" //Just for printing debug info in tree

namespace paffs{

class Btree{
	Device* dev;
public:
	TreeCache cache;
	Btree(Device* dev): dev(dev), cache(TreeCache(dev)){};

	Result insertInode(Inode* inode);
	Result getInode(InodeNo number, Inode* outInode);
	Result updateExistingInode(Inode* inode);
	Result deleteInode(InodeNo number);
	Result findFirstFreeNo(InodeNo* outNumber);

	void print_tree();
	Result start_new_tree();
	Result commitCache();
	void wipeCache();
private:
	void print_leaves(TreeCacheNode* c);
	void print_queued_keys_r(queue_s* q);
	void print_keys(TreeCacheNode* c);

	Addr* getPointerAsAddr(char* pointers, unsigned int pos);
	Inode* getPointerAsInode(char* pointers, unsigned int pos);
	void insertAddrInPointer(char* pointers, Addr* addr, unsigned int pos);
	void insertInodeInPointer(char* pointers, Inode* inode, unsigned int pos);
	Result updateAddrInTreeCacheNode(TreeCacheNode* node, Addr* old, Addr* newAddress);
	bool isTreeCacheNodeEqual(TreeCacheNode* left, TreeCacheNode* right);

	//bool isEqual(TreeCacheNode* left, TreeCacheNode* right);
	int height(TreeCacheNode * root );
	//Length is number of Kanten, not Knoten
	int length_to_root(TreeCacheNode * child );
	//Path is root first, child last
	Result path_from_root(TreeCacheNode * child, Addr* path, unsigned int* lengthOut);
	int find_range(TreeCacheNode * root, InodeNo key_start, InodeNo key_end,
					int returned_keys[], void * returned_pointers[]);
	Result find_branch( TreeCacheNode* target, TreeCacheNode** outtreeCacheNode);
	Result find_leaf( InodeNo key, TreeCacheNode** outtreeCacheNode);
	Result find_in_leaf (TreeCacheNode* leaf, InodeNo key, Inode* outInode);
	Result find(InodeNo key, Inode* outInode);
	int cut(int length );


	// Insertion.

	int get_left_index(TreeCacheNode * parent, TreeCacheNode * left);
	Result insert_into_leaf(TreeCacheNode * leaf, Inode * pointer );
	Result insert_into_leaf_after_splitting(TreeCacheNode * leaf, Inode * newInode);
	Result insert_into_node(TreeCacheNode * newNode,
				int left_index, InodeNo key, TreeCacheNode * right);
	Result insert_into_node_after_splitting(TreeCacheNode * old_node, int left_index,
					InodeNo key, TreeCacheNode * right);
	Result insert_into_parent(TreeCacheNode * left, InodeNo key, TreeCacheNode * right);
	Result insert_into_new_root(TreeCacheNode * left, InodeNo key, TreeCacheNode * right);
	Result insert(Inode* value);

	// Deletion.

	int get_neighbor_index(TreeCacheNode * n );
	Result adjust_root(TreeCacheNode * root);
	Result coalesce_nodes(TreeCacheNode * n, TreeCacheNode * neighbor, int neighbor_index, int k_prime);
	Result redistribute_nodes(TreeCacheNode * n, TreeCacheNode * neighbor, int neighbor_index,
					int k_prime_index, int k_prime);
	Result delete_entry(TreeCacheNode * n, InodeNo key);
	Result remove_entry_from_node(TreeCacheNode * n, InodeNo key);

};
}
#endif
