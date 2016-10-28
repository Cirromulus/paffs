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


int main( int argc, char ** argv ) {
	printf("Starting FS contexts...");
	fflush(stdout);
	PAFFS_RESULT r = paffs_start_up();
	printf("%s\n", paffs_err_msg(r));
	if(r != PAFFS_OK)
		return -1;

	printf("Formatting '1'...");
	fflush(stdout);
	r = paffs_format("1");
	printf("%s\n", paffs_err_msg(r));
	if(r != PAFFS_OK)
		return -1;

	printf("mounting '1'...");
	fflush(stdout);
	r = paffs_mnt("1");
	printf("%s\n", paffs_err_msg(r));
	if(r != PAFFS_OK)
		return -1;




	printf("creating file /foo and write something to it...");
	paffs_obj* fil = paffs_open("/foo", PAFFS_FC | PAFFS_FW);
	if(fil == NULL){
		printf("%s\n", paffs_err_msg(paffs_getLastErr()));
		return -1;
	}
	unsigned int bw;
	r = paffs_write(fil, "Hallo.", 7, &bw);
	printf("%s\n", paffs_err_msg(r));
	if(r != PAFFS_OK)
		return -1;
	paffs_close(fil);
	//Nothing new until here


	printf("Unmounting '1'...");
	fflush(stdout);
	r = paffs_unmnt("1");
	printf("%s\n", paffs_err_msg(r));
	if(r != PAFFS_OK)
		return -1;

	printf("mounting '1'...");
	fflush(stdout);
	r = paffs_mnt("1");
	printf("%s\n", paffs_err_msg(r));
	if(r != PAFFS_OK)
		return -1;

	printf("Reading file /foo ...");
	fil = paffs_open("/foo", PAFFS_FR);
	if(fil == NULL){
		printf("%s\n", paffs_err_msg(paffs_getLastErr()));
		return -1;
	}
	unsigned int br;
	char buf[7];
	r = paffs_read(fil, buf, 7, &br);
	printf("%s\n", paffs_err_msg(r));
	if(r != PAFFS_OK)
		return -1;
	paffs_close(fil);
	printf("read: %s\n", buf);

	if(strcmp("Hallo.", buf) != 0){
		printf("%s does not match 'Hallo.'\n", buf);
		return -1;
	}

	printf("Success.\n");
	return 0;
}
