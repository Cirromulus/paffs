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
	printf("No:\t\t%d\n", ind->no);
	printf("Roonode addr.: \t%u:%u\n", extractLogicalArea(ind->rootNode), extractPage(ind->rootNode));
	printf("areaMap: (first eight entrys)\n");
	for(int i = 0; i < 8; i ++){
		printf("\t%d->%d\n", i, ind->areaMap[i].position);
		printf("\tType: %d\n", ind->areaMap[i].type);
		if(ind->areaMap[i].has_areaSummary){
			unsigned int free = 0, used = 0, dirty = 0;
			for(unsigned int j = 0; j < getDevice()->param.pages_per_area; j++){
				if(ind->areaMap[i].areaSummary[j] == FREE)
					free++;
				if(ind->areaMap[i].areaSummary[j] == USED)
					used++;
				if(ind->areaMap[i].areaSummary[j] == DIRTY)
					dirty++;
			}
			printf("\tFree/Used/Dirty Pages: %u/%u/%u\n", free, used, dirty);
		}else{
			printf("\tSummary not present.\n");
		}
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

	printf("Printing initial super Index...\n");
	superIndex actual;
	actual.no = 0;
	actual.rootNode = getRootnodeAddr(getDevice());
	actual.areaMap = getDevice()->areaMap;
	printSuperIndex(&actual);


	printf("Committing super Index...");
	fflush(stdout);
	PAFFS_RESULT r = commitSuperIndex(getDevice());
	printf("%s\n", paffs_err_msg(r));
	if(r != PAFFS_OK)
		return -1;
	//while(getchar() == EOF);

	printf("Reading super Index...");
	fflush(stdout);
	superIndex index = {0};
	p_area test_area_map[getDevice()->param.areas_no];	//Normally, the version in paffs_flasj is used
	memset(test_area_map, 0, sizeof(p_area) * getDevice()->param.areas_no);
	index.areaMap = test_area_map;

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
	printSuperIndex(&index);

	return 0;
}
