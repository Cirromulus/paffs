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

#include "../paffs.hpp"
#include "../btree.hpp"
#include "../treeCache.hpp"

using namespace paffs;
Paffs* fs;

void listDir(const char* path){
	printf("opening Dir '%s'.\n", path);
	Dir* rewt = fs->openDir(path);
	if(rewt == NULL){
		printf("Opendir: Result %s\n", err_msg(fs->getLastErr()));
		return;
	}
	Dirent* dir;
	while((dir = fs->readDir(rewt)) != NULL){
		printf("\tFound ");
		switch(dir->node->type){
		case InodeType::file:
			printf("file: ");
			break;
		case InodeType::dir:
			printf("dir : ");
			break;
		case InodeType::lnk:	//FIXME: Warning, -2 ??
			printf("link: ");
			break;
		default:
			printf("unknown: ");
		}
		printf("\"%s\"\n", dir->name);
	}
	if(fs->getLastErr() != Result::ok)
		printf("Error reading Dir: %s\n", err_msg(fs->getLastErr()));
	fs->closeDir(rewt);
}

void printInfo(ObjInfo* obj){
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
	printf("\t Permission rwx: %d%d%d\n", (obj->perm & R) != 0, (obj->perm & W) != 0, (obj->perm & X) != 0);
}

void printFile(unsigned int offs, unsigned int bytes, const char* path){
	Obj *fil = fs->open(path, FR);

	unsigned int bytesread = 0;
	char* out = (char*) malloc (bytes + 1);
	memset(out, 0, bytes + 1);

	Result r = fs->seek(fil, offs, Seekmode::set);
	if(r != Result::ok){
		free(out);
		printf("%s\n", err_msg(r));
		return;
	}
	r  = fs->read(fil, out, bytes, &bytesread);
	if(r != Result::ok){
		free(out);
		printf("%s\n", err_msg(r));
		return;
	}
	fs->close(fil);

	out[bytesread] = 0;
	printf("Read '%s': %s\n", path, out);
	free (out);
}

void printWholeFile(const char* path){
	ObjInfo fileInfo = {0};
	Result r = fs->getObjInfo(path, &fileInfo);
	if(r != Result::ok){
		printf("%s\n", err_msg(r));
		return;
	}
	printFile(0, fileInfo.size, path);

}

