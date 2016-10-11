/**
 * File created on 19.05.2016
 * Author: Pascal Pieper
 */

#include "paffs.h"
#include "paffs_flash.h"
#include "btree.h"
#include <linux/string.h>
#include <time.h>

static p_dev* device = NULL;

PAFFS_RESULT paffs_lasterr = PAFFS_OK;

//Dentrys zur schnelleren verfügung
/*#define DENTRY_BUFSIZE 10
static pDentry* dentry_buf[DENTRY_BUFSIZE];
static unsigned char dentrys_buf_used = 0;
*/

static const char* PAFFS_RESULT_MSG[] = {
		"OK",
		"Unknown error",
		"Object not found",
		"Input values malformed",
		"Operation not yet supported",
		"Gratulations, you found a Bug",
		"Node is already root, no Parent",
		"No (usable) space left on device",
		"Not enough RAM for cache",
		"Operation not permitted",
		"Directory is not empty",
		"Cache had to be flushed, any current TCN can be invalidated now",
		"You should not be seeing this..."
};

const char* paffs_err_msg(PAFFS_RESULT pr){
	return PAFFS_RESULT_MSG[pr];
}

PAFFS_RESULT paffs_getLastErr(){
	return paffs_lasterr;
}

void paffs_resetLastErr(){
	paffs_lasterr = PAFFS_OK;
}

PAFFS_RESULT paffs_initialize(p_dev* dev){
	device = dev;
	p_param* param = &device->param;
	param->areas_no = param->blocks / 2;	//For now: 16 b -> 8 Areas
	param->blocks_per_area = param->blocks / param->areas_no;
	param->data_bytes_per_page = param->total_bytes_per_page - param->oob_bytes_per_page;
	param->pages_per_area = param->pages_per_block * param->blocks_per_area;
	device->areaMap = malloc(sizeof(p_area) * device->param.areas_no);
	device->drv.drv_initialise_fn(dev);

	activeArea[SUPERBLOCKAREA] = 0;
	activeArea[INDEXAREA] = 0;
	activeArea[JOURNALAREA] = 0;
	activeArea[DATAAREA] = 0;

	return PAFFS_OK;
}

PAFFS_RESULT paffs_mnt(const char* devicename){
	if(strcmp(devicename, device->param.name) != 0){
		paffs_lasterr = PAFFS_EINVAL;
		return PAFFS_EINVAL;
	}
	//TODO: Read Superblock on nand-Flash
	bool emptyFlash = true;

	if(emptyFlash){
		bool had_superblock = false;	//Todo: have_enough
		bool had_index = false;
		bool had_journal = false;
		for(int area = 0; area < device->param.areas_no; area++){
			device->areaMap[area].status = EMPTY;
			device->areaMap[area].erasecount = 0;
			device->areaMap[area].position = area;
			device->areaMap[area].dirtyPages = 0;

			if(!had_superblock){
				device->areaMap[area].type = SUPERBLOCKAREA;
				had_superblock = true;
				activeArea[SUPERBLOCKAREA] = area;
				initArea(device, area);
				continue;
			}
			if(!had_index){
				device->areaMap[area].type = INDEXAREA;
				had_index = true;
				activeArea[INDEXAREA] = area;
				initArea(device, area);
				continue;
			}
			if(!had_journal){
				device->areaMap[area].type = JOURNALAREA;
				had_journal = true;
				activeArea[JOURNALAREA] = area;
				initArea(device, area);
				continue;
			}
			device->areaMap[area].type = DATAAREA;

		}

		PAFFS_RESULT r = start_new_tree(device);
		if(r != PAFFS_OK)
			return r;

	}else{
		//Todo: Scan NAND-Flash
	}
	pInode rootDir = {0};
	if(paffs_createDirInode(&rootDir, PAFFS_R | PAFFS_W | PAFFS_X) != PAFFS_OK){
		return paffs_lasterr;
	}
	if(insertInode(device, &rootDir) != PAFFS_OK){
		return paffs_lasterr;
	}
	return PAFFS_OK;
}

