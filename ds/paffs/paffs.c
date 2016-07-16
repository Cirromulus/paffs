/**
 * File created on 19.05.2016
 * Author: Pascal Pieper
 */

#include "paffs.h"
#include "paffs_flash.h"
#include "btree.h"
#include <linux/string.h>
#include <time.h>

//Eigentlich auf dem Speicher
static node* paffs_root = NULL;

static p_dev* device = NULL;

//Dentrys zur schnelleren verfügung
/*#define DENTRY_BUFSIZE 10
static pDentry* dentry_buf[DENTRY_BUFSIZE];
static unsigned char dentrys_buf_used = 0;
*/

//Address-wrapper für einfacheren Garbagecollector
/*p_addr combineAddress(unsigned int area, unsigned int page){
	unsigned long long ar = area;
	return  ar << 8 * 4;// << (sizeof(unsigned long) * 8) | page;
}

unsigned int extractArea(p_addr addr){
	return addr >> (sizeof(unsigned long) * 8);	//Whut?
}

unsigned int extractPage(p_addr addr){
	return addr & 0xFFFFFFFF;
}*/

const char* paffs_err_msg(PAFFS_RESULT pr){
	return PAFFS_RESULT_MSG[pr];
}

PAFFS_RESULT paffs_getLastErr(){
	return paffs_lasterr;
}

PAFFS_RESULT paffs_initialize(p_dev* dev){
	device = dev;
	p_param* param = &device->param;
	param->areas_no = param->blocks / 2;	//For now: 16 b -> 8 Areas
	param->blocks_per_area = param->blocks / param->areas_no;
	param->data_bytes_per_page = param->total_bytes_per_page - param->oob_bytes_per_page;
	device->areaMap = malloc(sizeof(p_area) * device->param.areas_no);
	device->drv.drv_initialise_fn(dev);
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
				continue;
			}
			if(!had_index){
				device->areaMap[area].type = INDEXAREA;
				had_index = true;
				continue;
			}
			if(!had_journal){
				device->areaMap[area].type = JOURNALAREA;
				had_journal = true;
				continue;
			}
			device->areaMap[area].type = DATAAREA;

		}
	}else{
		//Todo: Scan NAND-Flash
	}
	pInode* rootDir = paffs_createDirInode(PAFFS_R | PAFFS_W | PAFFS_X);
	paffs_root = insert_direct(paffs_root, rootDir);
	return PAFFS_OK;
}

pInode* paffs_createInode(paffs_permission mask){
	pInode *newNode = malloc(sizeof(pInode));
	memset(newNode, 0, sizeof(pInode));
	newNode->no = find_first_free_key(paffs_root);
	newNode->perm = mask;
	newNode->size = 0;
	newNode->reservedSize = 0;
	newNode->crea = time(0);
	newNode->mod = newNode->crea;
	return newNode;
}


pInode* paffs_createDirInode(paffs_permission mask){
	pInode *newFolder = paffs_createInode(mask);
	newFolder->type = PINODE_DIR;
	newFolder->size = sizeof(unsigned int);		//For directoryentrycount

	unsigned int buf = 0;
	unsigned int bytes_written = 0;
	PAFFS_RESULT r = writeInodeData(newFolder, 0, sizeof(unsigned int), &bytes_written, &buf, device);
	if(r != PAFFS_OK || bytes_written != sizeof(unsigned int)){
		paffs_lasterr = r;
		return NULL;
	}
	return newFolder;
}

pInode* paffs_createFilInode(paffs_permission mask){
	pInode *newFil = paffs_createInode(mask);
	newFil->type = PINODE_FILE;
	return newFil;
}

void paffs_destroyInode(pInode* node){
	deleteInodeData(node, device);
	free(node);
}

PAFFS_RESULT paffs_getParentDir(const char* fullPath, pInode* *parDir, unsigned int *lastSlash){
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

	*parDir = paffs_getInodeOfElem(pathC);
	free(pathC);

	if(*parDir == NULL){
		return paffs_lasterr = PAFFS_NF;
	}
	return PAFFS_OK;
}