int main(int argc, char** argv){
	fs = new Paffs();
	printf("Cache usage: %d/%d\n", getCacheUsage(), getCacheSize());
	printf("Cache size: %u Bytes\n", getCacheSize() * sizeof(TreeCacheNode));
	Result r = fs->format("1");
	if(r != Result::ok)
		return -1;
	r = fs->mnt("1");
	if(r != Result::ok)
		return -1;
	print_tree(fs->getDevice());
	Permission p = R | W;
	printf("Creating directory /a... ");
	fflush(stdout);
//	while(getchar() == EOF);
	printf("%s\n", err_msg(fs->mkDir("/a", p)));
	printf("Cache usage: %d/%d\n", getCacheUsage(), getCacheSize());
	print_tree(fs->getDevice());
//	while(getchar() == EOF);
	printf("Creating directory /b... ");
	fflush(stdout);
//	while(getchar() == EOF);
	printf("%s\n", err_msg(fs->mkDir("/b", p)));
	printf("Cache usage: %d/%d\n", getCacheUsage(), getCacheSize());
	print_tree(fs->getDevice());
//	while(getchar() == EOF);
	printf("Creating directory /b/foo... ");
	fflush(stdout);
//	while(getchar() == EOF);
	printf("%s\n", err_msg(fs->mkDir("/b/foo", p)));
	printf("Cache usage: %d/%d\n", getCacheUsage(), getCacheSize());
	print_tree(fs->getDevice());
	printf("Touching file /b/file ... ");
	fflush(stdout);
//	while(getchar() == EOF);
	printf("%s\n", err_msg(fs->touch ("/b/file")));
	printf("Cache usage: %d/%d\n", getCacheUsage(), getCacheSize());
	print_tree(fs->getDevice());

	listDir("/");

	listDir("/b/");

	listDir("/a");
	
	printf("opening file /b/file ...");
	fflush(stdout);

	Obj *fil = fs->open("/b/file", FW);

	printf("%s\n", err_msg(fs->getLastErr()));
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
	r = fs->write(fil, tl, strlen(tl), &bytes);
	printf("%s\n", err_msg(r));
	if(r != Result::ok)
		return -1;
	printf("Wrote content  '%s' 25 times to file\n", t);
	// ----- first write


	printWholeFile("/b/file");
//	while(getchar() == EOF);

	//read misaligned ---
	ObjInfo fileInfo = {0};
	r = fs->seek(fil, 9, Seekmode::set);
	r = fs->getObjInfo("/b/file", &fileInfo);
	printf("%s\n", err_msg(r));
	if(r != Result::ok)
		return -1;

	char* out = (char*) malloc(fileInfo.size - 8);
	r = fs->read(fil, out, fileInfo.size - 9, &bytes);
	printf("%s\n", err_msg(r));
	if(r != Result::ok)
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
	r = fs->seek(fil, -5, Seekmode::end);
	printf("%s\n", err_msg(r));
	if(r != Result::ok)
		return -1;

	char testlauf[] = "---Testlauf---";


	r = fs->write(fil, testlauf, strlen(testlauf), &bytes);
	if(r != Result::ok){
		printf("%s\n", err_msg(r));
		return -1;
	}
	// ---- write misaligned 1


//	while(getchar() == EOF);

	//TODO: test with memcmp
	//write misaligned - end misaligned ----
	printf("write misaligned - last page misaligned\n");
	r = fs->seek(fil, -11, Seekmode::end);
	printf("%s\n", err_msg(r));
	if(r != Result::ok)
		return -1;

	char kurz[] = "kurz";
	r = fs->write(fil, kurz, strlen(kurz), &bytes);
	printf("%s\n", err_msg(r));
	if(r != Result::ok)
		return -1;
	// ---- write misaligned 2

	printWholeFile("/b/file");
//	while(getchar() == EOF);

	//write misaligned - write over page boundaries ----
	printf("write misaligned - over page boundaries\n");
	r = fs->seek(fil, 508, Seekmode::set);
	printf("%s\n", err_msg(r));
	if(r != Result::ok)
		return -1;

	r = fs->write(fil, testlauf, strlen(testlauf), &bytes);
	printf("%s\n", err_msg(r));
	if(r != Result::ok)
		return -1;
	// ---- write misaligned 2

	printWholeFile("/b/file");
//	while(getchar() == EOF);

	//write misaligned - write inside not start/end page ----
	printf("write misaligned - write inside non start/end page\n");
	r = fs->seek(fil, 530, Seekmode::set);

	printf("%s\n", err_msg(r));
	if(r != Result::ok)
		return -1;

	r = fs->write(fil, testlauf, strlen(testlauf), &bytes);
	printf("%s\n", err_msg(r));
	if(r != Result::ok)
		return -1;
	// ---- write misaligned 3


	printWholeFile("/b/file");
//	while(getchar() == EOF);

	r = fs->getObjInfo("/b/file", &fileInfo);
	printf("%s\n", err_msg(r));
	if(r != Result::ok)
		return -1;

	printInfo(&fileInfo);

	print_tree(fs->getDevice());
	printf("Flushing Cache ... ");
	fflush(stdout);
	r = commitTreeCache(fs->getDevice());
	printf("%s\n", err_msg(r));
	if(r != Result::ok)
		return -1;

	listDir("/");


//	while(getchar() == EOF);
	printf("Changing permissions of /b/file to 0... ");
	fflush(stdout);
	r = fs->chmod("/b/file", 0);
	printf("%s\n", err_msg(r));
	if(r != Result::ok)
		return -1;

	r = fs->getObjInfo("/b/file", &fileInfo);
	if(r != Result::ok){
		printf("%s\n", err_msg(r));
		return -1;
	}
	printInfo(&fileInfo);

//	while(getchar() == EOF);
	printf("Changing permissions of /b/file to rwx... ");
	fflush(stdout);
	r = fs->chmod("/b/file", R | W | X);
	printf("%s\n", err_msg(r));
	if(r != Result::ok){
		return -1;
	}

	printf("Removing /b/file... ");
	fflush(stdout);
	r = fs->remove("/b/file");
	printf("%s\n", err_msg(r));
	if(r != Result::ok){
		return -1;
	}

	listDir("/b");

//	while(getchar() == EOF);

	printf("Trying to remove /b/... ");
	fflush(stdout);
	r = fs->remove("/b/");
	printf("%s\n", err_msg(r));
	if(r != Result::dirnotempty){
		return -1;
	}

//	while(getchar() == EOF);

	printf("Removing /b/foo... ");
	fflush(stdout);
	r = fs->remove("/b/foo");
	printf("%s\n", err_msg(r));
	if(r != Result::ok){
		return -1;
	}

	listDir("/b");


//	while(getchar() == EOF);

	printf("Removing /b/... ");
	fflush(stdout);
	r = fs->remove("/b/");
	printf("%s\n", err_msg(r));
	if(r != Result::ok){
		return -1;
	}

	listDir("/");

	free (tl);
	fs->close(fil);

	printf("Success.\n");

	printf("\nCache-Hits: %d, Cache-Misses: %d\n\tHit ratio: %.5f%%\n",
			getCacheHits(), getCacheMisses(),
			100*((float)getCacheHits())/(getCacheHits()+getCacheMisses()));
//	while(getchar() == EOF);
	r = fs->unmnt("1");
	if(r != Result::ok)
		return -1;
	delete fs;
	return 0;
}