PAFFS_RESULT paffs_createInode(pInode* outInode, paffs_permission mask){
	memset(outInode, 0, sizeof(pInode));

	//FIXME: is this the best way to find a new number?
	PAFFS_RESULT r = findFirstFreeNo(device, &outInode->no);
	if(r != PAFFS_OK)
		return r;

	outInode->perm = mask;
	outInode->size = 0;
	outInode->reservedSize = 0;
	outInode->crea = time(0);
	outInode->mod = outInode->crea;
	return PAFFS_OK;
}

/**
 * creates DirInode ONLY IN RAM
 */
PAFFS_RESULT paffs_createDirInode(pInode* outInode, paffs_permission mask){
	if(paffs_createInode(outInode, mask) != PAFFS_OK)
		return PAFFS_BUG;
	outInode->type = PINODE_DIR;
	outInode->size = sizeof(dirEntryCount_t);		//to hold directory-entry-count. even if it is not commited to flash
	outInode->reservedSize = 0;
	return PAFFS_OK;
}

/**
 * creates FilInode ONLY IN RAM
 */
PAFFS_RESULT paffs_createFilInode(pInode* outInode, paffs_permission mask){
	if(paffs_createInode(outInode, mask) != PAFFS_OK){
		return PAFFS_BUG;
	}
	outInode->type = PINODE_FILE;
	return PAFFS_OK;
}

void paffs_destroyInode(pInode* node){
	deleteInodeData(node, device, 0);
	free(node);
}

PAFFS_RESULT paffs_getParentDir(const char* fullPath, pInode* parDir, unsigned int *lastSlash){
	if(fullPath[0] == 0){
		paffs_lasterr = PAFFS_EINVAL;
		return PAFFS_EINVAL;
	}

	unsigned int p = 0;
	*lastSlash = 0;

	while(fullPath[p] != 0){
		if(fullPath[p] == '/' && fullPath[p+1] != 0){   //Nicht /a/b/c/ erkennen, sondern /a/b/c
			*lastSlash = p+1;
		}
		p++;
	}

	char* pathC = malloc(*lastSlash+1);
	memcpy(pathC, fullPath, *lastSlash);
	pathC[*lastSlash] = 0;

	PAFFS_RESULT r = paffs_getInodeOfElem(parDir, pathC);
	free(pathC);
	return r;
}

//Currently Linearer Aufwand
PAFFS_RESULT paffs_getInodeInDir( pInode* outInode, pInode* folder, const char* name){
	if(folder->type != PINODE_DIR){
		return PAFFS_BUG;
	}

	if(folder->size <= sizeof(dirEntryCount_t)){
		//Just contains a zero for "No entrys"
		return PAFFS_NF;
	}

	char* buf = malloc(folder->size);
	unsigned int bytes_read = 0;
	PAFFS_RESULT r = readInodeData(folder, 0, folder->size, &bytes_read, buf, device);
	if(r != PAFFS_OK || bytes_read != folder->size){
		free(buf);
		return r == PAFFS_OK ? PAFFS_BUG : r;
	}

	unsigned int p = sizeof(dirEntryCount_t);		//skip directory entry count
	while(p < folder->size){
			dirEntryLength_t direntryl = buf[p];
			if(direntryl < sizeof(dirEntryLength_t)){
				return PAFFS_BUG;
			}
			unsigned int dirnamel = direntryl - sizeof(dirEntryLength_t) - sizeof(pInode_no);
			p += sizeof(dirEntryLength_t);
			pInode_no tmp_no;
			memcpy(&tmp_no, &buf[p], sizeof(pInode_no));
			p += sizeof(pInode_no);
			char* tmpname = malloc((dirnamel+1) * sizeof(char));
			memcpy(tmpname, &buf[p], dirnamel);
			tmpname[dirnamel] = 0;
			p += dirnamel;
			if(strcmp(name, tmpname) == 0){
				//Eintrag gefunden
				if(getInode(device, tmp_no, outInode) != PAFFS_OK){
					PAFFS_DBG(PAFFS_TRACE_BUG, "BUG: Found Element '%s' in dir, but did not find its Inode (No. %d) in Index!", tmpname, tmp_no);
					free(tmpname);
					return PAFFS_BUG;
				}
				free(tmpname);
				free(buf);
				return PAFFS_OK;
			}
			free(tmpname);
	}
	free(buf);
	return PAFFS_NF;

}

