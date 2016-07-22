/**
 * File created on 19.05.2016
 * Author: Pascal Pieper
 */

//todo: Types definiert definieren (unsigned int -> uint32_t)

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


typedef enum PAFFS_RESULT{
	PAFFS_OK = 0,
	PAFFS_FAIL,
	PAFFS_NF,
	PAFFS_EINVAL,
	PAFFS_NIMPL,
	PAFFS_BUG,
	PAFFS_NOSP
} PAFFS_RESULT;

static const char* PAFFS_RESULT_MSG[] = {
		"OK",
		"Unknown error",
		"Object not found",
		"Input values malformed",
		"Operation not yet supported",
		"Gratulations, you found a Bug",
		"No (usable) space left on device"
};

typedef enum pinode_type{
    PINODE_FILE,
    PINODE_DIR,
    PINODE_LNK
}pinode_type;

typedef char paffs_permission;

static const paffs_permission PAFFS_R = 0x1;
static const paffs_permission PAFFS_W = 0x2;
static const paffs_permission PAFFS_X = 0x4;

typedef char fileopenmask;
static const fileopenmask PAFFS_FC = 0x01;	//file create
static const fileopenmask PAFFS_FR = 0x02;	//file read
static const fileopenmask PAFFS_FW = 0x04;	//file write
static const fileopenmask PAFFS_FA = 0x08;	//file append
static const fileopenmask PAFFS_FE = 0x10;	//file open only existing

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
} p_param;


typedef unsigned long p_date;

typedef uint64_t p_addr;

typedef unsigned int pInode_no;

typedef struct pInode{
	pInode_no no;
	unsigned int seq_no;
	pinode_type type;
	paffs_permission perm;
	p_date crea;
	p_date mod;
	unsigned long long reservedSize;
	unsigned long long size;    //~1,8 * 10^19 Byte
	p_addr direct[11];
	p_addr indir;
	p_addr d_indir;
	p_addr t_indir;
} pInode;


typedef struct pDentry{
	char* name;
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
	unsigned int no_entrys;
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
	PAFFS_RESULT (*drv_write_page_fn) (struct p_dev *dev, unsigned long long page_no,
			void* data, unsigned int data_len);
	PAFFS_RESULT (*drv_read_page_fn) (struct p_dev *dev, unsigned long long page_no,
			void* data, unsigned int data_len);
	PAFFS_RESULT (*drv_erase_fn) (struct p_dev *dev, unsigned long block_no);
	PAFFS_RESULT (*drv_mark_bad_fn) (struct p_dev *dev, unsigned long block_no);
	PAFFS_RESULT (*drv_check_bad_fn) (struct p_dev *dev, unsigned long block_no);
	PAFFS_RESULT (*drv_initialise_fn) (struct p_dev *dev);
	PAFFS_RESULT (*drv_deinitialise_fn) (struct p_dev *dev);
} p_driver;

typedef enum p_areaType{
	SUPERBLOCKAREA,
	INDEXAREA,
	JOURNALAREA,
	DATAAREA,
	area_types_no
} p_areaType;

typedef enum p_areaStatus{
	CLOSED,
	UNCLOSED,
	EMPTY
} p_areaStatus;

typedef enum p_summaryEntry{
	FREE = 0,
	USED,
	DIRTY
}p_summaryEntry;

static unsigned int activeArea[area_types_no];

typedef struct p_area{
	p_areaType type;
	p_areaStatus status;
	unsigned int erasecount;
	unsigned int position;	//physical position, not logical
	p_summaryEntry* areaSummary; //May be invalid if status == closed;
	unsigned int dirtyPages; 	//redundant to contents of areaSummary
} p_area;


typedef struct p_dev{
	p_param param;
	p_driver drv;
	void* driver_context;

	//Automatically filled
	paffs_obj root_dir;
	p_area* areaMap;
} p_dev;

static PAFFS_RESULT paffs_lasterr = PAFFS_OK;

PAFFS_RESULT paffs_initialize(p_dev* dev);

PAFFS_RESULT paffs_mnt(const char* devicename);
const char* paffs_err_msg(PAFFS_RESULT pr);
PAFFS_RESULT paffs_getLastErr();

//Private(ish)
pInode* paffs_createInode(paffs_permission mask);
pInode* paffs_createDirInode(paffs_permission mask);
pInode* paffs_createFilInode(paffs_permission mask);
void paffs_destroyInode(pInode* node);
PAFFS_RESULT paffs_getParentDir(const char* fullPath, pInode* *parDir, unsigned int *lastSlash);
pInode* paffs_getInodeInDir(pInode* folder, const char* name);
pInode* paffs_getInodeOfElem(const char* fullPath);
PAFFS_RESULT paffs_insertInodeInDir(const char* name, pInode* contDir, pInode* newElem);
pInode* paffs_createFile(const char* fullPath, paffs_permission mask);
p_addr combineAddress(uint32_t logical_area, uint32_t page);
unsigned int extractLogicalArea(p_addr addr);			//Address-wrapper für einfacheren Garbagecollector
unsigned int extractPage(p_addr addr);
unsigned long long getPageNumber(p_addr addr, p_dev *dev);	//Translates p_addr to physical page number in respect to the Area mapping


//Directory
PAFFS_RESULT paffs_mkdir(const char* fullPath, paffs_permission mask);
paffs_dir* paffs_opendir(const char* path);
paffs_dirent* paffs_readdir(paffs_dir* dir);
PAFFS_RESULT paffs_closedir(paffs_dir* dir);
void paffs_rewinddir(paffs_dir* dir);

//File
paffs_obj* paffs_open(const char* path, fileopenmask mask);
PAFFS_RESULT paffs_touch(const char* path);
PAFFS_RESULT paffs_getObjInfo(const char *fullPath, paffs_objInfo* nfo);
PAFFS_RESULT paffs_read(paffs_obj* obj, char* buf, unsigned int bytes_to_read, unsigned int *bytes_read);
PAFFS_RESULT paffs_write(paffs_obj* obj, const char* buf, unsigned int bytes_to_write, unsigned int *bytes_written);
PAFFS_RESULT paffs_seek(paffs_obj* obj, int m, paffs_seekmode mode);
PAFFS_RESULT paffs_flush(paffs_obj* obj);
PAFFS_RESULT paffs_close(paffs_obj* obj);

#if defined (__cplusplus) && !defined (__CDT_PARSER__)
}
#endif

#endif /*__PAFFS_H__*/

