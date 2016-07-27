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
#include "paffs.h"


int main( int argc, char ** argv ) {
	printf("Branch-Order: %d, Leaf-Order: %d\nSpace in Pointers: %lu, Sizeof pInode: %lu\n", BRANCH_ORDER, LEAF_ORDER, BRANCH_ORDER * sizeof(p_addr), sizeof(pInode));
	printf("Size of TreeNode: %d\n", sizeof(treeNode));
	paffs_start_up();

	PAFFS_RESULT r = paffs_mnt("1");
	p_dev * device = getDevice();
	pInode test1;
	memset(&test1, 0x88, sizeof(pInode));
	test1.no = 1;

	pInode test2;
	memset(&test2, 0xAA, sizeof(pInode));
	test2.no = 2;

	pInode test3;
	memset(&test3, 0x0F, sizeof(pInode));
	test3.no = 3;

//	while(getchar() == EOF);
	r = insertInode(device, &test1);
	if(r != PAFFS_OK)
		printf("%s!\n", paffs_err_msg(r));
//	while(getchar() == EOF);
	r = insertInode(device, &test2);
	if(r != PAFFS_OK)
		printf("%s!\n", paffs_err_msg(r));
	r = insertInode(device, &test3);
	if(r != PAFFS_OK)
		printf("%s!\n", paffs_err_msg(r));
	while(getchar() == EOF);



}