PAFFS_RESULT paffs_getInodeOfElem(pInode* outInode, const char* fullPath){
    pInode root;
    if(getInode(device, 0, &root) != PAFFS_OK){
            return paffs_lasterr = PAFFS_BUG;
    }
    pInode *curr = outInode;
    *curr = root;
    
    unsigned int fpLength = strlen(fullPath);
    char* fullPathC = malloc(fpLength * sizeof(char) +1);
    memcpy(fullPathC, fullPath, fpLength * sizeof(char));
    fullPathC[fpLength] = 0;
    
    char delimiter[] = "/"; 
    char *fnP;
    fnP = strtok(fullPathC, delimiter);
    
    while(fnP != NULL){
        if(strlen(fnP) == 0){   //is first '/'
            continue;
        }

        if(curr->type != PINODE_DIR){
            free(fullPathC);
            return PAFFS_EINVAL;
        }

        PAFFS_RESULT r;
        if((r = paffs_getInodeInDir(outInode, curr, fnP)) != PAFFS_OK){
        	free(fullPathC);
            return r;
        }
        curr = outInode;
        //todo: Dentry cachen
        fnP = strtok(NULL, delimiter);
    }
    
    free(fullPathC);
    return PAFFS_OK;
}


PAFFS_RESULT paffs_insertInodeInDir(const char* name, pInode* contDir, pInode* newElem){
	if(contDir == NULL){
		paffs_lasterr = PAFFS_BUG;
		return PAFFS_BUG;
	}

	unsigned int dirnamel = strlen(name);
	if(name[dirnamel-1] == '/'){
		dirnamel--;
	}

	//TODO: Check if name already exists

	dirEntryLength_t direntryl = sizeof(dirEntryLength_t) + sizeof(pInode_no) + dirnamel;	//Size of the new directory entry

	unsigned char *buf = (unsigned char*) malloc(direntryl);
	buf[0] = direntryl;
	memcpy(&buf[sizeof(dirEntryLength_t)], &newElem->no, sizeof(pInode_no));

	memcpy(&buf[sizeof(dirEntryLength_t) + sizeof(pInode_no)], name, dirnamel);

	char* dirData = malloc(contDir->size +  direntryl);
	unsigned int bytes = 0;
	PAFFS_RESULT r;
	if(contDir->reservedSize > 0){		//if Directory is not empty
		r = readInodeData(contDir, 0, contDir->size, &bytes, dirData, device);
		if(r != PAFFS_OK || bytes != contDir->size){
			paffs_lasterr = r;
			free(dirData);
			free(buf);
			return r;
		}
	}else{
		memset(dirData, 0, contDir->size);	//Wipe directory-entry-count area
	}

	//append record
	memcpy (&dirData[contDir->size], buf, direntryl);

	dirEntryCount_t directoryEntryCount = 0;
	memcpy (&directoryEntryCount, dirData, sizeof(dirEntryCount_t));
	directoryEntryCount ++;
	memcpy (dirData, &directoryEntryCount, sizeof(dirEntryCount_t));

	r = writeInodeData(contDir, 0, contDir->size + direntryl, &bytes, dirData, device);
	free(dirData);
	free(buf);
	if(bytes != contDir->size)
		r = r == PAFFS_OK ? PAFFS_BUG : r;
	return r;

}


