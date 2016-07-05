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
#include <unistd.h>
#include "paffs.h"

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
	paffs_closedir(rewt);
}

void printInfo(paffs_objInfo* obj){
	if(obj == NULL){
		printf("invalid object!");
		return;
	}
	char buff[20];
	printf("Listing Info:\n");
	printf("\t size: %d\n", obj->size);
	time_t rawtime = obj->created;
	strftime(buff, 20, "%Y-%m-%d %H:%M:%S", localtime(&rawtime));
	printf("\t created: %s\n", buff);
	rawtime = obj->modified;
	strftime(buff, 20, "%Y-%m-%d %H:%M:%S", localtime(&rawtime));
	printf("\t modified: %s\n", buff);
	printf("\t Permission rwx: %d%d%d\n", (obj->perm & PAFFS_R) != 0, (obj->perm & PAFFS_W) != 0, (obj->perm & PAFFS_X) != 0);
}

void dirTest(){
	paffs_start_up();

	PAFFS_RESULT r = paffs_mnt("1");

	while(getchar() == EOF);

	paffs_permission p = 0;
	r = paffs_mkdir("/a", p);
	if(r != PAFFS_OK)
		printf("%s\n", paffs_err_msg(r));

	r = paffs_mkdir("/b", p);
	if(r != PAFFS_OK)
			printf("%s\n", paffs_err_msg(r));

	r = paffs_mkdir("/b/c", p);
	if(r != PAFFS_OK)
			printf("%s\n", paffs_err_msg(r));

	r = paffs_touch ("/b/file");
	if(r != PAFFS_OK)
			printf("%s\n", paffs_err_msg(r));


	listDir("/");

	listDir("/b/");

	listDir("/a");
	
	while(getchar() == EOF);

	paffs_obj *fil = paffs_open("/b/file/", PAFFS_FW);
	char t[] = "Pimmelmann";
	if(fil == NULL)
		printf("%s\n", paffs_err_msg(paffs_lasterr));

	unsigned int bytes = 0;
	r = paffs_write(fil, t, strlen(t)+1, &bytes);
	if(r != PAFFS_OK)
			printf("%s\n", paffs_err_msg(r));
	printf("Wrote content '%s' to file\n", t);

	while(getchar() == EOF);

	r = paffs_seek(fil, 0, PAFFS_SEEK_SET);

	paffs_objInfo fileInfo = {0};
	r = paffs_getObjInfo("/b/file", &fileInfo);
	if(r != PAFFS_OK)
			printf("%s\n", paffs_err_msg(r));
	printInfo(&fileInfo);

	char *out = malloc(fileInfo.size );

	paffs_read(fil, out, fileInfo.size, &bytes);

	printf("Read Contents: %s\n", out);

	free(out);

	while(getchar() == EOF);

	r = paffs_touch("/b/file");
	if(r != PAFFS_OK)
			printf("%s\n", paffs_err_msg(r));

	r = paffs_getObjInfo("/b/file", &fileInfo);
	if(r != PAFFS_OK)
			printf("%s\n", paffs_err_msg(r));
	printInfo(&fileInfo);
}

int main( int argc, char ** argv ) {
	dirTest();
}
