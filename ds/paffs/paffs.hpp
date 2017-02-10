/**
 * File created on 19.05.2016
 * Author: Pascal Pieper
 */

#pragma once

#include <stdint.h>
#include <functional>
#include "paffs_trace.hpp"


#define BYTES_PER_PAGE 512

namespace paffs{

class Driver;

enum class Result : uint8_t{
	ok = 0,
	fail,
	nf,
	exists,
	einval,
	nimpl,
	bug,
	noparent,
	nosp,
	lowmem,
	noperm,
	dirnotempty,
	flushedcache,
	badflash,
	num_result
};

extern char* areaNames[];		//Initialized in paffs_flash.c
extern const char* resultMsg[];		//Initialized in paffs.cpp
extern Result lasterr;
const char* err_msg(Result pr);

typedef uint8_t Permission;

static const Permission R = 0x1;
static const Permission W = 0x2;
static const Permission X = 0x4;
static const Permission PERM_MASK = 0b111;

typedef char Fileopenmask;
static const Fileopenmask FR = 0x01;	//file read
static const Fileopenmask FW = 0x02;	//file write
static const Fileopenmask FEX= 0x04;	//file execute
static const Fileopenmask FA = 0x08;	//file append
static const Fileopenmask FE = 0x10;	//file open only existing
static const Fileopenmask FC = 0x20;	//file create

enum class Seekmode{
	set = 0,
	cur,
	end
};

struct Param{
	const char *name;
	unsigned int total_bytes_per_page;
	unsigned int oob_bytes_per_page;
	unsigned int pages_per_block;
	unsigned int blocks;
	/*Automatically filled*/
	unsigned int data_bytes_per_page;
	unsigned long areas_no;
	unsigned int blocks_per_area;
	unsigned int total_pages_per_area;
	unsigned int data_pages_per_area;
};


typedef unsigned long Date;

typedef uint64_t Addr;
typedef uint32_t AreaPos;
typedef uint32_t FileSize; 	  //~ 4 GB per file
typedef uint32_t InodeNo;		  //~ 4 Million files
typedef uint16_t DirEntryCount; //65,535 Entries per Directory
typedef uint8_t  DirEntryLength; //255 characters per Directory entry

enum class InodeType : uint8_t{
	file,
	dir,
	lnk
};

struct Inode{
	InodeNo no;
	InodeType type:2;
	Permission perm:3;
	Date crea;
	Date mod;
	FileSize reservedSize;	//Space on filesystem used in bytes
	FileSize size;			//Space on filesystem needed in bytes
	Addr direct[11];
	Addr indir;
	Addr d_indir;
	Addr t_indir;
};

//could be later used for caching file paths
struct Dirent{
	InodeNo no; //This is used for lazy loading of Inode
	Inode* node; //can be NULL if not loadet yet
	Dirent* parent;
	char* name;
};

//An object can be a file, directory or link
struct Obj{
	bool rdnly;
	Dirent* dirent;
	unsigned int fp;	//Current filepointer
};

struct Dir{
	Dirent* self;
	Dirent** childs;
	DirEntryCount no_entrys;
	DirEntryCount pos;
};

struct ObjInfo{
	FileSize size;
	Date created;
	Date modified;
	bool isDir;
	Permission perm;
};

enum AreaType : uint8_t{
	unset = 0,
	superblock,
	index,
	journal,
	data,
	garbageBuffer,
	retired,
	no
};

enum AreaStatus : uint8_t{
	closed = 0,
	active,
	empty
};

enum class SummaryEntry : uint8_t{
	free = 0,
	used,		//if read from super index, used can mean both free and used to save a bit per entry.
	dirty,
	error
};

struct Area{
	AreaType type:4;
	AreaStatus status:2;
	uint32_t erasecount:17;	//Overflow at 132.000 is acceptable (assuming less than 100k erase cycles)
	AreaPos position;	//physical position, not logical
	std::function<SummaryEntry(uint8_t,Result*)> getPageStatus;
	std::function<Result(uint8_t,SummaryEntry)> setPageStatus;
};	//4 + 2 + 17 + 32 (+2*64) = 55 (183) Bit = 7 (23) Byte

struct Dev{
	Param* param;
	AreaPos activeArea[AreaType::no];
	Area* areaMap;
	Driver *driver;
};

Addr combineAddress(uint32_t logical_area, uint32_t page);
unsigned int extractLogicalArea(Addr addr);
unsigned int extractPage(Addr addr);

class Paffs{
	Dev device = {0};

public:
	Paffs();
	Paffs(void* fc);
	~Paffs();



	Result format(const char* devicename);
	Result mnt(const char* devicename);
	Result unmnt(const char* devicename);
	Result getLastErr();
	void resetLastErr();

	//Directory
	Result mkDir(const char* fullPath, Permission mask);
	Dir* openDir(const char* path);
	Dirent* readDir(Dir* dir);
	Result closeDir(Dir* dir);
	void rewindDir(Dir* dir);

	//File
	Obj* open(const char* path, Fileopenmask mask);
	Result close(Obj* obj);
	Result touch(const char* path);
	Result getObjInfo(const char *fullPath, ObjInfo* nfo);
	Result read(Obj* obj, char* buf, unsigned int bytes_to_read, unsigned int *bytes_read);
	Result write(Obj* obj, const char* buf, unsigned int bytes_to_write, unsigned int *bytes_written);
	Result seek(Obj* obj, int m, Seekmode mode);
	Result flush(Obj* obj);
	Result truncate(Dirent* obj);

	Result chmod(const char* path, Permission perm);
	Result remove(const char* path);

	//ONLY FOR DEBUG
	Dev* getDevice();

private:
	Result initializeDevice(const char* devicename);
	Result destroyDevice(const char* devicename);

	Result createInode(Inode* outInode, Permission mask);
	Result createDirInode(Inode* outInode, Permission mask);
	Result createFilInode(Inode* outInode, Permission mask);
	void destroyInode(Inode* node);
	Result getParentDir(const char* fullPath, Inode* parDir, unsigned int *lastSlash);
	Result getInodeInDir( Inode* outInode, Inode* folder, const char* name);
	Result getInodeOfElem( Inode* outInode, const char* fullPath);
	//newElem should be already inserted in Tree
	Result insertInodeInDir(const char* name, Inode* contDir, Inode* newElem);
	Result removeInodeFromDir(Inode* contDir, Inode* elem);
	Result createFile(Inode* outFile, const char* fullPath, Permission mask);

};
}

