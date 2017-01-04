/**
 * File created on 19.05.2016
 * Author: Pascal Pieper
 */

#ifndef __PAFFS_H__
#define __PAFFS_H__

#if defined (__cplusplus) && !defined (__CDT_PARSER__)	//BUG: CDT is unable to look into extern c.
extern "C" {
#endif

#include <stdbool.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdio.h>

#include "paffs_trace.h"

#define BYTES_PER_PAGE 512

typedef enum PAFFS_RESULT{	//See paffs.c for String versions
	PAFFS_OK = 0,
	PAFFS_FAIL,
	PAFFS_NF,
	PAFFS_EXISTS,
	PAFFS_EINVAL,
	PAFFS_NIMPL,
	PAFFS_BUG,
	PAFFS_NOPARENT,
	PAFFS_NOSP,
	PAFFS_LOWMEM,
	PAFFS_NOPERM,
	PAFFS_DIRNOTEMPTY,
	PAFFS_FLUSHEDCACHE,
	PAFFS_BADFLASH,
	num_PAFFS_RESULT
} PAFFS_RESULT;

typedef char paffs_permission;

static const paffs_permission PAFFS_R = 0x1;
static const paffs_permission PAFFS_W = 0x2;
static const paffs_permission PAFFS_X = 0x4;
static const paffs_permission PAFFS_PERM_MASK = 0b111;

typedef char fileopenmask;
static const fileopenmask PAFFS_FR = 0x01;	//file read
static const fileopenmask PAFFS_FW = 0x02;	//file write
static const fileopenmask PAFFS_FEX= 0x04;	//file execute
static const fileopenmask PAFFS_FA = 0x08;	//file append
static const fileopenmask PAFFS_FE = 0x10;	//file open only existing
static const fileopenmask PAFFS_FC = 0x20;	//file create

PAFFS_RESULT paffs_start_up();
PAFFS_RESULT paffs_custom_start_up(void* fc);

typedef enum paffs_seekmode{
	PAFFS_SEEK_SET = 0,
	PAFFS_SEEK_CUR,
	PAFFS_SEEK_END
}paffs_seekmode;

typedef struct p_param{
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
} p_param;


typedef unsigned long p_date;

typedef uint64_t p_addr;
typedef uint32_t area_pos_t;
typedef uint32_t fileSize_t; 	  //~ 4 GB per file
typedef uint32_t pInode_no;		  //~ 4 Million files
typedef uint16_t dirEntryCount_t; //65,535 Entries per Directory
typedef uint8_t dirEntryLength_t; //255 characters per Directory entry

typedef enum pInode_type{
    PINODE_FILE,
    PINODE_DIR,
    PINODE_LNK
} pInode_type;

typedef struct pInode{
	pInode_no no;
	pInode_type type:3;
	paffs_permission perm:3;
	p_date crea;
	p_date mod;
	fileSize_t reservedSize;	//Space on filesystem used in bytes
	fileSize_t size;			//Space on filesystem needed in bytes
	p_addr direct[11];
	p_addr indir;
	p_addr d_indir;
	p_addr t_indir;
} pInode;


typedef struct pDentry{
	char* name;		//is actually full path
	pInode* iNode;
	struct pDentry* parent;
} pDentry;

typedef struct paffs_obj{
	bool rdnly;
	pDentry* dentry;
	unsigned int fp;	//Current filepointer
} paffs_obj;

typedef struct paffs_dirent{
	pInode_no node_no;
	pInode* node;
	char* name;
}paffs_dirent;

typedef struct paffs_dir{
	pDentry* dentry;
	paffs_dirent** dirents;
	dirEntryCount_t no_entrys;
	unsigned int pos;
}paffs_dir;

typedef struct paffs_objInfo{
	unsigned int size;
	p_date created;
	p_date modified;
	bool isDir;
	paffs_permission perm;
} paffs_objInfo;

struct p_dev;

typedef struct p_driver {
	PAFFS_RESULT (*drv_write_page_fn) (struct p_dev *dev, uint64_t page_no,
			void* data, unsigned int data_len);
	PAFFS_RESULT (*drv_read_page_fn) (struct p_dev *dev, uint64_t page_no,
			void* data, unsigned int data_len);
	PAFFS_RESULT (*drv_erase_fn) (struct p_dev *dev, uint32_t block_no);
	PAFFS_RESULT (*drv_mark_bad_fn) (struct p_dev *dev, uint32_t block_no);
	PAFFS_RESULT (*drv_check_bad_fn) (struct p_dev *dev, uint32_t block_no);
	PAFFS_RESULT (*drv_initialise_fn) (struct p_dev *dev);
	PAFFS_RESULT (*drv_deinitialise_fn) (struct p_dev *dev);
} p_driver;

typedef enum p_areaType{
	UNSET,
	SUPERBLOCKAREA,
	INDEXAREA,
	JOURNALAREA,
	DATAAREA,
	GARBAGE_BUFFER,
	RETIRED,
	area_types_no
} p_areaType;

extern char* area_names[];		//Initialized in paffs_flash.c

typedef enum p_areaStatus{
	CLOSED,
	ACTIVE,
	EMPTY
} p_areaStatus;

typedef enum __attribute__ ((__packed__))p_summaryEntry{
	FREE = 0,
	USED,		//If read from super Index, USED can mean both FREE and USED to save a bit per entry.
	DIRTY
}p_summaryEntry;

typedef struct p_area{
	p_areaType type:3;
	p_areaStatus status:2;
	uint32_t erasecount:17;	//Overflow at 132.000 is acceptable (assuming less than 100k erase cycles)
	area_pos_t position;	//physical position, not logical
	p_summaryEntry* areaSummary; //May be invalid if status == closed; Optimizable bitusage
	bool has_areaSummary:1;		//TODO: Check if really necessary for superindex
	bool isAreaSummaryDirty:1;
} p_area;	//3 + 2 + 17 + 1 + 32 (+64/32) = 55 (119/87) Bit = 6,875 (14,875/10,875) Byte

typedef struct p_dev{
	p_param param;
	p_driver drv;
	void* driver_context;
	area_pos_t activeArea[area_types_no];
	//Automatically filled
	paffs_obj root_dir;
	p_area* areaMap;
} p_dev;

extern PAFFS_RESULT paffs_lasterr;				//defined in paffs.c
extern unsigned int activeArea[area_types_no]; 	//defined in paffs_flash.c

PAFFS_RESULT paffs_initialize(p_dev* dev);

PAFFS_RESULT paffs_format(const char* devicename);
PAFFS_RESULT paffs_mnt(const char* devicename);
PAFFS_RESULT paffs_unmnt(const char* devicename);
const char* paffs_err_msg(PAFFS_RESULT pr);
PAFFS_RESULT paffs_getLastErr();
void paffs_resetLastErr();

//Private(ish)
PAFFS_RESULT paffs_createInode(pInode* outInode, paffs_permission mask);
PAFFS_RESULT paffs_createDirInode(pInode* outInode, paffs_permission mask);
PAFFS_RESULT paffs_createFilInode(pInode* outInode, paffs_permission mask);
void paffs_destroyInode(pInode* node);
PAFFS_RESULT paffs_getParentDir(const char* fullPath, pInode* parDir, unsigned int *lastSlash);
PAFFS_RESULT paffs_getInodeInDir( pInode* outInode, pInode* folder, const char* name);
PAFFS_RESULT paffs_getInodeOfElem( pInode* outInode, const char* fullPath);
//newElem should be already inserted in Tree
PAFFS_RESULT paffs_insertInodeInDir(const char* name, pInode* contDir, pInode* newElem);
PAFFS_RESULT paffs_removeInodeFromDir(pInode* contDir, pInode* elem);
PAFFS_RESULT paffs_createFile(pInode* outFile, const char* fullPath, paffs_permission mask);


//Directory
PAFFS_RESULT paffs_mkdir(const char* fullPath, paffs_permission mask);
paffs_dir* paffs_opendir(const char* path);
paffs_dirent* paffs_readdir(paffs_dir* dir);
PAFFS_RESULT paffs_closedir(paffs_dir* dir);
void paffs_rewinddir(paffs_dir* dir);

//File
paffs_obj* paffs_open(const char* path, fileopenmask mask);
PAFFS_RESULT paffs_close(paffs_obj* obj);
PAFFS_RESULT paffs_touch(const char* path);
PAFFS_RESULT paffs_getObjInfo(const char *fullPath, paffs_objInfo* nfo);
PAFFS_RESULT paffs_read(paffs_obj* obj, char* buf, unsigned int bytes_to_read, unsigned int *bytes_read);
PAFFS_RESULT paffs_write(paffs_obj* obj, const char* buf, unsigned int bytes_to_write, unsigned int *bytes_written);
PAFFS_RESULT paffs_seek(paffs_obj* obj, int m, paffs_seekmode mode);
PAFFS_RESULT paffs_flush(paffs_obj* obj);
PAFFS_RESULT paffs_truncate(paffs_obj* obj);

PAFFS_RESULT paffs_chmod(const char* path, paffs_permission perm);
PAFFS_RESULT paffs_remove(const char* path);

//ONLY FOR DEBUG
p_dev* getDevice();

#if defined (__cplusplus) && !defined (__CDT_PARSER__)
}
#endif

#endif /*__PAFFS_H__*/

