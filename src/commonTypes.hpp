/*
 * commonTypes.hpp
 *
 *  Created on: Feb 17, 2017
 *      Author: Pascal Pieper
 */
#include <stdint.h>
#include <outpost/time/time_epoch.h>
#include "paffs_trace.hpp"
#include "smartInodePtr.hpp"

#pragma once

#ifdef __CDT_PARSER__
#	define PRIu32 "u"
#	define PRIu64 "lu"
#endif

namespace paffs {
static const uint8_t version = 1;

//TODO: Elaborate certain order of badness
enum class Result : uint8_t{
	ok = 0,
	fail,
	nf,
	exists,
	toobig,
	einval,
	nimpl,
	bug,
	noparent,
	nospace,
	lowmem,
	noperm,
	dirnotempty,
	badflash,
	notMounted,
	alrMounted,
	objNameTooLong,
	readonly,
	biterrorCorrected,
	biterrorNotCorrected,
	num_result
};

extern const char* areaNames[];		  //Initialized in area.cpp
extern const char* areaStatusNames[]; //Initialized in area.cpp
extern const char* resultMsg[];		  //Initialized in paffs.cpp

typedef uint8_t Permission;

const Permission R = 0x1;
const Permission W = 0x2;
const Permission X = 0x4;
const Permission permMask = R | W | X;

typedef uint8_t Fileopenmask;
const Fileopenmask FR = 0x01;	//file read
const Fileopenmask FW = 0x02;	//file write
const Fileopenmask FEX= 0x04;	//file execute
const Fileopenmask FA = 0x08;	//file append
const Fileopenmask FE = 0x10;	//file open only existing
const Fileopenmask FC = 0x20;	//file create

enum class Seekmode{
	set = 0,
	cur,
	end
};

struct Param{
	unsigned int totalBytesPerPage;
	unsigned int oobBytesPerPage;
	unsigned int pagesPerBlock;
	unsigned int blocksTotal;
	unsigned int jumpPadNo;
	//Automatically filled//
	unsigned int dataBytesPerPage;
	unsigned long areasNo;
	unsigned int blocksPerArea;
	unsigned int totalPagesPerArea;
	unsigned int dataPagesPerArea;
	unsigned int superChainElems;
};

extern const Param stdParam;

typedef uint64_t Addr;			//Contains logical area and relative page
typedef uint32_t AreaPos;		//Has to address total areas
typedef uint32_t PageOffs;		//Has to address pages per area
typedef uint64_t PageAbs;		//has to address all pages in a device
typedef uint32_t BlockAbs;		//has to address all blocks in a device
typedef uint32_t FileSize; 	  	//~ 4 GB per file
typedef uint32_t InodeNo;		//~ 4 Million files
typedef uint16_t DirEntryCount; //65,535 Entries per Directory
typedef uint8_t  DirEntryLength; //255 characters per Directory entry
static const DirEntryLength maxDirEntryLength = 255;

//Together with area = 0, it is used to mark an unused page in Inode
static const PageOffs unusedMarker = 0xFFFFFFFF;

struct BadBlockList{
	BlockAbs *mList;
	uint16_t mSize;
	BadBlockList(): mList(nullptr), mSize(0){};
	BadBlockList(BlockAbs *list, uint16_t size):mSize(size){
		mList = new BlockAbs[size];
		memcpy(mList, list, size*sizeof(BlockAbs));
	};
	BadBlockList& operator= (BadBlockList const &other){
		mList = new BlockAbs[other.mSize];
		memcpy(mList, other.mList, other.mSize*sizeof(BlockAbs));
		mSize = other.mSize;
		return *this;
	}
	BadBlockList(BadBlockList const &other){
		*this = other;
	}
	~BadBlockList(){
		if(mList != nullptr){
			delete[] mList;
		}
	}
	BlockAbs operator [](size_t pos){
		if(pos > mSize){
			return 0;
		}
		return mList[pos];
	}
};

enum class InodeType : uint8_t{
	file,
	dir,
	lnk
};

static constexpr uint16_t directAddrCount = 11;

struct Inode{
	InodeNo no;
	InodeType type;//:2;
	Permission perm:3;
	uint32_t reservedPages;	//Space on filesystem used in Pages
	FileSize size;			//Space on filesystem needed in bytes
	uint64_t crea;
	uint64_t mod;
	Addr direct[directAddrCount];
	Addr indir;
	Addr d_indir;
	Addr t_indir;
	//TODO: Add pointer to a PAC if cached
};

//could be later used for caching file paths, can be a file, directory or link
struct Dirent{
	InodeNo no; //This is used for lazy loading of Inode
	SmartInodePtr node; //can be NULL if not loaded yet
	Dirent* parent;
	char* name;
};

//An object is a file
struct Obj{
	bool rdnly;
	Dirent dirent;
	unsigned int fp;	//Current filepointer
	Fileopenmask fo;	//TODO actually use this for read/write
};

struct Dir{
	Dirent* self;
	Dirent* childs;
	DirEntryCount no_entries;
	DirEntryCount pos;
};

struct ObjInfo{
	FileSize size;
	outpost::time::GpsTime created;
	outpost::time::GpsTime modified;
	bool isDir;
	Permission perm;
};

enum AreaType : uint8_t{
	unset = 0,
	superblock,
	index,
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

struct Area{	//TODO: Maybe packed? Slow, but less RAM
	//AreaType type:4;
	//AreaStatus status:2;
	AreaType type;
	AreaStatus status;
	uint32_t erasecount:17;	//Overflow at 132.000 is acceptable (assuming less than 100k erase cycles)
	AreaPos position;	//physical position, not logical
};	//4 + 2 + 17 + 32 = 55 Bit = 7 Byte (8 on RAM)

const char* err_msg(Result pr);	//implemented in paffs.cpp

class Device;
class Driver;

}  // namespace paffs

#include <paffs/config.hpp>
#include "config/auto.hpp"