//Currently Linearer Aufwand
pInode* paffs_getInodeInDir(pInode* folder, const char* name){
        if(folder->type != PINODE_DIR){
            paffs_lasterr = PAFFS_BUG;
            return NULL;
        }
        
        if(folder->size <= sizeof(unsigned int)){
        	//Just contains a zero for "No entrys"
        	paffs_lasterr = PAFFS_NF;
        	return NULL;
        }

        char* buf = malloc(folder->size);
        unsigned int bytes_read = 0;
        PAFFS_RESULT r = readInodeData(folder, 0, folder->size, &bytes_read, buf, device);
        if(r != PAFFS_OK || bytes_read != folder->size){
        	free(buf);
        	paffs_lasterr = r == PAFFS_OK ? PAFFS_BUG : r;
        	return NULL;
        }

        unsigned int p = sizeof(unsigned int);		//skip directory entry count
        while(p < folder->size){
                unsigned int direntryl = buf[p];
                if(direntryl < sizeof(unsigned int)){
                	paffs_lasterr = PAFFS_BUG;
                	return NULL;
                }
                unsigned int dirnamel = direntryl - sizeof(unsigned int) - sizeof(pInode_no);
                p += sizeof(unsigned int);
                pInode_no tmp;
                memcpy(&tmp, &buf[p], sizeof(pInode_no));
                p += sizeof(pInode_no);
                char* tmpname = malloc((dirnamel+1) * sizeof(char));
                memcpy(tmpname, &buf[p], dirnamel);
                tmpname[dirnamel] = 0;
                p += dirnamel;
                if(strcmp(name, tmpname) == 0){
                    //Eintrag gefunden
                    free(tmpname);
                    pInode* out = find(paffs_root, tmp);
                    if(out == NULL){
                        paffs_lasterr = PAFFS_BUG;
                    }
                    free(buf);
                    return out;
                }
                free(tmpname);
        }
        free(buf);
        paffs_lasterr = PAFFS_NF;
        return NULL;
        
}

pInode* paffs_getInodeOfElem(const char* fullPath){
    pInode *curr = find(paffs_root, 0);
    if(curr == NULL){
            paffs_lasterr = PAFFS_BUG;
            return NULL;
    }
    
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
            paffs_lasterr = PAFFS_EINVAL;
            free(fullPathC);
            return NULL;
        }

        pInode *tmp = paffs_getInodeInDir(curr, fnP);
        if(tmp == NULL){
        	paffs_lasterr = PAFFS_NF;
        	free(fullPathC);
            return NULL;
        }
        curr = tmp;
        //todo: Dentry cachen
        fnP = strtok(NULL, delimiter);
    }
    
    free(fullPathC);
    return curr;
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

	unsigned int direntryl = sizeof(unsigned int) + sizeof(pInode_no) + dirnamel;	//Size of one directory entry in main memory

	paffs_root = insert_direct(paffs_root, newElem);

	unsigned char *buf = (unsigned char*) malloc(direntryl);
	buf[0] = direntryl;
	memcpy(&buf[sizeof(unsigned int)], &newElem->no, sizeof(pInode_no));

	memcpy(&buf[sizeof(unsigned int) + sizeof(pInode_no)], name, dirnamel);

	char* dirData = malloc(contDir->size +  direntryl);
	unsigned int bytes = 0;
	PAFFS_RESULT r = readInodeData(contDir, 0, contDir->size, &bytes, dirData, device);
	if(r != PAFFS_OK || bytes != contDir->size){
		paffs_lasterr = r;
		free(dirData);
		free(buf);
		return r;
	}

	//append record
	memcpy (&dirData[contDir->size], buf, direntryl);

	unsigned int directoryEntryCount = 0;
	memcpy (&directoryEntryCount, dirData, sizeof(unsigned int));
	directoryEntryCount ++;
	memcpy (dirData, &directoryEntryCount, sizeof(unsigned int));

	r = writeInodeData(contDir, 0, contDir->size + direntryl, &bytes, dirData, device);
	contDir->size += direntryl;
	free(dirData);
	free(buf);
	if(bytes != contDir->size + direntryl)
		r = r == PAFFS_OK ? PAFFS_BUG : r;
	return r;

}

PAFFS_RESULT paffs_mkdir(const char* fullPath, paffs_permission mask){
	unsigned int lastSlash = 0;

	pInode* parDir = NULL;
	PAFFS_RESULT res = paffs_getParentDir(fullPath, &parDir, &lastSlash);
	if(res != PAFFS_OK)
		return res;

	pInode* newDir = paffs_createDirInode(mask);

	return paffs_insertInodeInDir(&fullPath[lastSlash], parDir, newDir);
}

