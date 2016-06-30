/*
 * dirtest.c
 *
 *  Created on: 19.05.2016
 *      Author: Pascal Pieper
 */

#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include <time.h>
#include "paffs.h"
#include "driver/driver.h"

void listDir(const char* path){
	printf("Opening Dir '%s'.\n", path);
	paffs_dir* rewt = paffs_opendir(path);
	if(paffs_getLastErr() != PAFFS_OK)
			printf("%s\n", paffs_err_msg(paffs_getLastErr()));
	if(rewt == NULL){
		printf("Opendir: Result %s\n", paffs_err_msg(paffs_getLastErr()));
		return;
	}
	paffs_dirent* dir;
	while((dir = paffs_readdir(rewt)) != NULL){
			printf("\tFound item: \"%s\"\n", dir->name);
	}
}

void printInfo(paffs_objInfo* obj){
	if(obj == NULL){
		printf("invalid object!");
		return;
	}
	char buff[20];
	printf("Listing Info:\n");
	printf("\t size: %d\n", obj->size);
	strftime(buff, 20, "%Y-%m-%d %H:%M:%S", localtime(&obj->created));
	printf("\t created: %s\n", buff);
	strftime(buff, 20, "%Y-%m-%d %H:%M:%S", localtime(&obj->modified));
	printf("\t modified: %s\n", buff);
	printf("\t Permission rwx: %d%d%d\n", (obj->perm & PAFFS_R) != 0, (obj->perm & PAFFS_W) != 0, (obj->perm & PAFFS_X) != 0);
}

int main( int argc, char ** argv ) {

	paffs_start_up();

	PAFFS_RESULT r = paffs_mnt("1");

	paffs_permission p = 0;
	r = paffs_mkdir("/a", p);
	if(r != PAFFS_OK)
		printf("%s\n", paffs_err_msg(paffs_getLastErr()));

	r = paffs_mkdir("/b", p);
	if(r != PAFFS_OK)
			printf("%s\n", paffs_err_msg(paffs_getLastErr()));

	r = paffs_mkdir("/b/c", p);
	if(r != PAFFS_OK)
			printf("%s\n", paffs_err_msg(paffs_getLastErr()));

	r = paffs_touch ("/b/file");
	if(r != PAFFS_OK)
			printf("%s\n", paffs_err_msg(paffs_getLastErr()));


	listDir("/");

	listDir("/b/");

	listDir("/a");
	
	sleep(1);

	//paffs_obj* file = paffs_open("/b/file", PAFFS_FW);
	r = paffs_touch("/b/file");
	if(r != PAFFS_OK)
			printf("%s\n", paffs_err_msg(paffs_getLastErr()));

	paffs_objInfo fileInfo = {0};
	r = paffs_getObjInfo("/b/file", &fileInfo);
	if(r != PAFFS_OK)
			printf("%s\n", paffs_err_msg(paffs_getLastErr()));
	printInfo(&fileInfo);

}
