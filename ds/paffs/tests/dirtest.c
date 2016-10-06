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
#include "btree.h"

void listDir(const char* path){
	printf("Opening Dir '%s'.\n", path);
	paffs_dir* rewt = paffs_opendir(path);
	if(rewt == NULL){
		printf("Opendir: Result %s\n", paffs_err_msg(paffs_getLastErr()));
		return;
	}
	paffs_dirent* dir;
	while((dir = paffs_readdir(rewt)) != NULL){
			printf("\tFound item: \"%s\"\n", dir->name);
	}
	if(paffs_getLastErr() != PAFFS_OK)
		//printf("Error reading Dir: %s\n", paffs_err_msg(paffs_getLastErr()));
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
	print_tree(getDevice());
	paffs_permission p = 0;
	printf("Creating directory /a... ");
	fflush(stdout);
//	while(getchar() == EOF);
	printf("%s\n", paffs_err_msg(paffs_mkdir("/a", p)));
	print_tree(getDevice());
//	while(getchar() == EOF);
	printf("Creating directory /b... ");
	fflush(stdout);
//	while(getchar() == EOF);
	printf("%s\n", paffs_err_msg(paffs_mkdir("/b", p)));
	print_tree(getDevice());
//	while(getchar() == EOF);
	printf("Creating directory /b/foo... ");
	fflush(stdout);
//	while(getchar() == EOF);
	printf("%s\n", paffs_err_msg(paffs_mkdir("/b/foo", p)));
	print_tree(getDevice());
	printf("Touching file /b/file ... ");
	fflush(stdout);
//	while(getchar() == EOF);
	printf("%s\n", paffs_err_msg(paffs_touch ("/b/file")));
	print_tree(getDevice());

	listDir("/");

	listDir("/b/");

	listDir("/a");
	
	printf("opening file /b/file ...");
	fflush(stdout);

	paffs_obj *fil = paffs_open("/b/file", PAFFS_FW);

	if(fil == NULL){
		printf("Open err: %s\n", paffs_err_msg(paffs_getLastErr()));
		return;
	}

	//	while(getchar() == EOF);


	//first write ----
	char t[] = ".                         Text";	//30 chars
	char* tl = (char*) malloc(36*strlen(t)+2);

	for(int i = 0; i < 36; i++){
		memcpy(&tl[i * strlen(t)], t, strlen(t));
	}
	tl[36*strlen(t)] = '+';
	tl[36*strlen(t) + 1] = 0;
	tl[511] = '!';
	tl[512] = '#';
	tl[1023] = '!';
	tl[1024] = '#';




	unsigned int bytes = 0;
	r = paffs_write(fil, tl, strlen(tl), &bytes);
	if(r != PAFFS_OK)
			printf("Write: %s\n", paffs_err_msg(r));
	printf("Wrote content  '%s' 25 times to file\n", t);
	// ----- first write


	printWholeFile("/b/file");
//	while(getchar() == EOF);

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

//	while(getchar() == EOF);

	//write misaligned - over Size----
	printf("write misaligned - over Size\n");
	r = paffs_seek(fil, -5, PAFFS_SEEK_END);
	if(r != PAFFS_OK)
			printf("%s\n", paffs_err_msg(r));

	char testlauf[] = "---Testlauf---";
	r = paffs_write(fil, testlauf, strlen(testlauf), &bytes);
	if(r != PAFFS_OK)
			printf("%s\n", paffs_err_msg(r));
	// ---- write misaligned 1

	printWholeFile("/b/file");
//	while(getchar() == EOF);

	//write misaligned - end misaligned ----
	printf("write misaligned - last page misaligned\n");
	r = paffs_seek(fil, -11, PAFFS_SEEK_END);
	if(r != PAFFS_OK)
			printf("%s\n", paffs_err_msg(r));

	char kurz[] = "kurz";
	r = paffs_write(fil, kurz, strlen(kurz), &bytes);
	if(r != PAFFS_OK)
			printf("%s\n", paffs_err_msg(r));
	// ---- write misaligned 2

	printWholeFile("/b/file");
//	while(getchar() == EOF);

	//write misaligned - write over page boundaries ----
	printf("write misaligned - over page boundaries\n");
	r = paffs_seek(fil, 508, PAFFS_SEEK_SET);
	if(r != PAFFS_OK)
			printf("%s\n", paffs_err_msg(r));

	r = paffs_write(fil, testlauf, strlen(testlauf), &bytes);
	if(r != PAFFS_OK)
			printf("%s\n", paffs_err_msg(r));
	// ---- write misaligned 2

	printWholeFile("/b/file");
//	while(getchar() == EOF);

	//write misaligned - write inside not start/end page ----
	printf("write misaligned - write inside non start/end page\n");
	r = paffs_seek(fil, 530, PAFFS_SEEK_SET);

	if(r != PAFFS_OK)
			printf("%s\n", paffs_err_msg(r));

	r = paffs_write(fil, testlauf, strlen(testlauf), &bytes);
	if(r != PAFFS_OK)
			printf("%s\n", paffs_err_msg(r));
	// ---- write misaligned 3


	printWholeFile("/b/file");
//	while(getchar() == EOF);

	r = paffs_getObjInfo("/b/file", &fileInfo);
	if(r != PAFFS_OK)
			printf("%s\n", paffs_err_msg(r));
	printInfo(&fileInfo);


//	while(getchar() == EOF);

	free (tl);
	paffs_close(fil);
}

int main( int argc, char ** argv ) {
	dirTest();
}