//TODO: mark deleted treeCacheNodes as dirty
PAFFS_RESULT paffs_removeInodeFromDir(pInode* contDir, pInode* elem){
	if(contDir == NULL){
		paffs_lasterr = PAFFS_BUG;
		return PAFFS_BUG;
	}

	char* dirData = malloc(contDir->size);
	unsigned int bytes = 0;
	PAFFS_RESULT r;
	if(contDir->reservedSize > 0){		//if Directory is not empty
		r = readInodeData(contDir, 0, contDir->size, &bytes, dirData, device);
		if(r != PAFFS_OK || bytes != contDir->size){
			paffs_lasterr = r;
			free(dirData);
			return r;
		}
	}else{
		return PAFFS_NF;	//did not find directory entry, because dir is empty
	}


	dirEntryCount_t *entries = (dirEntryCount_t*) &dirData[0];
	fileSize_t pointer = sizeof(dirEntryCount_t);
	while(pointer < contDir->size){
		dirEntryLength_t entryl = (dirEntryLength_t) dirData[pointer];
		if(memcmp(&dirData[pointer + sizeof(dirEntryLength_t)], &(elem->no), sizeof(pInode_no)) == 0){
			//Found
			unsigned int newSize = contDir->size - entryl;
			unsigned int restByte = newSize - pointer;

			if((r = deleteInodeData(contDir, device, newSize)) != PAFFS_OK)
				return r;

			if(restByte > 0 && restByte < 4)	//should either be 0 (no entries left) or bigger than 4 (minimum size for one entry)
				PAFFS_DBG(PAFFS_TRACE_BUG, "Something is fishy! (%d)", restByte);

			if(newSize == 0)
				return PAFFS_OK;

			(*entries)--;
			memcpy(&dirData[pointer], &dirData[pointer + entryl], restByte);

			unsigned int bw = 0;
			return writeInodeData(contDir, 0, newSize, &bw, dirData, device);
		}
		pointer += entryl;
	}

	return PAFFS_NF;
}

PAFFS_RESULT paffs_mkdir(const char* fullPath, paffs_permission mask){
	unsigned int lastSlash = 0;

	pInode parDir;
	PAFFS_RESULT res = paffs_getParentDir(fullPath, &parDir, &lastSlash);
	if(res != PAFFS_OK)
		return res;

	pInode newDir;
	PAFFS_RESULT r = paffs_createDirInode(&newDir, mask);
	if(r != PAFFS_OK)
		return r;
	r = insertInode(device, &newDir);
	if(r != PAFFS_OK)
		return r;

	return paffs_insertInodeInDir(&fullPath[lastSlash], &parDir, &newDir);
}

paffs_dir* paffs_opendir(const char* path){
	if(path[0] == 0){
		paffs_lasterr = PAFFS_EINVAL;
		return NULL;
	}

	pInode dirPinode;
	PAFFS_RESULT r = paffs_getInodeOfElem(&dirPinode, path);
	if(r != PAFFS_OK){
		if(r != PAFFS_NF){
			PAFFS_DBG(PAFFS_TRACE_BUG, "BUG? '%s'", paffs_err_msg(r));
		}
		paffs_lasterr = r;
		return NULL;
	}

	char* dirData = malloc(dirPinode.size);
	unsigned int br = 0;
	if(dirPinode.reservedSize > 0){
		r = readInodeData(&dirPinode, 0, dirPinode.size, &br, dirData, device);
		if(r != PAFFS_OK || br != dirPinode.size){
			paffs_lasterr = r;
			return NULL;
		}
	}else{
		memset(dirData, 0, dirPinode.size);
	}


	paffs_dir* dir = malloc(sizeof(paffs_dir));
	dir->dentry = malloc(sizeof(pDentry));
	dir->dentry->name = "not_impl.";
	dir->dentry->iNode = malloc(sizeof(pInode));
	*dir->dentry->iNode = dirPinode;
	dir->dentry->parent = NULL;
	dir->no_entrys = dirData[0];
	dir->dirents = malloc(dir->no_entrys * sizeof(paffs_dirent*));
	dir->pos = 0;

	unsigned int p = sizeof(dirEntryCount_t);
	unsigned int entry;
	for(entry = 0; p < dirPinode.size; entry++){

		dir->dirents[entry] = malloc (sizeof(paffs_dirent));
		memset(dir->dirents[entry], 0, sizeof(paffs_dirent));
		dirEntryLength_t direntryl = dirData[p];
		unsigned int dirnamel = direntryl - sizeof(dirEntryLength_t) - sizeof(pInode_no);
		if(dirnamel > 1 << sizeof(dirEntryLength_t) * 8){
			//We have an error while reading
			PAFFS_DBG(PAFFS_TRACE_BUG, "Dirname length was bigger than possible (%u)!", dirnamel);
			for(int i = 0; i <= entry; i++)
				free(dir->dirents[i]);
			free(dir->dirents);
			free(dirData);
			free(dir->dentry);
			free(dir->dentry->iNode);
			free(dir);
			paffs_lasterr = PAFFS_BUG;
			return NULL;
		}
		p += sizeof(dirEntryLength_t);
		memcpy(&dir->dirents[entry]->node_no, &dirData[p], sizeof(pInode_no));
		dir->dirents[entry]->node = NULL;
		p += sizeof(pInode_no);
		dir->dirents[entry]->name = malloc(dirnamel+2);    //+2 weil 1. Nullbyte und 2. Vielleicht ein Zeichen '/' dazukommt
		memcpy(dir->dirents[entry]->name, &dirData[p], dirnamel);
		dir->dirents[entry]->name[dirnamel] = 0;
		p += dirnamel;
	}

	free(dirData);

	if(entry != dir->no_entrys){
		PAFFS_DBG(PAFFS_TRACE_BUG, "Directory stated it had %u entries, but has actually %u!", dir->no_entrys, entry);
		paffs_lasterr = PAFFS_BUG;
		return NULL;
	}

	return dir;
}

