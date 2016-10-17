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
#include "treeCache.h"

void listDir(const char* path){
	printf("Opening Dir '%s'.\n", path);
	paffs_dir* rewt = paffs_opendir(path);
	if(rewt == NULL){
		printf("Opendir: Result %s\n", paffs_err_msg(paffs_getLastErr()));
		return;
	}
	paffs_dirent* dir;
	while((dir = paffs_readdir(rewt)) != NULL){
		printf("\tFound ");
		switch(dir->node->type){
		case PINODE_FILE:
			printf("file: ");
			break;
		case PINODE_DIR:
			printf("dir : ");
			break;
		case PINODE_LNK:
			printf("link: ");
			break;
		default:
			printf("unknown: ");
		}
		printf("\"%s\"\n", dir->name);
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

int main( int argc, char ** argv ) {
	printf("Cache usage: %d/%d\n", getCacheUsage(), getCacheSize());
	printf("Cache size: %lu Bytes\n", getCacheSize() * sizeof(treeCacheNode));
	paffs_start_up();
	PAFFS_RESULT r = paffs_mnt("1");
	print_tree(getDevice());
	paffs_permission p = PAFFS_R | PAFFS_W;
	printf("Creating directory /a... ");
	fflush(stdout);
//	while(getchar() == EOF);
	printf("%s\n", paffs_err_msg(paffs_mkdir("/a", p)));
	printf("Cache usage: %d/%d\n", getCacheUsage(), getCacheSize());
	print_tree(getDevice());
//	while(getchar() == EOF);
	printf("Creating directory /b... ");
	fflush(stdout);
//	while(getchar() == EOF);
	printf("%s\n", paffs_err_msg(paffs_mkdir("/b", p)));
	printf("Cache usage: %d/%d\n", getCacheUsage(), getCacheSize());
	print_tree(getDevice());
//	while(getchar() == EOF);
	printf("Creating directory /b/foo... ");
	fflush(stdout);
//	while(getchar() == EOF);
	printf("%s\n", paffs_err_msg(paffs_mkdir("/b/foo", p)));
	printf("Cache usage: %d/%d\n", getCacheUsage(), getCacheSize());
	print_tree(getDevice());
	printf("Touching file /b/file ... ");
	fflush(stdout);
//	while(getchar() == EOF);
	printf("%s\n", paffs_err_msg(paffs_touch ("/b/file")));
	printf("Cache usage: %d/%d\n", getCacheUsage(), getCacheSize());
	print_tree(getDevice());

	listDir("/");

	listDir("/b/");

	listDir("/a");
	
	printf("opening file /b/file ...");
	fflush(stdout);

	paffs_obj *fil = paffs_open("/b/file", PAFFS_FW);

	printf("%s\n", paffs_err_msg(paffs_getLastErr()));
	if(fil == NULL)
		return -1;

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
	printf("%s\n", paffs_err_msg(r));
	if(r != PAFFS_OK)
		return -1;
	printf("Wrote content  '%s' 25 times to file\n", t);
	// ----- first write


	printWholeFile("/b/file");
//	while(getchar() == EOF);

	//read misaligned ---
	paffs_objInfo fileInfo = {0};
	r = paffs_seek(fil, 9, PAFFS_SEEK_SET);
	r = paffs_getObjInfo("/b/file", &fileInfo);
	printf("%s\n", paffs_err_msg(r));
	if(r != PAFFS_OK)
		return -1;

	char* out = malloc(fileInfo.size - 8);
	r = paffs_read(fil, out, fileInfo.size - 9, &bytes);
	printf("%s\n", paffs_err_msg(r));
	if(r != PAFFS_OK)
		return -1;

	out[fileInfo.size - 9] = 0;
	printf("Read Contents misaligned (+9)... ");
	fflush(stdout);
	if(memcmp(&tl[9], out, fileInfo.size - 9) == 0){
		printf("OK\n");
	}else{
		printf("Different!\n%s", out);
		return -1;
	}
	free(out);
	//--- read misaligned

//	while(getchar() == EOF);

	//write misaligned - over Size----
	printf("write misaligned - over Size... ");
	fflush(stdout);
	r = paffs_seek(fil, -5, PAFFS_SEEK_END);
	printf("%s\n", paffs_err_msg(r));
	if(r != PAFFS_OK)
		return -1;

	char testlauf[] = "---Testlauf---";


	r = paffs_write(fil, testlauf, strlen(testlauf), &bytes);
	if(r != PAFFS_OK){
		printf("%s\n", paffs_err_msg(r));
		return -1;
	}
	// ---- write misaligned 1


//	while(getchar() == EOF);

	//TODO: test with memcmp
	//write misaligned - end misaligned ----
	printf("write misaligned - last page misaligned\n");
	r = paffs_seek(fil, -11, PAFFS_SEEK_END);
	printf("%s\n", paffs_err_msg(r));
	if(r != PAFFS_OK)
		return -1;

	char kurz[] = "kurz";
	r = paffs_write(fil, kurz, strlen(kurz), &bytes);
	printf("%s\n", paffs_err_msg(r));
	if(r != PAFFS_OK)
		return -1;
	// ---- write misaligned 2

	printWholeFile("/b/file");
//	while(getchar() == EOF);

	//write misaligned - write over page boundaries ----
	printf("write misaligned - over page boundaries\n");
	r = paffs_seek(fil, 508, PAFFS_SEEK_SET);
	printf("%s\n", paffs_err_msg(r));
	if(r != PAFFS_OK)
		return -1;

	r = paffs_write(fil, testlauf, strlen(testlauf), &bytes);
	printf("%s\n", paffs_err_msg(r));
	if(r != PAFFS_OK)
		return -1;
	// ---- write misaligned 2

	printWholeFile("/b/file");
//	while(getchar() == EOF);

	//write misaligned - write inside not start/end page ----
	printf("write misaligned - write inside non start/end page\n");
	r = paffs_seek(fil, 530, PAFFS_SEEK_SET);

	printf("%s\n", paffs_err_msg(r));
	if(r != PAFFS_OK)
		return -1;

	r = paffs_write(fil, testlauf, strlen(testlauf), &bytes);
	printf("%s\n", paffs_err_msg(r));
	if(r != PAFFS_OK)
		return -1;
	// ---- write misaligned 3


	printWholeFile("/b/file");
//	while(getchar() == EOF);

	r = paffs_getObjInfo("/b/file", &fileInfo);
	printf("%s\n", paffs_err_msg(r));
	if(r != PAFFS_OK)
		return -1;

	printInfo(&fileInfo);

	print_tree(getDevice());
	printf("Flushing Cache ... ");
	fflush(stdout);
	r = flushTreeCache(getDevice());
	printf("%s\n", paffs_err_msg(r));
	if(r != PAFFS_OK)
		return -1;

	listDir("/");


//	while(getchar() == EOF);
	printf("Changing permissions of /b/file to 0... ");
	fflush(stdout);
	r = paffs_chmod("/b/file", 0);
	printf("%s\n", paffs_err_msg(r));
	if(r != PAFFS_OK)
		return -1;

	r = paffs_getObjInfo("/b/file", &fileInfo);
	if(r != PAFFS_OK){
		printf("%s\n", paffs_err_msg(r));
		return -1;
	}
	printInfo(&fileInfo);

//	while(getchar() == EOF);
	printf("Changing permissions of /b/file to rwx... ");
	fflush(stdout);
	r = paffs_chmod("/b/file", PAFFS_R | PAFFS_W | PAFFS_X);
	printf("%s\n", paffs_err_msg(r));
	if(r != PAFFS_OK){
		return -1;
	}

	printf("Removing /b/file... ");
	fflush(stdout);
	r = paffs_remove("/b/file");
	printf("%s\n", paffs_err_msg(r));
	if(r != PAFFS_OK){
		return -1;
	}

	listDir("/b");

//	while(getchar() == EOF);

	printf("Trying to remove /b/... ");
	fflush(stdout);
	r = paffs_remove("/b/");
	printf("%s\n", paffs_err_msg(r));
	if(r != PAFFS_DIRNOTEMPTY){
		return -1;
	}

//	while(getchar() == EOF);

	printf("Removing /b/foo... ");
	fflush(stdout);
	r = paffs_remove("/b/foo");
	printf("%s\n", paffs_err_msg(r));
	if(r != PAFFS_OK){
		return -1;
	}

	listDir("/b");


//	while(getchar() == EOF);

	printf("Removing /b/... ");
	fflush(stdout);
	r = paffs_remove("/b/");
	printf("%s\n", paffs_err_msg(r));
	if(r != PAFFS_OK){
		return -1;
	}

	listDir("/");

	free (tl);
	paffs_close(fil);

	printf("Success.\n");

	printf("\nCache-Hits: %d, Cache-Misses: %d\n\tHit ratio: %.5f%%\n",
			getCacheHits(), getCacheMisses(),
			100*((float)getCacheHits())/(getCacheHits()+getCacheMisses()));
//	while(getchar() == EOF);

	return 0;
}
