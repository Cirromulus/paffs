/**
 * File created on 19.05.2016
 * Author: Pascal Pieper
 */

#include "paffs.hpp"

#include "area.hpp"
#include "dataIO.hpp"
#include "treeCache.hpp"
#include "paffs_trace.hpp"
#include "summaryCache.hpp"
#include "driver/driverconf.hpp"

#include <stdio.h>
#include <linux/string.h>
#include <time.h>
#include <stdlib.h>


//Dirents zur schnelleren verfügung
/*#define Dirent_BUFSIZE 10
static Dirent* Dirent_buf[Dirent_BUFSIZE];
static unsigned char Dirents_buf_used = 0;
*/

namespace paffs{

unsigned int trace_mask =
	//PAFFS_TRACE_AREA |
	PAFFS_TRACE_ERROR |
	PAFFS_TRACE_BUG |
	//PAFFS_TRACE_TREE |
	PAFFS_TRACE_CACHE |
	//PAFFS_TRACE_SCAN |
	//PAFFS_TRACE_WRITE |
	PAFFS_TRACE_SUPERBLOCK |
	//PAFFS_TRACE_ALLOCATE |
	PAFFS_TRACE_VERIFY_AS |
	PAFFS_TRACE_GC |
	//PAFFS_TRACE_GC_DETAIL |
	0;

const char* resultMsg[] = {
		"ok",
		"Unknown error",
		"Object not found",
		"Object already exists",
		"Input values malformed",
		"Operation not yet supported",
		"Gratulations, you found a Bug",
		"Node is already root, no Parent",
		"No (usable) space left on device",
		"Not enough RAM for cache",
		"Operation not permitted",
		"Directory is not empty",
		"Cache had to be flushed, any current TCN can be invalidated now",
		"Flash needs retirement.",
		"You should not be seeing this..."
};

Result lasterr = Result::ok;

const char* err_msg(Result pr){
	return resultMsg[static_cast<int>(pr)];
}

Addr combineAddress(uint32_t logical_area, uint32_t page){
	Addr addr = 0;
	memcpy(&addr, &logical_area, sizeof(uint32_t));
	memcpy(&((char*)&addr)[sizeof(uint32_t)], &page, sizeof(uint32_t));

	return addr;
}

unsigned int extractLogicalArea(Addr addr){
	unsigned int area = 0;
	memcpy(&area, &addr, sizeof(uint32_t));
	return area;
}
unsigned int extractPage(Addr addr){
	unsigned int page = 0;
	memcpy(&page, &((char*)&addr)[sizeof(uint32_t)], sizeof(uint32_t));
	return page;
}

Paffs::Paffs(){
	//normal startup, load drivers
	device.driver = getDriver("1");
};
Paffs::Paffs(void* fc){
	device.driver = getDriverSpecial("1", fc);
}
Paffs::~Paffs(){
	if(device.areaMap != NULL){
		std::cerr << "Destroyed FS-Object without unmouning! "
				"This will most likely destroy "
				"the filesystem on flash." << std::endl;
		destroyDevice("1");
		//todo: If we handle multiple devices, this has to be changed as well
	}
	delete device.driver;
}


Result Paffs::getLastErr(){
	return lasterr;
}

void Paffs::resetLastErr(){
	lasterr = Result::ok;
}

Result Paffs::initializeDevice(const char* devicename){
	device.param = &device.driver->param;

	Param* param = device.param;
	param->areasNo = param->blocks / 2;	//For now: 16 b -> 8 Areas
	param->blocksPerArea = param->blocks / param->areasNo;
	param->dataBytesPerPage = param->totalBytesPerPage - param->oobBytesPerPage;
	param->totalPagesPerArea = param->pagesPerBlock * param->blocksPerArea;
	unsigned int needed_pages_for_AS = 1;	//Todo: actually calculate
	param->dataPagesPerArea = param->totalPagesPerArea - needed_pages_for_AS;
	device.areaMap = new Area[param->areasNo];
	memset(device.areaMap, 0, sizeof(Area) * param->areasNo);

	device.activeArea[AreaType::superblock] = 0;
	device.activeArea[AreaType::index] = 0;
	device.activeArea[AreaType::journal] = 0;
	device.activeArea[AreaType::data] = 0;

	if(param->blocksPerArea < 2){
		PAFFS_DBG(PAFFS_TRACE_ERROR, "Device too small, at least 12 Blocks are needed!");
		return Result::einval;
	}

	if(param->dataBytesPerPage != dataBytesPerPage){
		PAFFS_DBG(PAFFS_TRACE_ERROR, "Total bytes per Page differs between "
				"calculation and global define! (%d, %d)",
				param->dataBytesPerPage, dataBytesPerPage);
		return Result::einval;
	}

	if(param->dataPagesPerArea != dataPagesPerArea){
		PAFFS_DBG(PAFFS_TRACE_ERROR, "'Data pages per Area' differs between "
				"calculation and global define! (%d, %d)",
				param->dataPagesPerArea, dataBytesPerPage);
		return Result::einval;
	}

	return Result::ok;
}

Result Paffs::destroyDevice(const char* devicename){
	delete[] device.areaMap;
	device.areaMap = 0;
	memset(device.activeArea, 0, sizeof(AreaPos)*AreaType::no);
	return Result::ok;
}

Result Paffs::format(const char* devicename){
	Result r = initializeDevice(devicename);
	if(r != Result::ok)
		return r;

	unsigned char had_areaType = 0;		//Efficiency hack, bc there are less than 2⁸ area types
	for(unsigned int area = 0; area < device.param->areasNo; area++){
		device.areaMap[area].status = AreaStatus::empty;
		device.areaMap[area].erasecount = 0;
		device.areaMap[area].position = area;

		if(!(had_areaType & 1 << AreaType::superblock)){
			device.activeArea[AreaType::superblock] = area;
			device.areaMap[area].type = AreaType::superblock;
			initArea(&device, area);
			had_areaType |= 1 << AreaType::superblock;
			continue;
		}
		if(!(had_areaType & 1 << AreaType::journal)){
			device.activeArea[AreaType::journal] = area;
			device.areaMap[area].type = AreaType::journal;
			initArea(&device, area);
			had_areaType |= 1 << AreaType::journal;
			continue;
		}

		if(!(had_areaType & 1 << AreaType::garbageBuffer)){
			device.activeArea[AreaType::garbageBuffer] = area;
			device.areaMap[area].type = AreaType::garbageBuffer;
			initArea(&device, area);
			had_areaType |= 1 << AreaType::garbageBuffer;
			continue;
		}

		device.areaMap[area].type = AreaType::unset;

	}

	r = start_new_tree(&device);
	if(r != Result::ok)
		return r;

	Inode rootDir = {0};
	r = createDirInode(&rootDir, R | W | X);
	if(r != Result::ok){
		destroyDevice(devicename);
		return r;
	}
	r = insertInode(&device, &rootDir);
	if(r != Result::ok){
		destroyDevice(devicename);
		return r;
	}
	r = commitTreeCache(&device);
	if(r != Result::ok){
		destroyDevice(devicename);
		return r;
	}
	r = commitAreaSummaries(&device);
	if(r != Result::ok){
		destroyDevice(devicename);
		return r;
	}
	destroyDevice(devicename);
	return Result::ok;
}

Result Paffs::mnt(const char* devicename){
	if(strcmp(devicename, device.param->name) != 0){
		return Result::einval;
	}

	Result r = initializeDevice(devicename);
	if(r != Result::ok)
		return r;

	r = loadAreaSummaries(&device);
	if(r == Result::nf){
		PAFFS_DBG(PAFFS_TRACE_ERROR, "Tried mounting a device with an empty superblock!");
		destroyDevice(devicename);
		return r;
	}


	for(AreaPos i = 0; i < device.param->areasNo; i++){
		if(device.areaMap[i].type == AreaType::garbageBuffer){
			device.activeArea[AreaType::garbageBuffer] = i;
		}
		//Superblock do not need an active Area,
		//data and index active areas are extracted by areaSummaryCache
	}

	return r;
}
Result Paffs::unmnt(const char* devicename){
	if(strcmp(devicename, device.param->name) != 0){
		return Result::einval;
	}

	if(trace_mask && PAFFS_TRACE_AREA){
		printf("Info: \n");
		for(unsigned int i = 0; i < device.param->areasNo; i++){
			printf("\tArea %d on %u as %10s from page %4d %s\n"
					, i, device.areaMap[i].position, area_names[device.areaMap[i].type]
					, device.areaMap[i].position*device.param->blocksPerArea*device.param->pagesPerBlock
					, areaStatusNames[device.areaMap[i].status]);
		}
	}

	Result r = commitTreeCache(&device);
	if(r != Result::ok)
		return r;

	r = commitAreaSummaries(&device);
	if(r != Result::ok)
		return r;

	destroyDevice(devicename);

	//just for cleanup & tests
	deleteTreeCache();

	return Result::ok;
}

Result Paffs::createInode(Inode* outInode, Permission mask){
	memset(outInode, 0, sizeof(Inode));

	//FIXME: is this the best way to find a new number?
	Result r = findFirstFreeNo(&device, &outInode->no);
	if(r != Result::ok)
		return r;

	outInode->perm = (mask & PERM_MASK);
	if(mask & W)
		outInode->perm |= R;
	outInode->size = 0;
	outInode->reservedSize = 0;
	outInode->crea = time(0);
	outInode->mod = outInode->crea;
	return Result::ok;
}

/**
 * creates DirInode ONLY IN RAM
 */
Result Paffs::createDirInode(Inode* outInode, Permission mask){
	if(createInode(outInode, mask) != Result::ok)
		return Result::bug;
	outInode->type = InodeType::dir;
	outInode->size = sizeof(DirEntryCount);		//to hold directory-entry-count. even if it is not commited to flash
	outInode->reservedSize = 0;
	return Result::ok;
}

/**
 * creates FilInode ONLY IN RAM
 */
Result Paffs::createFilInode(Inode* outInode, Permission mask){
	if(createInode(outInode, mask) != Result::ok){
		return Result::bug;
	}
	outInode->type = InodeType::file;
	return Result::ok;
}

void Paffs::destroyInode(Inode* node){
	deleteInodeData(node, &device, 0);
	free(node);
}

Result Paffs::getParentDir(const char* fullPath, Inode* parDir, unsigned int *lastSlash){
	if(fullPath[0] == 0){
		lasterr = Result::einval;
		return Result::einval;
	}

	unsigned int p = 0;
	*lastSlash = 0;

	while(fullPath[p] != 0){
		if(fullPath[p] == '/' && fullPath[p+1] != 0){   //Nicht /a/b/c/ erkennen, sondern /a/b/c
			*lastSlash = p+1;
		}
		p++;
	}


	char* pathC = (char*) malloc(*lastSlash+1);
	memcpy(pathC, fullPath, *lastSlash);
	pathC[*lastSlash] = 0;

	Result r = getInodeOfElem(parDir, pathC);
	free(pathC);
	return r;
}

//Currently Linearer Aufwand
Result Paffs::getInodeInDir( Inode* outInode, Inode* folder, const char* name){
	if(folder->type != InodeType::dir){
		return Result::bug;
	}

	if(folder->size <= sizeof(DirEntryCount)){
		//Just contains a zero for "No entrys"
		return Result::nf;
	}

	char* buf = (char*) malloc(folder->size);
	unsigned int bytes_read = 0;
	Result r = readInodeData(folder, 0, folder->size, &bytes_read, buf, &device);
	if(r != Result::ok || bytes_read != folder->size){
		free(buf);
		return r == Result::ok ? Result::bug : r;
	}

	unsigned int p = sizeof(DirEntryCount);		//skip directory entry count
	while(p < folder->size){
			DirEntryLength direntryl = buf[p];
			if(direntryl < sizeof(DirEntryLength)){
				return Result::bug;
			}
			unsigned int dirnamel = direntryl - sizeof(DirEntryLength) - sizeof(InodeNo);
			p += sizeof(DirEntryLength);
			InodeNo tmp_no;
			memcpy(&tmp_no, &buf[p], sizeof(InodeNo));
			p += sizeof(InodeNo);
			if(p + dirnamel > folder->size){
				PAFFS_DBG(PAFFS_TRACE_ERROR, "ERROR: foldersize of Inode %u not plausible!", folder->no);
				return Result::fail;
			}
			char* tmpname = (char*) malloc((dirnamel+1) * sizeof(char));
			memcpy(tmpname, &buf[p], dirnamel);
			tmpname[dirnamel] = 0;
			p += dirnamel;
			if(strcmp(name, tmpname) == 0){
				//Eintrag gefunden
				if(getInode(&device, tmp_no, outInode) != Result::ok){
					PAFFS_DBG(PAFFS_TRACE_BUG, "Result::bug: Found Element '%s' in dir, but did not find its Inode (No. %d) in Index!", tmpname, tmp_no);
					free(tmpname);
					return Result::bug;
				}
				free(tmpname);
				free(buf);
				return Result::ok;
			}
			free(tmpname);
	}
	free(buf);
	return Result::nf;

}

Result Paffs::getInodeOfElem(Inode* outInode, const char* fullPath){
	Inode root;
	if(getInode(&device, 0, &root) != Result::ok){
		PAFFS_DBG(PAFFS_TRACE_ERROR, "Could not find rootInode! (%s)", resultMsg[static_cast<int>(lasterr)]);
		return Result::fail;
	}
	Inode *curr = outInode;
	*curr = root;

	unsigned int fpLength = strlen(fullPath);
	char* fullPathC = (char*) malloc(fpLength * sizeof(char) +1);
	memcpy(fullPathC, fullPath, fpLength * sizeof(char));
	fullPathC[fpLength] = 0;

	char delimiter[] = "/";
	char *fnP;
	fnP = strtok(fullPathC, delimiter);

	while(fnP != NULL){
		if(strlen(fnP) == 0){   //is first '/'
			continue;
		}

		if(curr->type != InodeType::dir){
			free(fullPathC);
			return Result::einval;
		}

		Result r;
		if((r = getInodeInDir(outInode, curr, fnP)) != Result::ok){
			free(fullPathC);
			return r;
		}
		curr = outInode;
		//todo: Dirent cachen
		fnP = strtok(NULL, delimiter);
	}

	free(fullPathC);
	return Result::ok;
}


Result Paffs::insertInodeInDir(const char* name, Inode* contDir, Inode* newElem){
	if(contDir == NULL){
		lasterr = Result::bug;
		return Result::bug;
	}

	unsigned int dirnamel = strlen(name);
	if(name[dirnamel-1] == '/'){
		dirnamel--;
	}

	//TODO: Check if name already exists

	DirEntryLength direntryl = sizeof(DirEntryLength) + sizeof(InodeNo) + dirnamel;	//Size of the new directory entry

	unsigned char *buf = (unsigned char*) malloc(direntryl);
	buf[0] = direntryl;
	memcpy(&buf[sizeof(DirEntryLength)], &newElem->no, sizeof(InodeNo));

	memcpy(&buf[sizeof(DirEntryLength) + sizeof(InodeNo)], name, dirnamel);

	char* dirData = (char*) malloc(contDir->size +  direntryl);
	unsigned int bytes = 0;
	Result r;
	if(contDir->reservedSize > 0){		//if Directory is not empty
		r = readInodeData(contDir, 0, contDir->size, &bytes, dirData, &device);
		if(r != Result::ok || bytes != contDir->size){
			lasterr = r;
			free(dirData);
			free(buf);
			return r;
		}
	}else{
		memset(dirData, 0, contDir->size);	//Wipe directory-entry-count area
	}

	//append record
	memcpy (&dirData[contDir->size], buf, direntryl);

	DirEntryCount directoryEntryCount = 0;
	memcpy (&directoryEntryCount, dirData, sizeof(DirEntryCount));
	directoryEntryCount ++;
	memcpy (dirData, &directoryEntryCount, sizeof(DirEntryCount));

	r = writeInodeData(contDir, 0, contDir->size + direntryl, &bytes, dirData, &device);
	free(dirData);
	free(buf);
	if(bytes != contDir->size)
		r = r == Result::ok ? Result::bug : r;
	return r;

}


//TODO: mark deleted treeCacheNodes as dirty
Result Paffs::removeInodeFromDir(Inode* contDir, Inode* elem){
	if(contDir == NULL){
		lasterr = Result::bug;
		return Result::bug;
	}

	char* dirData = (char*) malloc(contDir->size);
	unsigned int bytes = 0;
	Result r;
	if(contDir->reservedSize > 0){		//if Directory is not empty
		r = readInodeData(contDir, 0, contDir->size, &bytes, dirData, &device);
		if(r != Result::ok || bytes != contDir->size){
			lasterr = r;
			free(dirData);
			return r;
		}
	}else{
		free(dirData);
		return Result::nf;	//did not find directory entry, because dir is empty
	}


	DirEntryCount *entries = (DirEntryCount*) &dirData[0];
	FileSize pointer = sizeof(DirEntryCount);
	while(pointer < contDir->size){
		DirEntryLength entryl = (DirEntryLength) dirData[pointer];
		if(memcmp(&dirData[pointer + sizeof(DirEntryLength)], &(elem->no), sizeof(InodeNo)) == 0){
			//Found
			unsigned int newSize = contDir->size - entryl;
			unsigned int restByte = newSize - pointer;

			if((r = deleteInodeData(contDir, &device, newSize)) != Result::ok)
				return r;

			if(restByte > 0 && restByte < 4)	//should either be 0 (no entries left) or bigger than 4 (minimum size for one entry)
				PAFFS_DBG(PAFFS_TRACE_BUG, "Something is fishy! (%d)", restByte);

			if(newSize == 0)
				return Result::ok;

			(*entries)--;
			memcpy(&dirData[pointer], &dirData[pointer + entryl], restByte);

			unsigned int bw = 0;
			r = writeInodeData(contDir, 0, newSize, &bw, dirData, &device);
			free(dirData);
			return r;
		}
		pointer += entryl;
	}
	free(dirData);
	return Result::nf;
}

Result Paffs::mkDir(const char* fullPath, Permission mask){
	unsigned int lastSlash = 0;

	Inode parDir;
	Result res = getParentDir(fullPath, &parDir, &lastSlash);
	if(res != Result::ok)
		return res;

	Inode newDir;
	Result r = createDirInode(&newDir, mask);
	if(r != Result::ok)
		return r;
	r = insertInode(&device, &newDir);
	if(r != Result::ok)
		return r;

	return insertInodeInDir(&fullPath[lastSlash], &parDir, &newDir);
}

Dir* Paffs::openDir(const char* path){
	if(path[0] == 0){
		lasterr = Result::einval;
		return NULL;
	}

	Inode dirPinode;
	Result r = getInodeOfElem(&dirPinode, path);
	if(r != Result::ok){
		if(r != Result::nf){
			PAFFS_DBG(PAFFS_TRACE_BUG, "Result::bug? '%s'", err_msg(r));
		}
		lasterr = r;
		return NULL;
	}

	char* dirData = (char*) malloc(dirPinode.size);
	unsigned int br = 0;
	if(dirPinode.reservedSize > 0){
		r = readInodeData(&dirPinode, 0, dirPinode.size, &br, dirData, &device);
		if(r != Result::ok || br != dirPinode.size){
			lasterr = r;
			return NULL;
		}
	}else{
		memset(dirData, 0, dirPinode.size);
	}

	Dir* dir = (Dir*) malloc(sizeof(Dir));
	dir->self = (Dirent*) malloc(sizeof(Dirent));
	dir->self->name = (char*) "not_impl.";
	dir->self->node = (Inode*) malloc(sizeof(Inode));
	*dir->self->node = dirPinode;
	dir->self->parent = NULL;
	dir->no_entrys = dirData[0];
	dir->childs = (Dirent**) malloc(dir->no_entrys * sizeof(Dirent*));
	dir->pos = 0;

	unsigned int p = sizeof(DirEntryCount);
	unsigned int entry;
	for(entry = 0; p < dirPinode.size; entry++){

		dir->childs[entry] = (Dirent*) malloc (sizeof(Dirent));
		memset(dir->childs[entry], 0, sizeof(Dirent));
		DirEntryLength direntryl = dirData[p];
		unsigned int dirnamel = direntryl - sizeof(DirEntryLength) - sizeof(InodeNo);
		if(dirnamel > 1 << sizeof(DirEntryLength) * 8){
			//We have an error while reading
			PAFFS_DBG(PAFFS_TRACE_BUG, "Dirname length was bigger than possible (%u)!", dirnamel);
			for(unsigned int i = 0; i <= entry; i++)
				free(dir->childs[i]);
			free(dir->childs);
			free(dirData);
			free(dir->self);
			free(dir->self->node);
			free(dir);
			lasterr = Result::bug;
			return NULL;
		}
		p += sizeof(DirEntryLength);
		memcpy(&dir->childs[entry]->no, &dirData[p], sizeof(InodeNo));
		dir->childs[entry]->node = NULL;
		p += sizeof(InodeNo);
		dir->childs[entry]->name = (char*) malloc(dirnamel+2);    //+2 weil 1. Nullbyte und 2. Vielleicht ein Zeichen '/' dazukommt
		memcpy(dir->childs[entry]->name, &dirData[p], dirnamel);
		dir->childs[entry]->name[dirnamel] = 0;
		p += dirnamel;
	}

	free(dirData);

	if(entry != dir->no_entrys){
		PAFFS_DBG(PAFFS_TRACE_BUG, "Directory stated it had %u entries, but has actually %u!", dir->no_entrys, entry);
		lasterr = Result::bug;
		return NULL;
	}

	return dir;
}

Result Paffs::closeDir(Dir* dir){
	if(dir->childs == NULL)
		return Result::einval;
	for(int i = 0; i < dir->no_entrys; i++){
		free(dir->childs[i]->name);
		if(dir->childs[i]->node != NULL)
			free(dir->childs[i]->node);
		free(dir->childs[i]);
	}
	free(dir->childs);
	free(dir->self->node);
	free(dir->self);
	free(dir);
	return Result::ok;
}

/**
 * TODO: What happens if dir is changed after opendir?
 */
Dirent* Paffs::readDir(Dir* dir){
	if(dir->no_entrys == 0)
		return NULL;

	if(dir->pos == dir->no_entrys){
		return NULL;
	}

	if(dir->childs == NULL){
		PAFFS_DBG(PAFFS_TRACE_ERROR, "READ DIR on dir with NULL dirents");
		lasterr = Result::bug;
		return NULL;
	}

	if(dir->pos > dir->no_entrys){
		PAFFS_DBG(PAFFS_TRACE_ERROR, "READ DIR on dir that points further than its contents");
		lasterr = Result::bug;
		return NULL;
	}

	if(dir->childs[dir->pos] == NULL){
		PAFFS_DBG(PAFFS_TRACE_ERROR, "READ DIR on dir with NULL Dirent no. %d", dir->pos);
		lasterr = Result::bug;
		return NULL;
	}

	if(dir->childs[dir->pos]->node != NULL){
		return dir->childs[dir->pos++];
	}
	Inode item;
	Result r = getInode(&device, dir->childs[dir->pos]->no, &item);
	if(r != Result::ok){
	   lasterr = Result::bug;
	   return NULL;
	}
	if((item.perm & R) == 0){
		lasterr = Result::noperm;
		return NULL;
	}


	dir->childs[dir->pos]->node = (Inode*) malloc(sizeof(Inode));
	*dir->childs[dir->pos]->node = item;
	if(dir->childs[dir->pos]->node->type == InodeType::dir){
		int namel = strlen(dir->childs[dir->pos]->name);
		dir->childs[dir->pos]->name[namel] = '/';
		dir->childs[dir->pos]->name[namel+1] = 0;
	}
	return dir->childs[dir->pos++];
}

void Paffs::rewindDir(Dir* dir){
    dir->pos = 0;
}

Result Paffs::createFile(Inode* outFile, const char* fullPath, Permission mask){
	unsigned int lastSlash = 0;

	Inode parDir;
	Result res = getParentDir(fullPath, &parDir, &lastSlash);
	if(res != Result::ok)
		return res;

	if(createFilInode(outFile, mask) != Result::ok){
		return Result::bug;
	}
	res = insertInode(&device, outFile);
	if(res != Result::ok)
		return res;

	return insertInodeInDir(&fullPath[lastSlash], &parDir, outFile);
}

Obj* Paffs::open(const char* path, Fileopenmask mask){
	Inode file;
	Result r = getInodeOfElem(&file, path);
	if(r == Result::nf){
		//create new file
		if(mask & FC){
			r = createFile(&file, path, mask);
			if(r != Result::ok){
				lasterr = r;
				return NULL;
			}
		}else{
			lasterr = Result::exists;	//not exsist
			return NULL;
		}
	}else if(r != Result::ok){
		lasterr = r;
		return NULL;
	}

	if(file.type == InodeType::lnk){
		//LINKS are not supported yet
		lasterr = Result::nimpl;
		return NULL;
	}

	if(file.type == InodeType::dir){
		//tried to open directory as file
		lasterr = Result::einval;
		return NULL;
	}

	if((file.perm & (mask & PERM_MASK)) != (mask & PERM_MASK)){
		lasterr = Result::noperm;
		return NULL;
	}

	Obj* obj = (Obj*) malloc(sizeof(Obj));
	obj->dirent = (Dirent*) malloc(sizeof(Dirent));
	obj->dirent->name = (char*) malloc(strlen(path));
	obj->dirent->node = (Inode*) malloc(sizeof(Inode));
	*obj->dirent->node = file;
	obj->dirent->parent = NULL;		//TODO: Sollte aus cache gesucht werden, erstellt in "getInodeOfElem(path))" ?

	memcpy((void*)obj->dirent->name, path, strlen(path));

	if(mask & FA){
		obj->fp = file.size;
	}else{
		obj->fp = 0;
	}

	obj->rdnly = ! (mask & FW);

	return obj;
}

Result Paffs::close(Obj* obj){
	if(obj == NULL)
		return Result::einval;
	flush(obj);
	free(obj->dirent->node);
	free((void*)obj->dirent->name);
	free(obj->dirent);
	free(obj);

	return Result::ok;
}

Result Paffs::touch(const char* path){
	Inode file;
	Result r = getInodeOfElem(&file, path);
	if(r == Result::nf){
		//create new file
		Result r2 = createFile(&file, path, R | W);
		if(r2 != Result::ok){
			return r2;
		}
		return Result::ok;
	}else{
		if(r != Result::ok)
			return r;
		file.mod = time(0);
		return updateExistingInode(&device, &file);
	}

}


Result Paffs::getObjInfo(const char *fullPath, ObjInfo* nfo){
	Inode object;
	Result r;
	if((r = getInodeOfElem(&object, fullPath)) != Result::ok){
		return lasterr = r;
	}
	nfo->created = object.crea;
	nfo->modified = object.mod;
	nfo->perm = object.perm;
	nfo->size = object.size;
	nfo->isDir = object.type == InodeType::dir;
	return Result::ok;
}

Result Paffs::read(Obj* obj, char* buf, unsigned int bytes_to_read, unsigned int *bytes_read){
	if(obj == NULL)
		return lasterr = Result::einval;

	if(obj->dirent->node->type == InodeType::dir){
		return lasterr = Result::einval;
	}
	if(obj->dirent->node->type == InodeType::lnk){
		return lasterr = Result::nimpl;
	}
	if((obj->dirent->node->perm & R) == 0)
		return Result::noperm;

	if(obj->dirent->node->size == 0){
		*bytes_read = 0;
		return Result::ok;
	}

	Result r = readInodeData(obj->dirent->node, obj->fp, bytes_to_read, bytes_read, buf, &device);
	if(r != Result::ok){
		return r;
	}
	//TODO: Check if actually read that much!
	*bytes_read = bytes_to_read;
	obj->fp += *bytes_read;
	return Result::ok;
}

Result Paffs::write(Obj* obj, const char* buf, unsigned int bytes_to_write, unsigned int *bytes_written){
	if(obj == NULL)
		return Result::einval;

	if(obj->dirent->node->type == InodeType::dir){
		return Result::einval;
	}
	if(obj->dirent->node->type == InodeType::lnk){
		return Result::nimpl;
	}
	if((obj->dirent->node->perm & W) == 0)
		return Result::noperm;

	Result r = writeInodeData(obj->dirent->node, obj->fp, bytes_to_write, bytes_written, buf, &device);
	if(r != Result::ok){
		return r;
	}

	obj->dirent->node->mod = time(0);

	obj->fp += *bytes_written;
	if(obj->fp > obj->dirent->node->size){
		//size was increased
		if(obj->dirent->node->reservedSize < obj->fp){
			PAFFS_DBG(PAFFS_TRACE_BUG, "Reserved size is smaller than actual size?!");
			return Result::bug;
		}
	}
	return updateExistingInode(&device, obj->dirent->node);
}

Result Paffs::seek(Obj* obj, int m, Seekmode mode){
	switch(mode){
	case Seekmode::set :
		if(m < 0)
			return lasterr = Result::einval;
		obj->fp = m;
		break;
	case Seekmode::end :
		obj->fp = obj->dirent->node->size + m;
		break;
	case Seekmode::cur :
		obj->fp += m;
		break;
	}

	return Result::ok;
}


Result Paffs::flush(Obj* obj){
	return Result::ok;
}

Result Paffs::chmod(const char* path, Permission perm){
	Inode object;
	Result r;
	if((r = getInodeOfElem(&object, path)) != Result::ok){
		return r;
	}
	object.perm = perm;
	return updateExistingInode(&device, &object);
}
Result Paffs::remove(const char* path){
	Inode object;
	Result r;
	if((r = getInodeOfElem(&object, path)) != Result::ok)
		return r;

	if(!(object.perm & W))
		return Result::noperm;

	if(object.type == InodeType::dir)
		if(object.size > sizeof(DirEntryCount))
			return Result::dirnotempty;

	if((r = deleteInodeData(&object, &device, 0)) != Result::ok)
		return r;

	Inode parentDir;
	unsigned int lastSlash = 0;
	if((r = getParentDir(path, &parentDir, &lastSlash)) != Result::ok)
		return r;

	if((r = removeInodeFromDir(&parentDir, &object)) != Result::ok)
		return r;
	return deleteInode(&device, object.no);
}


//ONLY FOR DEBUG
Dev* Paffs::getDevice(){
	return &device;
}

}