PAFFS_RESULT paffs_closedir(paffs_dir* dir){
	if(dir->dirents == NULL)
		return PAFFS_EINVAL;
    for(int i = 0; i < dir->no_entrys; i++){
        free(dir->dirents[i]->name);
        if(dir->dirents[i]->node != NULL)
        	free(dir->dirents[i]->node);
        free(dir->dirents[i]);
    }
    free(dir->dirents);
    free(dir->dentry->iNode);
    free(dir->dentry);
    free(dir);
    return PAFFS_OK;
}

/**
 * TODO: What happens, if dir is changed after opendir?
 */
paffs_dirent* paffs_readdir(paffs_dir* dir){
	if(dir->no_entrys == 0)
		return NULL;

	if(dir->pos == dir->no_entrys){
		return NULL;
	}

	if(dir->dirents == NULL){
		PAFFS_DBG(PAFFS_TRACE_ERROR, "READ DIR on dir with NULL dirents");
		paffs_lasterr = PAFFS_BUG;
		return NULL;
	}

	if(dir->pos > dir->no_entrys){
		PAFFS_DBG(PAFFS_TRACE_ERROR, "READ DIR on dir that points further than its contents");
		paffs_lasterr = PAFFS_BUG;
		return NULL;
	}

	if(dir->dirents[dir->pos] == NULL){
		PAFFS_DBG(PAFFS_TRACE_ERROR, "READ DIR on dir with NULL dirent no. %d", dir->pos);
		paffs_lasterr = PAFFS_BUG;
		return NULL;
	}

	if(dir->dirents[dir->pos]->node != NULL){
		return dir->dirents[dir->pos++];
	}
	pInode item;
	PAFFS_RESULT r = getInode(device, dir->dirents[dir->pos]->node_no, &item);
	if(r != PAFFS_OK){
	   paffs_lasterr = PAFFS_BUG;
	   return NULL;
	}
	dir->dirents[dir->pos]->node = malloc(sizeof(pInode));
	*dir->dirents[dir->pos]->node = item;
	if(dir->dirents[dir->pos]->node->type == PINODE_DIR){
		int namel = strlen(dir->dirents[dir->pos]->name);
		dir->dirents[dir->pos]->name[namel] = '/';
		dir->dirents[dir->pos]->name[namel+1] = 0;
	}
	return dir->dirents[dir->pos++];
}

void paffs_rewinddir(paffs_dir* dir){
    dir->pos = 0;
}