paffs_dir* paffs_opendir(const char* path){
	if(path[0] == 0){
		paffs_lasterr = PAFFS_EINVAL;
		return NULL;
	}

	pInode* dirPinode = paffs_getInodeOfElem(path);
	if(dirPinode == NULL){
		return NULL;
	}

	char* dirData = malloc(dirPinode->size);
	unsigned int br = 0;
	PAFFS_RESULT r = readInodeData(dirPinode, 0, dirPinode->size, &br, dirData, device);
	if(r != PAFFS_OK || br != dirPinode->size){
		paffs_lasterr = r;
		return NULL;
	}


	paffs_dir* dir = malloc(sizeof(paffs_dir));
	dir->dentry = malloc(sizeof(pDentry));
	dir->dentry->name = "not_impl.";	//Sollte in paffs_getInodeOfDir(path) gecached werden
	dir->dentry->iNode = dirPinode;
	dir->dentry->parent = NULL;
	dir->no_entrys = dirData[0];
	dir->dirents = malloc(dir->no_entrys * sizeof(paffs_dirent*));
	dir->pos = 0;

	unsigned int p = sizeof(unsigned int);
	unsigned int entry;
	for(entry = 0; p < dirPinode->size; entry++){

		dir->dirents[entry] = malloc (sizeof(paffs_dirent));
		unsigned int direntryl = dirData[p];
		unsigned int dirnamel = direntryl - sizeof(unsigned int) - sizeof(pInode_no);
		p += sizeof(unsigned int);
		memcpy(&dir->dirents[entry]->node_no, &dirData[p], sizeof(pInode_no));
                dir->dirents[entry]->node = NULL;
		p += sizeof(pInode_no);
		dir->dirents[entry]->name = malloc((dirnamel+2) * sizeof(char));    //+2 weil 1. Nullbyte und 2. Vielleicht ein Zeichen '/' dazukommt
		memcpy(dir->dirents[entry]->name, &dirData[p], dirnamel);
		dir->dirents[entry]->name[dirnamel] = 0;
		p += dirnamel;
	}

	free(dirData);

	return dir;
}

PAFFS_RESULT paffs_closedir(paffs_dir* dir){
    for(int i = 0; i < dir->no_entrys; i++){
        free(dir->dirents[i]->name);
        free(dir->dirents[i]);
    }
    free(dir->dirents);
    free(dir->dentry);
    free(dir);
    return PAFFS_OK;
}

paffs_dirent* paffs_readdir(paffs_dir* dir){
	if(dir->pos >= dir->no_entrys){
		return NULL;
	}
	pInode* item = find(paffs_root,  dir->dirents[dir->pos]->node_no);
        if(item == NULL){
           paffs_lasterr = PAFFS_BUG;
           return NULL;
        }
        dir->dirents[dir->pos]->node = item;
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

pInode* paffs_createFile(const char* fullPath, paffs_permission mask){
	unsigned int lastSlash = 0;

	pInode* parDir = NULL;
	PAFFS_RESULT res = paffs_getParentDir(fullPath, &parDir, &lastSlash);
	if(res != PAFFS_OK)
		return NULL;

	pInode* newFil = paffs_createFilInode(mask);

	res = paffs_insertInodeInDir(&fullPath[lastSlash], parDir, newFil);
	if(res != PAFFS_OK)
		return NULL;
	return newFil;
}

paffs_obj* paffs_open(const char* path, fileopenmask mask){
	pInode* file;
	if((file = paffs_getInodeOfElem(path)) == NULL){
		//create new file
		if(mask & PAFFS_FC){
			file = paffs_createFile(path, mask);
		}else{
			return NULL;
		}
	}
	paffs_obj* obj = malloc(sizeof(paffs_obj));
	obj->dentry = malloc(sizeof(pDentry));
	obj->dentry->name = "n.impl.";
	obj->dentry->iNode = file;
	obj->dentry->parent = NULL;		//Sollte aus cache gesucht werden, erstellt in "paffs_getInodeOfElem(path))" ?

	if(mask & PAFFS_FA){
		obj->fp = file->size;
	}else{
		obj->fp = 0;
	}

	obj->rdnly = ! (mask & PAFFS_FW);

	return obj;
}

PAFFS_RESULT paffs_close(paffs_obj* obj){
	paffs_flush(obj);
	free(obj->dentry);
	free(obj);

	return PAFFS_OK;
}

PAFFS_RESULT paffs_touch(const char* path){
	pInode* file;
	if((file = paffs_getInodeOfElem(path)) == NULL){
		//create new file
		if(paffs_createFile(path, PAFFS_R | PAFFS_W) == NULL){
			return paffs_lasterr;
		}
		//To Overwrite the PAFFS_NF in paffs_getInodeOfElem(path)
		paffs_lasterr = PAFFS_OK;
	}else{
		file->mod = time(0);
	}
	return PAFFS_OK;
}


PAFFS_RESULT paffs_getObjInfo(const char *fullPath, paffs_objInfo* nfo){
	pInode* object;
	if((object = paffs_getInodeOfElem(fullPath)) == NULL){
		return paffs_lasterr;
	}
	nfo->created = object->crea;
	nfo->modified = object->mod;
	nfo->perm = object->perm;
	nfo->size = object->size;
	nfo->isDir = object->type == PINODE_DIR;
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
		return paffs_lasterr = r;
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
		return paffs_lasterr = r;
	}

	obj->dentry->iNode->mod = time(0);

	obj->fp += *bytes_written;
	if(obj->fp > obj->dentry->iNode->size){
		//size was increased
		if(obj->dentry->iNode->reservedSize < obj->fp){
			PAFFS_DBG(PAFFS_TRACE_ALWAYS, "Reserved size is smaller than actual size?!");
			return PAFFS_BUG;
		}
		obj->dentry->iNode->size = obj->fp;
	}
	return PAFFS_OK;
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



