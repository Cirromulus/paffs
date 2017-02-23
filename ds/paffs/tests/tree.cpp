
#ifdef __WIN32__
#include <io.h>
#include <windows.h>
#endif
#include <stdbool.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <stdio.h>

#include "btree.h"
#include "../paffs.hpp"

#include "treeCache.h"

#define RED   "\x1B[31m"
#define GRN   "\x1B[32m"
#define YEL   "\x1B[33m"
#define BLU   "\x1B[34m"
#define MAG   "\x1B[35m"
#define CYN   "\x1B[36m"
#define WHT   "\x1B[37m"
#define RESET "\x1B[0m"


int main( int argc, char ** argv ) {
	printf("Branch-Order: %u, Leaf-Order: %u\n"
			"Space in Pointers: %u Byte, "
			"Sizeof pInode: %u Byte\n", BRANCH_ORDER, LEAF_ORDER,
			BRANCH_ORDER * sizeof(p_addr), sizeof(pInode));
	printf("Size of TreeNode: %u\n", sizeof(treeNode));
	printf("Size of TreeCacheNode: %u\n", sizeof(treeCacheNode));

	treeNode a;
	printf("Size of TreeNode.as_branch.keys: %u\n", sizeof(a.as_branch.keys) / sizeof(pInode_no));
	printf("Size of TreeNode.as_leaf.keys: %u\n", sizeof(a.as_leaf.keys) / sizeof(pInode_no));

	paffs_start_up();

	PAFFS_RESULT r = paffs_mnt("1");
	p_dev * device = getDevice();

	//unsigned char values[] = {0xAA, 0x88, 0x03, 0x70, 0xF0};

	print_tree(device);
	for(int i = 5; i > 0; i--){
		printf("Insert nr. %d:", i);
		fflush(stdout);
		pInode test;
		memset(&test, 0, sizeof(pInode));
		test.no = i;

		r = insertInode(device, &test);
		if(r != PAFFS_OK){
			printf( RED "\t%s!" RESET "\n", paffs_err_msg(r));
			return -1;
		}else
			printf(GRN "\tOK" RESET "\n");
		print_tree(device);
		//while(getchar() == EOF);
	}
	print_tree(device);
	for(int i = 0; i <= 5; i++){
		pInode test = {0};

		printf("Get nr. %d: ", i);
		fflush(stdout);
		r = getInode(device, i, &test);
		if(r != PAFFS_OK){
			printf(RED "\t%s!" RESET "\n", paffs_err_msg(r));
			return -1;
		}else
			printf(GRN "\tFound Inode %d" RESET "\n", test.no);
	}

	//To test coalesce_nodes
	printf("Delete Node 3: ");
	fflush(stdout);
	r = deleteInode(device, 3);
	if(r != PAFFS_OK){
		printf(RED "\t %s" RESET "\n", paffs_err_msg(r));
		return -1;
	}
	printf(GRN "\t OK" RESET "\n");
	print_tree(device);

	//to test redistribute_nodes
	printf("Delete Node 4:");
	fflush(stdout);
	r = deleteInode(device, 4);
	if(r != PAFFS_OK){
		printf(RED "\t %s" RESET "\n", paffs_err_msg(r));
		print_tree(device);
		return -1;
	}
	printf(GRN "\t OK" RESET "\n");
	print_tree(device);

	printf("Delete Node 1:");
	fflush(stdout);
	r = deleteInode(device, 1);
	if(r != PAFFS_OK){
		printf(RED "\t %s" RESET "\n", paffs_err_msg(r));
		print_tree(device);
		return -1;
	}
	printf(GRN "\t OK" RESET "\n");
	print_tree(device);

	printf("\nCache-Hits: %d, Cache-Misses: %d\n\tHit ratio: %.5f%%\n",
			getCacheHits(),
			getCacheMisses(), 100*((float)getCacheHits())/(getCacheHits()+getCacheMisses()));

}