PAFFS_RESULT paffs_createFile(pInode* outFile, const char* fullPath, paffs_permission mask){
	unsigned int lastSlash = 0;

	pInode parDir;
	PAFFS_RESULT res = paffs_getParentDir(fullPath, &parDir, &lastSlash);
	if(res != PAFFS_OK)
		return res;

	if(paffs_createFilInode(outFile, mask) != PAFFS_OK){
		return PAFFS_BUG;
	}
	res = insertInode(device, outFile);
	if(res != PAFFS_OK)
		return res;

	return paffs_insertInodeInDir(&fullPath[lastSlash], &parDir, outFile);
}


p_addr combineAddress(uint32_t logical_area, uint32_t page){
	p_addr addr = 0;
	memcpy(&addr, &logical_area, sizeof(uint32_t));
	memcpy(&((char*)&addr)[sizeof(uint32_t)], &page, sizeof(uint32_t));

	return addr;
}

unsigned int extractLogicalArea(p_addr addr){
	unsigned int area = 0;
	memcpy(&area, &addr, sizeof(uint32_t));
	return area;
}
unsigned int extractPage(p_addr addr){
	unsigned int page = 0;
	memcpy(&page, &((char*)&addr)[sizeof(uint32_t)], sizeof(uint32_t));
	return page;
}

unsigned long long getPageNumber(p_addr addr, p_dev *dev){
	unsigned long long page = dev->areaMap[extractLogicalArea(addr)].position *
								dev->param.blocks_per_area * dev->param.pages_per_block;
	page += extractPage(addr);
	return page;
}

paffs_obj* paffs_open(const char* path, fileopenmask mask){
	pInode file;
	PAFFS_RESULT r = paffs_getInodeOfElem(&file, path);
	if(r == PAFFS_NF){
		//create new file
		if(mask & PAFFS_FC){
			PAFFS_RESULT r = paffs_createFile(&file, path, mask);
			if(r != PAFFS_OK)
				paffs_lasterr = r;
				return NULL;
		}else{
			return NULL;
		}
	}else if(r != PAFFS_OK){
		paffs_lasterr = r;
		return NULL;
	}

	if(file.type == PINODE_LNK){
		//LINKS are not supported yet
		paffs_lasterr = PAFFS_NIMPL;
		return NULL;
	}

	if(file.type == PINODE_DIR){
		//tried to open directory as file
		paffs_lasterr = PAFFS_EINVAL;
		return NULL;
	}

	paffs_obj* obj = malloc(sizeof(paffs_obj));
	obj->dentry = malloc(sizeof(pDentry));
	obj->dentry->name = malloc(strlen(path));
	obj->dentry->iNode = malloc(sizeof(pInode));
	*obj->dentry->iNode = file;
	obj->dentry->parent = NULL;		//TODO: Sollte aus cache gesucht werden, erstellt in "paffs_getInodeOfElem(path))" ?

	memcpy(obj->dentry->name, path, strlen(path));

	if(mask & PAFFS_FA){
		obj->fp = file.size;
	}else{
		obj->fp = 0;
	}

	obj->rdnly = ! (mask & PAFFS_FW);

	return obj;
}

PAFFS_RESULT paffs_close(paffs_obj* obj){
	paffs_flush(obj);
	free(obj->dentry->iNode);
	free(obj->dentry);
	free(obj);

	return PAFFS_OK;
}

PAFFS_RESULT paffs_touch(const char* path){
	pInode file;
	PAFFS_RESULT r = paffs_getInodeOfElem(&file, path);
	if(r == PAFFS_NF){
		//create new file
		PAFFS_RESULT r2 = paffs_createFile(&file, path, PAFFS_R | PAFFS_W);
		if(r2 != PAFFS_OK){
			return r2;
		}
		return PAFFS_OK;
	}else{
		if(r != PAFFS_OK)
			return r;
		file.mod = time(0);
		return updateExistingInode(device, &file);
	}

}


PAFFS_RESULT paffs_getObjInfo(const char *fullPath, paffs_objInfo* nfo){
	pInode object;
	PAFFS_RESULT r;
	if((r = paffs_getInodeOfElem(&object, fullPath)) != PAFFS_OK){
		return r;
	}
	nfo->created = object.crea;
	nfo->modified = object.mod;
	nfo->perm = object.perm;
	nfo->size = object.size;
	nfo->isDir = object.type == PINODE_DIR;
	return PAFFS_OK;
}

