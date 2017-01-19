/**
 * File created on 19.05.2016
 * Author: Pascal Pieper
 */

#pragma once


#include <stdint.h>


#include "paffs_trace.hpp"
#include "driver/driverconf.hpp"

#define BYTES_PER_PAGE 512

namespace paffs{

extern char* areaNames[];		//Initialized in paffs_flash.c
extern const char* resultMsg[];		//Initialized in paffs.cpp

enum class Result{
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

const char* err_msg(Result pr);

typedef char Permission;

static const Permission PAFFS_R = 0x1;
static const Permission PAFFS_W = 0x2;
static const Permission PAFFS_X = 0x4;
static const Permission PAFFS_PERM_MASK = 0b111;

typedef char Fileopenmask;
static const Fileopenmask PAFFS_FR = 0x01;	//file read
static const Fileopenmask PAFFS_FW = 0x02;	//file write
static const Fileopenmask PAFFS_FEX= 0x04;	//file execute
static const Fileopenmask PAFFS_FA = 0x08;	//file append
static const Fileopenmask PAFFS_FE = 0x10;	//file open only existing
static const Fileopenmask PAFFS_FC = 0x20;	//file create

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

enum class InodeType{
    file,
    dir,
    lnk
};

struct Inode{
	InodeNo no;
	InodeType type:3;
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


struct Dentry{
	char* name;		//is actually full path
	Inode* iNode;
	struct Dentry* parent;
};

struct Obj{
	bool rdnly;
	Dentry* dentry;
	unsigned int fp;	//Current filepointer
};

struct Dirent{
	InodeNo node_no;
	Inode* node;
	char* name;
};

struct Dir{
	Dentry* dentry;
	Dirent** dirents;
	DirEntryCount no_entrys;
	unsigned int pos;
};

struct ObjInfo{
	unsigned int size;
	Date created;
	Date modified;
	bool isDir;
	Permission perm;
};

enum AreaType : uint8_t{
	unset = 0,
	superblockarea,
	indexarea,
	journalarea,
	dataarea,
	garbage_buffer,
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
	dirty
};

struct Area{
	AreaType type:4;
	AreaStatus status:2;
	uint32_t erasecount:17;	//Overflow at 132.000 is acceptable (assuming less than 100k erase cycles)
	AreaPos position;	//physical position, not logical
	SummaryEntry* areaSummary; //May be invalid if status == closed; Optimizable bitusage
	bool has_areaSummary:1;		//TODO: Check if really necessary for superindex
	bool isAreaSummaryDirty:1;
};	//3 + 2 + 17 + 1 + 32 (+64/32) = 55 (119/87) Bit = 6,875 (14,875/10,875) Byte

struct Dev{
	Param param;
	AreaPos activeArea[AreaType::no];
	//Automatically filled
	Obj root_dir;
	Area* areaMap;
};

class Paffs{
	Driver *driver;
	Dev* device; 	//TODO: is actually in driver decide where to go with it
	SummaryEntry* summaryEntry_containers[2];
	Result lasterr = Result::ok;

public:
	Paffs();
	Paffs(void* fc),
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
	Result truncate(Obj* obj);

	Result chmod(const char* path, Permission perm);
	Result remove(const char* path);

	//ONLY FOR DEBUG
	Dev* getDevice();

private:
	Result initialize();

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

