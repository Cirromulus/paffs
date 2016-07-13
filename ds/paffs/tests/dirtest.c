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

void printFile(unsigned int offs, unsigned int bytes, const char* path){
	paffs_obj *fil = paffs_open(path, PAFFS_FR);

	unsigned int bytesread = 0;
	char* out = malloc (bytes + 1);
	memset(out, 0, bytes + 1);

	PAFFS_RESULT r = paffs_seek(fil, offs, PAFFS_SEEK_SET);
	if(r != PAFFS_OK){
		free(out);
		printf("%s\n", paffs_err_msg(r));
		return;
	}
	r  = paffs_read(fil, out, bytes, &bytesread);
	if(r != PAFFS_OK){
		free(out);
		printf("%s\n", paffs_err_msg(r));
		return;
	}
	paffs_close(fil);

	out[bytesread] = 0;
	printf("Read '%s': %s\n", path, out);
	free (out);
}

void printWholeFile(const char* path){
	paffs_objInfo fileInfo = {0};
	PAFFS_RESULT r = paffs_getObjInfo(path, &fileInfo);
	if(r != PAFFS_OK){
		printf("%s\n", paffs_err_msg(r));
		return;
	}
	printFile(0, fileInfo.size, path);

}

void dirTest(){
	paffs_start_up();

	PAFFS_RESULT r = paffs_mnt("1");

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
	
	paffs_obj *fil = paffs_open("/b/file", PAFFS_FW);

	//first write ----
	char t[] = "Dies ist ein sehr langer Text";	//29 chars
	char* tl = (char*) malloc(25*strlen(t)+2);

	for(int i = 0; i < 25; i++){
		memcpy(&tl[i * strlen(t)], t, strlen(t));
	}
	tl[25*strlen(t)] = '+';
	tl[25*strlen(t) + 1] = 0;
	tl[511] = '!';
	tl[512] = '#';


	if(fil == NULL)
		printf("%s\n", paffs_err_msg(paffs_lasterr));

	unsigned int bytes = 0;
	r = paffs_write(fil, tl, strlen(tl), &bytes);
	if(r != PAFFS_OK)
			printf("%s\n", paffs_err_msg(r));
	printf("Wrote content '%s' 25 times to file\n", t);
	// ----- first write


	printWholeFile("/b/file");


	//read misaligned ---
	paffs_objInfo fileInfo = {0};
	r = paffs_seek(fil, 9, PAFFS_SEEK_SET);
	r = paffs_getObjInfo("/b/file", &fileInfo);
	if(r != PAFFS_OK)
			printf("%s\n", paffs_err_msg(r));

	char* out = malloc(fileInfo.size - 8);
	r = paffs_read(fil, out, fileInfo.size - 9, &bytes);
	if(r != PAFFS_OK)
			printf("%s\n", paffs_err_msg(r));

	out[fileInfo.size - 9] = 0;
	printf("Read Contents (+9):      %s\n", out);
	free(out);
	//--- read misaligned

	//while(getchar() == EOF);

	//write misaligned - over boundarys ----
	r = paffs_seek(fil, -5, PAFFS_SEEK_END);
	if(r != PAFFS_OK)
			printf("%s\n", paffs_err_msg(r));

	char testlauf[] = "---Testlauf---";
	r = paffs_write(fil, testlauf, strlen(testlauf), &bytes);
	if(r != PAFFS_OK)
			printf("%s\n", paffs_err_msg(r));
	// ---- write misaligned 1

	printWholeFile("/b/file");
	//while(getchar() == EOF);


	//write misaligned - end misaligned ----
	r = paffs_seek(fil, -11, PAFFS_SEEK_END);
	if(r != PAFFS_OK)
			printf("%s\n", paffs_err_msg(r));

	char kurz[] = "kurz";
	r = paffs_write(fil, kurz, strlen(kurz), &bytes);
	if(r != PAFFS_OK)
			printf("%s\n", paffs_err_msg(r));
	// ---- write misaligned 2



	printWholeFile("/b/file");


	//write misaligned - write over boundarys ----
	r = paffs_seek(fil, 508, PAFFS_SEEK_SET);
	if(r != PAFFS_OK)
			printf("%s\n", paffs_err_msg(r));

	r = paffs_write(fil, testlauf, strlen(testlauf), &bytes);
	if(r != PAFFS_OK)
			printf("%s\n", paffs_err_msg(r));
	// ---- write misaligned 2



	printWholeFile("/b/file");

	r = paffs_getObjInfo("/b/file", &fileInfo);
	if(r != PAFFS_OK)
			printf("%s\n", paffs_err_msg(r));
	printInfo(&fileInfo);




	while(getchar() == EOF);
	free (tl);
	paffs_close(fil);
}

int main( int argc, char ** argv ) {

	dirTest();
}