PAFFS_RESULT paffs_read(paffs_obj* obj, char* buf, unsigned int bytes_to_read, unsigned int *bytes_read){
	if(obj == NULL)
		return paffs_lasterr = PAFFS_EINVAL;

	if(obj->dentry->iNode->type == PINODE_DIR){
		return paffs_lasterr = PAFFS_EINVAL;
	}
	if(obj->dentry->iNode->type == PINODE_LNK){
		return paffs_lasterr = PAFFS_NIMPL;
	}
	PAFFS_RESULT r = readInodeData(obj->dentry->iNode, obj->fp, bytes_to_read, bytes_read, buf, device);
	if(r != PAFFS_OK){
		return r;
	}
	//TODO: Check if actually read that much!
	*bytes_read = bytes_to_read;
	obj->fp += *bytes_read;
	return PAFFS_OK;
}

PAFFS_RESULT paffs_write(paffs_obj* obj, const char* buf, unsigned int bytes_to_write, unsigned int *bytes_written){
	if(obj == NULL)
		return paffs_lasterr = PAFFS_EINVAL;

	if(obj->dentry->iNode->type == PINODE_DIR){
		return paffs_lasterr = PAFFS_EINVAL;
	}
	if(obj->dentry->iNode->type == PINODE_LNK){
		return paffs_lasterr = PAFFS_NIMPL;
	}

	PAFFS_RESULT r = writeInodeData(obj->dentry->iNode, obj->fp, bytes_to_write, bytes_written, buf, device);
	if(r != PAFFS_OK){
		return r;
	}

	obj->dentry->iNode->mod = time(0);

	obj->fp += *bytes_written;
	if(obj->fp > obj->dentry->iNode->size){
		//size was increased
		if(obj->dentry->iNode->reservedSize < obj->fp){
			PAFFS_DBG(PAFFS_TRACE_BUG, "Reserved size is smaller than actual size?!");
			return PAFFS_BUG;
		}
	}
	return updateExistingInode(device, obj->dentry->iNode);
}

PAFFS_RESULT paffs_seek(paffs_obj* obj, int m, paffs_seekmode mode){
	switch(mode){
	case PAFFS_SEEK_SET:
		if(m < 0)
			return paffs_lasterr = PAFFS_EINVAL;
		obj->fp = m;
		break;
	case PAFFS_SEEK_END:
		obj->fp = obj->dentry->iNode->size + m;
		break;
	case PAFFS_SEEK_CUR:
		obj->fp += m;
		break;
	}

	return PAFFS_OK;
}


PAFFS_RESULT paffs_flush(paffs_obj* obj){
	return PAFFS_OK;
}

PAFFS_RESULT paffs_chmod(const char* path, paffs_permission perm){
	pInode object;
	PAFFS_RESULT r;
	if((r = paffs_getInodeOfElem(&object, path)) != PAFFS_OK){
		return r;
	}
	object.perm = perm;
	return updateExistingInode(device, &object);
}
PAFFS_RESULT paffs_remove(const char* path){
	pInode object;
	PAFFS_RESULT r;
	if((r = paffs_getInodeOfElem(&object, path)) != PAFFS_OK)
		return r;

	if(!(object.perm & PAFFS_W))
		return PAFFS_NOPERM;

	if(object.type == PINODE_DIR)
		if(object.size > sizeof(dirEntryCount_t))
			return PAFFS_DIRNOTEMPTY;

	if((r = deleteInodeData(&object, device, 0)) != PAFFS_OK)
		return r;

	pInode parentDir;
	unsigned int lastSlash = 0;
	if((r = paffs_getParentDir(path, &parentDir, &lastSlash)) != PAFFS_OK)
		return r;

	if((r = paffs_removeInodeFromDir(&parentDir, &object)) != PAFFS_OK)
		return r;
	return deleteInode(device, object.no);
}


//ONLY FOR DEBUG
p_dev* getDevice(){
	return device;
}

