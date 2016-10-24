/*
 * dirtest.c
 *
 *  Created on: 19.05.2016
 *      Author: Pascal Pieper
 */

#include <stdbool.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <stdio.h>
#include <time.h>
#include "paffs.h"
#include "treeCache.h"
#include "superblock.h"



void printSuperIndex(superIndex* ind){
	printf("No:\t\t%d", ind->no);
	printf("Roonode addr.: \t%u:%u", extractLogicalArea(ind->rootNode), extractPage(ind->rootNode));
	printf("areaMap: (first four entrys)\n");
	for(int i = 0; i < 4; i ++){
		printf("\t%d->%d\n", i, ind->areaMap[i].position);
		printf("\tType: %d\n", ind->areaMap[i].type);
		printf("\tSummary Present: %s\n", ind->areaMap[i].areaSummary != 0 ? "true" : "false");
		printf("\t----------------\n");
	}
}


int main( int argc, char ** argv ) {


	paffs_start_up();
	paffs_mnt("1");
	paffs_obj* fil = paffs_open("/foo", PAFFS_FC | PAFFS_FW);
	unsigned int bw;
	paffs_write(fil, "Hallo.", 7, &bw);
	paffs_close(fil);
	//Nothing new until here

	commitTreeCache(getDevice());
	print_tree(getDevice());


	printf("Committing super Index...");
	fflush(stdout);
	PAFFS_RESULT r = commitSuperIndex(getDevice());
	printf("%s\n", paffs_err_msg(r));
	if(r != PAFFS_OK)
		return -1;
	while(getchar() == EOF);

	printf("Reading super Index...");
	fflush(stdout);
	superIndex index;
	r = readSuperIndex(getDevice(), &index);
	printf("%s\n", paffs_err_msg(r));
	if(r != PAFFS_OK)
		return -1;
	printSuperIndex(&index);



	printf("Committing super Index again...");
	fflush(stdout);
	r = commitSuperIndex(getDevice());
	printf("%s\n", paffs_err_msg(r));
	if(r != PAFFS_OK)
		return -1;


	return 0;
}
