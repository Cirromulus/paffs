/*
 * device.hpp
 *
 *  Created on: 15 Feb 2017
 *      Author: Pascal Pieper
 */

#include "device.hpp"
#include "paffs_trace.hpp"
#include "driver/driver.hpp"

#include <iostream>

namespace paffs{

Device::Device(Driver* driver) : driver(driver),
		tree(Btree(this)), sumCache(SummaryCache(this)),
		areaMgmt(this), dataIO(this), superblock(this){
	areaMap = 0;
	param = 0;
	lasterr = Result::ok;
};

Device::~Device(){
	if(areaMap != nullptr){
		std::cerr << "Destroyed Device-Object without unmouning! "
				"This will most likely destroy "
				"the filesystem on flash." << std::endl;
		Result r = destroyDevice();
		if(r != Result::ok){
			PAFFS_DBG(PAFFS_TRACE_ERROR, "Could not (gracefully) destroy Device!");
		}
	}
	delete driver;
}


Result Device::format(){
	if(areaMap != 0)
		return Result::alrMounted;
	Result r = initializeDevice();
	if(r != Result::ok)
		return r;

	unsigned char had_areaType = 0;		//Efficiency hack, bc there are less than 2⁸ area types
	for(unsigned int area = 0; area < param->areasNo; area++){
		areaMap[area].status = AreaStatus::empty;
		areaMap[area].erasecount = 0;
		areaMap[area].position = area;

		if(!(had_areaType & 1 << AreaType::superblock)){
			activeArea[AreaType::superblock] = area;
			areaMap[area].type = AreaType::superblock;
			areaMgmt.initArea(area);
			had_areaType |= 1 << AreaType::superblock;
			continue;
		}
		if(!(had_areaType & 1 << AreaType::journal)){
			activeArea[AreaType::journal] = area;
			areaMap[area].type = AreaType::journal;
			areaMgmt.initArea(area);
			had_areaType |= 1 << AreaType::journal;
			continue;
		}

		if(!(had_areaType & 1 << AreaType::garbageBuffer)){
			activeArea[AreaType::garbageBuffer] = area;
			areaMap[area].type = AreaType::garbageBuffer;
			areaMgmt.initArea(area);
			had_areaType |= 1 << AreaType::garbageBuffer;
			continue;
		}

		areaMap[area].type = AreaType::unset;

	}

	r = tree.start_new_tree();
	if(r != Result::ok)
		return r;

	Inode rootDir = {0};
	r = createDirInode(&rootDir, R | W | X);
	if(r != Result::ok){
		destroyDevice();
		return r;
	}
	r = tree.insertInode(&rootDir);
	if(r != Result::ok){
		destroyDevice();
		return r;
	}
	r = tree.commitCache();
	if(r != Result::ok){
		destroyDevice();
		return r;
	}
	r = sumCache.commitAreaSummaries();
	if(r != Result::ok){
		destroyDevice();
		return r;
	}
	destroyDevice();
	return Result::ok;
}

Result Device::mnt(){
	if(areaMap != 0)
		return Result::alrMounted;
	Result r = initializeDevice();
	if(r != Result::ok)
		return r;

	r = sumCache.loadAreaSummaries();
	if(r == Result::nf){
		PAFFS_DBG(PAFFS_TRACE_ERROR, "Tried mounting a device with an empty superblock!");
		destroyDevice();
		return r;
	}


	for(AreaPos i = 0; i < param->areasNo; i++){
		if(areaMap[i].type == AreaType::garbageBuffer){
			activeArea[AreaType::garbageBuffer] = i;
		}
		//Superblock do not need an active Area,
		//data and index active areas are extracted by areaSummaryCache
	}

	return r;
}
Result Device::unmnt(){
	if(areaMap == 0)
		return Result::notMounted;
	if(traceMask && PAFFS_TRACE_AREA){
		printf("Info: \n");
		for(unsigned int i = 0; i < param->areasNo; i++){
			printf("\tArea %d on %u as %10s from page %4d %s\n"
					, i, areaMap[i].position, area_names[areaMap[i].type]
					, areaMap[i].position*param->blocksPerArea*param->pagesPerBlock
					, areaStatusNames[areaMap[i].status]);
		}
	}

	Result r = tree.commitCache();
	if(r != Result::ok)
		return r;

	r = sumCache.commitAreaSummaries();
	if(r != Result::ok)
		return r;

	destroyDevice();

	//just for cleanup & tests
	tree.wipeCache();

	return Result::ok;
}

Result Device::createInode(Inode* outInode, Permission mask){
	memset(outInode, 0, sizeof(Inode));

	//FIXME: is this the best way to find a new number?
	Result r = tree.findFirstFreeNo(&outInode->no);
	if(r != Result::ok)
		return r;

	outInode->perm = (mask & permMask);
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
Result Device::createDirInode(Inode* outInode, Permission mask){
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
Result Device::createFilInode(Inode* outInode, Permission mask){
	if(createInode(outInode, mask) != Result::ok){
		return Result::bug;
	}
	outInode->type = InodeType::file;
	return Result::ok;
}

void Device::destroyInode(Inode* node){
	dataIO.deleteInodeData(node, 0);
	free(node);
}

Result Device::getParentDir(const char* fullPath, Inode* parDir, unsigned int *lastSlash){
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
Result Device::getInodeInDir( Inode* outInode, Inode* folder, const char* name){
	if(folder->type != InodeType::dir){
		return Result::bug;
	}

	if(folder->size <= sizeof(DirEntryCount)){
		//Just contains a zero for "No entrys"
		return Result::nf;
	}

	char* buf = (char*) malloc(folder->size);
	unsigned int bytes_read = 0;
	Result r = dataIO.readInodeData(folder, 0, folder->size, &bytes_read, buf);
	if(r != Result::ok || bytes_read != folder->size){
		free(buf);
		return r == Result::ok ? Result::bug : r;
	}

	unsigned int p = sizeof(DirEntryCount);		//skip directory entry count
	while(p < folder->size){
			DirEntryLength direntryl = buf[p];
			if(direntryl < sizeof(DirEntryLength) + sizeof(InodeNo)){
				PAFFS_DBG(PAFFS_TRACE_BUG, "Directory size of Folder %u is unplausible! (was: %d, should: >%d)", folder->no, direntryl, sizeof(DirEntryLength) + sizeof(InodeNo));
				free(buf);
				return Result::bug;
			}
			if(direntryl > folder->size){
				PAFFS_DBG(PAFFS_TRACE_BUG, "BUG: direntry length of Folder %u not plausible (was: %d, should: >%d)!", folder->no, direntryl, folder->size);
				free(buf);
				return Result::bug;
			}
			unsigned int dirnamel = direntryl - sizeof(DirEntryLength) - sizeof(InodeNo);
			if(dirnamel > folder->size){
				PAFFS_DBG(PAFFS_TRACE_BUG, "BUG: dirname length of Inode %u not plausible (was: %d, should: >%d)!", folder->no, folder->size, p + dirnamel);
				free(buf);
				return Result::bug;
			}
			p += sizeof(DirEntryLength);
			InodeNo tmp_no;
			memcpy(&tmp_no, &buf[p], sizeof(InodeNo));
			p += sizeof(InodeNo);
			char* tmpname = (char*) malloc((dirnamel+1) * sizeof(char));
			memcpy(tmpname, &buf[p], dirnamel);
			tmpname[dirnamel] = 0;
			p += dirnamel;
			if(strcmp(name, tmpname) == 0){
				//Eintrag gefunden
				if(tree.getInode(tmp_no, outInode) != Result::ok){
					PAFFS_DBG(PAFFS_TRACE_BUG, "BUG: Found Element '%s' in dir, but did not find its Inode (No. %d) in Index!", tmpname, tmp_no);
					free(tmpname);
					free(buf);
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

Result Device::getInodeOfElem(Inode* outInode, const char* fullPath){
	Inode root;
	if(tree.getInode(0, &root) != Result::ok){
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

	while(fnP != nullptr){
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
		fnP = strtok(nullptr, delimiter);
	}

	free(fullPathC);
	return Result::ok;
}


Result Device::insertInodeInDir(const char* name, Inode* contDir, Inode* newElem){
	if(contDir == nullptr){
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
		r = dataIO.readInodeData(contDir, 0, contDir->size, &bytes, dirData);
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

	r = dataIO.writeInodeData(contDir, 0, contDir->size + direntryl, &bytes, dirData);
	free(dirData);
	free(buf);
	if(bytes != contDir->size)
		r = r == Result::ok ? Result::bug : r;
	return r;

}


//TODO: mark deleted treeCacheNodes as dirty
Result Device::removeInodeFromDir(Inode* contDir, Inode* elem){
	if(contDir == nullptr){
		lasterr = Result::bug;
		return Result::bug;
	}

	char* dirData = (char*) malloc(contDir->size);
	unsigned int bytes = 0;
	Result r;
	if(contDir->reservedSize > 0){		//if Directory is not empty
		r = dataIO.readInodeData(contDir, 0, contDir->size, &bytes, dirData);
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

			if((r = dataIO.deleteInodeData(contDir, newSize)) != Result::ok)
				return r;

			if(restByte > 0 && restByte < 4)	//should either be 0 (no entries left) or bigger than 4 (minimum size for one entry)
				PAFFS_DBG(PAFFS_TRACE_BUG, "Something is fishy! (%d)", restByte);

			if(newSize == 0)
				return Result::ok;

			(*entries)--;
			memcpy(&dirData[pointer], &dirData[pointer + entryl], restByte);

			unsigned int bw = 0;
			r = dataIO.writeInodeData(contDir, 0, newSize, &bw, dirData);
			free(dirData);
			return r;
		}
		pointer += entryl;
	}
	free(dirData);
	return Result::nf;
}

Result Device::mkDir(const char* fullPath, Permission mask){
	if(areaMap == 0)
		return Result::notMounted;
	unsigned int lastSlash = 0;

	Inode parDir;
	Result res = getParentDir(fullPath, &parDir, &lastSlash);
	if(res != Result::ok)
		return res;

	Inode newDir;
	Result r = createDirInode(&newDir, mask);
	if(r != Result::ok)
		return r;
	r = tree.insertInode(&newDir);
	if(r != Result::ok)
		return r;

	return insertInodeInDir(&fullPath[lastSlash], &parDir, &newDir);
}

Dir* Device::openDir(const char* path){
	if(areaMap == 0){
		lasterr = Result::notMounted;
		return nullptr;
		}
	if(path[0] == 0){
		lasterr = Result::einval;
		return nullptr;
	}

	Inode dirPinode;
	Result r = getInodeOfElem(&dirPinode, path);
	if(r != Result::ok){
		if(r != Result::nf){
			PAFFS_DBG(PAFFS_TRACE_BUG, "Result::bug? '%s'", err_msg(r));
		}
		lasterr = r;
		return nullptr;
	}

	char* dirData = (char*) malloc(dirPinode.size);
	unsigned int br = 0;
	if(dirPinode.reservedSize > 0){
		r = dataIO.readInodeData(&dirPinode, 0, dirPinode.size, &br, dirData);
		if(r != Result::ok || br != dirPinode.size){
			lasterr = r;
			return nullptr;
		}
	}else{
		memset(dirData, 0, dirPinode.size);
	}

	Dir* dir = (Dir*) malloc(sizeof(Dir));
	dir->self = (Dirent*) malloc(sizeof(Dirent));
	dir->self->name = (char*) "not_impl.";
	dir->self->node = (Inode*) malloc(sizeof(Inode));
	*dir->self->node = dirPinode;
	dir->self->parent = nullptr;	//no caching, so we pobably dont have the parent
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
			return nullptr;
		}
		p += sizeof(DirEntryLength);
		memcpy(&dir->childs[entry]->no, &dirData[p], sizeof(InodeNo));
		dir->childs[entry]->node = nullptr;
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
		return nullptr;
	}

	return dir;
}

Result Device::closeDir(Dir* dir){
	if(areaMap == 0)
		return Result::notMounted;
	if(dir->childs == nullptr)
		return Result::einval;
	for(int i = 0; i < dir->no_entrys; i++){
		free(dir->childs[i]->name);
		if(dir->childs[i]->node != nullptr)
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
Dirent* Device::readDir(Dir* dir){
	if(areaMap == 0){
		lasterr = Result::notMounted;
		return nullptr;
	}
	if(dir->no_entrys == 0)
		return nullptr;

	if(dir->pos == dir->no_entrys){
		return nullptr;
	}

	if(dir->childs == nullptr){
		PAFFS_DBG(PAFFS_TRACE_ERROR, "READ DIR on dir with nullptr dirents");
		lasterr = Result::bug;
		return nullptr;
	}

	if(dir->pos > dir->no_entrys){
		PAFFS_DBG(PAFFS_TRACE_ERROR, "READ DIR on dir that points further than its contents");
		lasterr = Result::bug;
		return nullptr;
	}

	if(dir->childs[dir->pos] == nullptr){
		PAFFS_DBG(PAFFS_TRACE_ERROR, "READ DIR on dir with nullptr Dirent no. %d", dir->pos);
		lasterr = Result::bug;
		return nullptr;
	}

	if(dir->childs[dir->pos]->node != nullptr){
		return dir->childs[dir->pos++];
	}
	Inode item;
	Result r = tree.getInode(dir->childs[dir->pos]->no, &item);
	if(r != Result::ok){
	   lasterr = Result::bug;
	   return nullptr;
	}
	if((item.perm & R) == 0){
		lasterr = Result::noperm;
		return nullptr;
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

void Device::rewindDir(Dir* dir){
    dir->pos = 0;
}

Result Device::createFile(Inode* outFile, const char* fullPath, Permission mask){
	if(areaMap == 0)
		return Result::notMounted;
	unsigned int lastSlash = 0;

	Inode parDir;
	Result res = getParentDir(fullPath, &parDir, &lastSlash);
	if(res != Result::ok)
		return res;

	if(createFilInode(outFile, mask) != Result::ok){
		return Result::bug;
	}
	res = tree.insertInode(outFile);
	if(res != Result::ok)
		return res;

	return insertInodeInDir(&fullPath[lastSlash], &parDir, outFile);
}

Obj* Device::open(const char* path, Fileopenmask mask){
	if(areaMap == 0){
		lasterr = Result::notMounted;
		return nullptr;
	}
	Inode file;
	Result r = getInodeOfElem(&file, path);
	if(r == Result::nf){
		//create new file
		if(mask & FC){
			//use standard mask
			//FIXME: Use standard mask or the mask provided?
			r = createFile(&file, path, R | W);
			if(r != Result::ok){
				lasterr = r;
				return nullptr;
			}
		}else{
			//does not exist, no filecreation bit is given
			lasterr = Result::nf;
			return nullptr;
		}
	}else if(r != Result::ok){
		lasterr = r;
		return nullptr;
	}

	if(file.type == InodeType::lnk){
		//LINKS are not supported yet
		lasterr = Result::nimpl;
		return nullptr;
	}

	if(file.type == InodeType::dir){
		//tried to open directory as file
		lasterr = Result::einval;
		return nullptr;
	}

	if((file.perm | (mask & permMask)) != (file.perm & permMask)){
		lasterr = Result::noperm;
		return nullptr;
	}

	Obj* obj = (Obj*) malloc(sizeof(Obj));
	obj->dirent = (Dirent*) malloc(sizeof(Dirent));
	obj->dirent->name = (char*) malloc(strlen(path));
	obj->dirent->node = (Inode*) malloc(sizeof(Inode));
	*obj->dirent->node = file;
	obj->dirent->parent = nullptr;		//TODO: Sollte aus cache gesucht werden, erstellt in "getInodeOfElem(path))" ?

	memcpy((void*)obj->dirent->name, path, strlen(path));

	if(mask & FA){
		obj->fp = file.size;
	}else{
		obj->fp = 0;
	}

	obj->rdnly = ! (mask & FW);

	return obj;
}

Result Device::close(Obj* obj){
	if(areaMap == 0)
		return Result::notMounted;
	if(obj == nullptr)
		return Result::einval;
	flush(obj);
	free(obj->dirent->node);
	free((void*)obj->dirent->name);
	free(obj->dirent);
	free(obj);

	return Result::ok;
}

Result Device::touch(const char* path){
	if(areaMap == 0)
		return Result::notMounted;
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
		return tree.updateExistingInode(&file);
	}

}


Result Device::getObjInfo(const char *fullPath, ObjInfo* nfo){
	if(areaMap == 0)
		return Result::notMounted;
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

Result Device::read(Obj* obj, char* buf, unsigned int bytes_to_read, unsigned int *bytes_read){
	if(areaMap == 0)
		return Result::notMounted;
	if(obj == nullptr)
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

	Result r = dataIO.readInodeData(obj->dirent->node, obj->fp, bytes_to_read, bytes_read, buf);
	if(r != Result::ok){
		return r;
	}
	//TODO: Check if actually read that much!
	*bytes_read = bytes_to_read;
	obj->fp += *bytes_read;
	return Result::ok;
}

Result Device::write(Obj* obj, const char* buf, unsigned int bytes_to_write, unsigned int *bytes_written){
	*bytes_written = 0;
	if(areaMap == 0)
		return Result::notMounted;
	if(obj == nullptr)
		return Result::einval;

	if(obj->dirent->node->type == InodeType::dir){
		return Result::einval;
	}
	if(obj->dirent->node->type == InodeType::lnk){
		return Result::nimpl;
	}
	if((obj->dirent->node->perm & W) == 0)
		return Result::noperm;

	Result r = dataIO.writeInodeData(obj->dirent->node, obj->fp, bytes_to_write, bytes_written, buf);
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
	return tree.updateExistingInode(obj->dirent->node);
}

Result Device::seek(Obj* obj, int m, Seekmode mode){
	if(areaMap == 0)
		return Result::notMounted;
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

	if(obj->fp > obj->dirent->node->size)
		obj->dirent->node->size = obj->fp;

	return Result::ok;
}


Result Device::flush(Obj* obj){
	if(areaMap == 0)
		return Result::notMounted;
	return Result::ok;
}

Result Device::chmod(const char* path, Permission perm){
	if(areaMap == 0)
		return Result::notMounted;
	Inode object;
	Result r;
	if((r = getInodeOfElem(&object, path)) != Result::ok){
		return r;
	}
	object.perm = perm;
	return tree.updateExistingInode(&object);
}
Result Device::remove(const char* path){
	if(areaMap == 0)
		return Result::notMounted;
	Inode object;
	Result r;
	if((r = getInodeOfElem(&object, path)) != Result::ok)
		return r;

	if(!(object.perm & W))
		return Result::noperm;

	if(object.type == InodeType::dir)
		if(object.size > sizeof(DirEntryCount))
			return Result::dirnotempty;

	if((r = dataIO.deleteInodeData(&object, 0)) != Result::ok)
		return r;

	Inode parentDir;
	unsigned int lastSlash = 0;
	if((r = getParentDir(path, &parentDir, &lastSlash)) != Result::ok)
		return r;

	if((r = removeInodeFromDir(&parentDir, &object)) != Result::ok)
		return r;
	return tree.deleteInode(object.no);
}

Result Device::initializeDevice(){
	if(areaMap != 0)
		return Result::alrMounted;
	param = &driver->param;
	param->areasNo = param->blocks / 2;	//For now: 16 b -> 8 Areas
	param->blocksPerArea = param->blocks / param->areasNo;
	param->dataBytesPerPage = param->totalBytesPerPage - param->oobBytesPerPage;
	param->totalPagesPerArea = param->pagesPerBlock * param->blocksPerArea;
	unsigned int needed_pages_for_AS = 1;	//Todo: actually calculate
	param->dataPagesPerArea = param->totalPagesPerArea - needed_pages_for_AS;
	areaMap = new Area[param->areasNo];
	memset(areaMap, 0, sizeof(Area) * param->areasNo);

	activeArea[AreaType::superblock] = 0;
	activeArea[AreaType::index] = 0;
	activeArea[AreaType::journal] = 0;
	activeArea[AreaType::data] = 0;

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

Result Device::destroyDevice(){
	if(areaMap == 0)
		return Result::notMounted;
	delete[] areaMap;
	areaMap = 0;
	memset(activeArea, 0, sizeof(AreaPos)*AreaType::no);
	return Result::ok;
}


};
