/*
 * device.hpp
 *
 *  Created on: 15 Feb 2017
 *      Author: Pascal Pieper
 */

#include "device.hpp"
#include "paffs_trace.hpp"
#include "driver/driver.hpp"
#include <inttypes.h>

namespace paffs{

outpost::rtos::SystemClock systemClock;

Device::Device(Driver* mdriver) : driver(mdriver),
		usedAreas(0), lasterr(Result::ok), mounted(false), readOnly(false),
		tree(Btree(this)), sumCache(SummaryCache(this)),
		areaMgmt(this), dataIO(this), superblock(this){
};

Device::~Device(){
	if(driver == nullptr)
		return;
	if(mounted){
		fprintf(stderr, "Destroyed Device-Object without unmouning! "
				"This will most likely destroy "
				"the filesystem on flash.\n");
		Result r = destroyDevice();
		if(r != Result::ok){
			PAFFS_DBG(PAFFS_TRACE_ERROR, "Could not (gracefully) destroy Device!");
		}
	}
	delete driver;
}


Result Device::format(bool complete){
	if(driver == nullptr){
		PAFFS_DBG(PAFFS_TRACE_ERROR, "Device has no driver set!");
		return Result::fail;
	}
	if(mounted)
		return Result::alrMounted;

	Result r = initializeDevice();
	if(r != Result::ok)
		return r;

	if(complete)
		PAFFS_DBG_S(PAFFS_TRACE_VERBOSE, "Deleting all areas.\n");

	unsigned char hadAreaType = 0;
	unsigned char hadSuperblocks = 0;

	for(unsigned int area = 0; area < areasNo; area++){
		areaMap[area].status = AreaStatus::empty;
		areaMap[area].erasecount = 0;
		areaMap[area].position = area;

		if(hadAreaType &
				(1 << AreaType::superblock |
				1 << AreaType::garbageBuffer) ||
				complete){
			for(unsigned int p = 0; p < blocksPerArea; p++){
				r = driver->eraseBlock(p + area);
				if(r != Result::ok){
					PAFFS_DBG_S(PAFFS_TRACE_BAD_BLOCKS,
							"Found bad block %u during formatting", p + area);
					areaMap[area].type = AreaType::retired;
					break;
				}
			}
			areaMap[area].erasecount++;

			if(areaMap[area].type == AreaType::retired){
				continue;
			}
		}

		if(!(hadAreaType & 1 << AreaType::superblock)){
			activeArea[AreaType::superblock] = area;
			areaMap[area].type = AreaType::superblock;
			areaMgmt.initArea(area);
			if(++hadSuperblocks == superChainElems)
				hadAreaType |= 1 << AreaType::superblock;
			continue;
		}

		if(!(hadAreaType & 1 << AreaType::garbageBuffer)){
			activeArea[AreaType::garbageBuffer] = area;
			areaMap[area].type = AreaType::garbageBuffer;
			areaMgmt.initArea(area);
			hadAreaType |= 1 << AreaType::garbageBuffer;
			continue;
		}

		areaMap[area].type = AreaType::unset;
	}

	r = tree.start_new_tree();
	if(r != Result::ok)
		return r;

	Inode rootDir;
	memset(&rootDir, 0, sizeof(Inode));

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
	r = sumCache.commitAreaSummaries(true);
	if(r != Result::ok){
		destroyDevice();
		return r;
	}
	destroyDevice();
	return Result::ok;
}

Result Device::mnt(bool readOnlyMode){
	readOnly = readOnlyMode;
	if(driver == nullptr){
		PAFFS_DBG(PAFFS_TRACE_ERROR, "Device has no driver set!");
		return Result::fail;
	}

	PAFFS_DBG_S(PAFFS_TRACE_VERBOSE, "mount with valid driver");

	if(mounted)
		return Result::alrMounted;

	PAFFS_DBG_S(PAFFS_TRACE_VERBOSE, "not yet mounted");


	Result r = initializeDevice();
	if(r != Result::ok)
		return r;
	PAFFS_DBG_S(PAFFS_TRACE_VERBOSE, "Inited Device");

	r = sumCache.loadAreaSummaries();
	if(r == Result::nf){
		PAFFS_DBG_S(PAFFS_TRACE_ERROR, "Tried mounting a device with an empty superblock!\n"
				"Maybe not formatted?");
		destroyDevice();
		return r;
	}
	if(r != Result::ok){
		PAFFS_DBG_S(PAFFS_TRACE_ERROR, "Could not load Area Summaries");
		return r;
	}

	PAFFS_DBG_S(PAFFS_TRACE_VERBOSE, "Area Summaries loaded");

	//FIXME: This is O(n), save aactive Area in SuperIndex
	for(AreaPos i = 0; i < areasNo; i++){
		if(areaMap[i].type == AreaType::garbageBuffer){
			activeArea[AreaType::garbageBuffer] = i;
		}
		//Superblock does not need an active Area,
		//data and index active areas are extracted by areaSummaryCache
	}
	mounted = true;
	PAFFS_DBG_S(PAFFS_TRACE_VERBOSE, "Mount sucessful");
	return r;
}
Result Device::unmnt(){
	if(driver == nullptr){
		PAFFS_DBG(PAFFS_TRACE_ERROR, "Device has no driver set!");
		return Result::fail;
	}
	if(!mounted)
		return Result::notMounted;
	if(traceMask & PAFFS_TRACE_AREA){
		printf("Info: \n\t%" PRIu32 " used Areas\n", usedAreas);
		for(unsigned int i = 0; i < areasNo; i++){
			printf("\tArea %03d on %03u as %10s from page %4d %s\n"
					, i, areaMap[i].position, areaNames[areaMap[i].type]
					, areaMap[i].position*blocksPerArea*pagesPerBlock
					, areaStatusNames[areaMap[i].status]);
			if(i > 128){
				printf("\n -- truncated 128-%u Areas.\n", areasNo);
				break;
			}
		}
	}

	Result r = tree.commitCache();
	if(r != Result::ok){
		PAFFS_DBG(PAFFS_TRACE_ERROR, "Could not commit Tree cache!");
		return r;
	}

	r = sumCache.commitAreaSummaries();
	if(r != Result::ok){
		PAFFS_DBG(PAFFS_TRACE_ERROR, "Could not commit Area Summaries!");
		return r;
	}

	destroyDevice();

	//just for cleanup & tests
	tree.wipeCache();
	mounted = false;
	return Result::ok;
}

Result Device::createInode(Inode* outInode, Permission mask){
	if(driver == nullptr){
		PAFFS_DBG(PAFFS_TRACE_ERROR, "Device has no driver set!");
		return Result::fail;
	}
	memset(outInode, 0, sizeof(Inode));

	//FIXME: is this the best way to find a new number?
	Result r = tree.findFirstFreeNo(&outInode->no);
	if(r != Result::ok)
		return r;

	outInode->perm = (mask & permMask);
	if(mask & W)
		outInode->perm |= R;
	outInode->size = 0;
	outInode->reservedPages = 0;
	outInode->crea = systemClock.now().convertTo<outpost::time::GpsTime>().timeSinceEpoch().milliseconds();
	outInode->mod = outInode->crea;
	return Result::ok;
}

/**
 * creates DirInode ONLY IN RAM
 */
Result Device::createDirInode(Inode* outInode, Permission mask){
	if(driver == nullptr){
		PAFFS_DBG(PAFFS_TRACE_ERROR, "Device has no driver set!");
		return Result::fail;
	}
	if(createInode(outInode, mask) != Result::ok)
		return Result::bug;
	outInode->type = InodeType::dir;
	outInode->size = sizeof(DirEntryCount);		//to hold directory-entry-count. even if it is not commited to flash
	outInode->reservedPages = 0;
	return Result::ok;
}

/**
 * creates FilInode ONLY IN RAM
 */
Result Device::createFilInode(Inode* outInode, Permission mask){
	if(driver == nullptr){
		PAFFS_DBG(PAFFS_TRACE_ERROR, "Device has no driver set!");
		return Result::fail;
	}
	if(createInode(outInode, mask) != Result::ok){
		return Result::bug;
	}
	outInode->type = InodeType::file;
	return Result::ok;
}

void Device::destroyInode(Inode* node){
	if(driver == nullptr){
		PAFFS_DBG(PAFFS_TRACE_ERROR, "Device has no driver set!");
		return;
	}
	dataIO.deleteInodeData(node, 0);
	delete node;
}

Result Device::getParentDir(const char* fullPath, Inode* parDir, unsigned int *lastSlash){
	if(driver == nullptr){
		PAFFS_DBG(PAFFS_TRACE_ERROR, "Device has no driver set!");
		return Result::fail;
	}
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


	char* pathC = new char[*lastSlash+1];
	memcpy(pathC, fullPath, *lastSlash);
	pathC[*lastSlash] = 0;

	Result r = getInodeOfElem(parDir, pathC);
	delete[] pathC;
	return r;
}

//Currently Linearer Aufwand
Result Device::getInodeInDir( Inode* outInode, Inode* folder, const char* name){
	if(driver == nullptr){
		PAFFS_DBG(PAFFS_TRACE_ERROR, "Device has no driver set!");
		return Result::fail;
	}

	if(folder->type != InodeType::dir){
		return Result::bug;
	}
	if(folder->size <= sizeof(DirEntryCount)){
		//Just contains a zero for "No entrys"
		return Result::nf;
	}

	char* buf = new char[folder->size];
	unsigned int bytes_read = 0;
	Result r = dataIO.readInodeData(folder, 0, folder->size, &bytes_read, buf);
	if(r != Result::ok || bytes_read != folder->size){
		delete[] buf;
		return r == Result::ok ? Result::bug : r;
	}

	unsigned int p = sizeof(DirEntryCount);		//skip directory entry count
	while(p < folder->size){
			DirEntryLength direntryl = buf[p];
			if(direntryl < sizeof(DirEntryLength) + sizeof(InodeNo)){
				PAFFS_DBG(PAFFS_TRACE_BUG, "Directory entry size of Folder %u is unplausible! (was: %d, should: >%lu)", folder->no, direntryl, sizeof(DirEntryLength) + sizeof(InodeNo));
				delete[] buf;
				return Result::bug;
			}
			if(direntryl > folder->size){
				PAFFS_DBG(PAFFS_TRACE_BUG, "BUG: direntry length of Folder %u not plausible (was: %d, should: >%d)!", folder->no, direntryl, folder->size);
				delete[] buf;
				return Result::bug;
			}
			unsigned int dirnamel = direntryl - sizeof(DirEntryLength) - sizeof(InodeNo);
			if(dirnamel > folder->size){
				PAFFS_DBG(PAFFS_TRACE_BUG, "BUG: dirname length of Inode %u not plausible (was: %d, should: >%d)!", folder->no, folder->size, p + dirnamel);
				delete[] buf;
				return Result::bug;
			}
			p += sizeof(DirEntryLength);
			InodeNo tmp_no;
			memcpy(&tmp_no, &buf[p], sizeof(InodeNo));
			p += sizeof(InodeNo);
			char* tmpname =  new char[dirnamel+1];
			memcpy(tmpname, &buf[p], dirnamel);
			tmpname[dirnamel] = 0;
			p += dirnamel;
			if(strcmp(name, tmpname) == 0){
				//Eintrag gefunden
				if(tree.getInode(tmp_no, outInode) != Result::ok){
					PAFFS_DBG(PAFFS_TRACE_BUG, "BUG: Found Element '%s' in dir, but did not find its Inode (No. %d) in Index!", tmpname, tmp_no);
					delete[] tmpname;
					delete[] buf;
					return Result::bug;
				}
				delete[] tmpname;
				delete[] buf;
				return Result::ok;
			}
			delete[] tmpname;
	}
	delete[] buf;
	return Result::nf;

}

Result Device::getInodeOfElem(Inode* outInode, const char* fullPath){
	if(driver == nullptr){
		PAFFS_DBG(PAFFS_TRACE_ERROR, "Device has no driver set!");
		return Result::fail;
	}
	Inode root;
	if(tree.getInode(0, &root) != Result::ok){
		PAFFS_DBG(PAFFS_TRACE_ERROR, "Could not find rootInode! (%s)", resultMsg[static_cast<int>(lasterr)]);
		return Result::fail;
	}
	Inode *curr = outInode;
	*curr = root;

	unsigned int fpLength = strlen(fullPath);
	char* fullPathC = new char[fpLength + 1];
	memcpy(fullPathC, fullPath, fpLength);
	fullPathC[fpLength] = 0;

	char delimiter[] = "/";
	char *fnP;
	fnP = strtok(fullPathC, delimiter);

	while(fnP != nullptr){
		if(strlen(fnP) == 0){   //is first '/'
			continue;
		}

		if(curr->type != InodeType::dir){
			delete[] fullPathC;
			return Result::einval;
		}

		Result r;
		if((r = getInodeInDir(outInode, curr, fnP)) != Result::ok){
			delete[] fullPathC;
			return r;
		}
		curr = outInode;
		//todo: Dirent cachen
		fnP = strtok(nullptr, delimiter);
	}

	delete[] fullPathC;
	return Result::ok;
}


Result Device::insertInodeInDir(const char* name, Inode* contDir, Inode* newElem){
	if(driver == nullptr){
		PAFFS_DBG(PAFFS_TRACE_ERROR, "Device has no driver set!");
		return Result::fail;
	}
	if(contDir == nullptr){
		PAFFS_DBG(PAFFS_TRACE_ERROR, "Container Directory was null!");
		return Result::bug;
	}

	unsigned int elemNameL = strlen(name);
	if(name[elemNameL-1] == '/'){
		elemNameL--;
	}

	if(elemNameL > maxDirEntryLength){
		PAFFS_DBG(PAFFS_TRACE_BUG, "Elem name too long,"
				" this should have been checked before calling insertInode");
		return Result::objNameTooLong;
	}

	//TODO: Check if name already exists

	DirEntryLength direntryl = sizeof(DirEntryLength) + sizeof(InodeNo) + elemNameL;	//Size of the new directory entry

	unsigned char *buf = new unsigned char [direntryl];
	buf[0] = direntryl;
	memcpy(&buf[sizeof(DirEntryLength)], &newElem->no, sizeof(InodeNo));

	memcpy(&buf[sizeof(DirEntryLength) + sizeof(InodeNo)], name, elemNameL);

	char* dirData = new char[contDir->size + direntryl];
	unsigned int bytes = 0;
	Result r;
	if(contDir->reservedPages > 0){		//if Directory is not empty
		r = dataIO.readInodeData(contDir, 0, contDir->size, &bytes, dirData);
		if(r != Result::ok || bytes != contDir->size){
			lasterr = r;
			delete[] dirData;
			delete[] buf;
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

	//TODO: If write more than one page, split in start and end page to reduce
	//unnecessary writes on intermediate pages.
	r = dataIO.writeInodeData(contDir, 0, contDir->size + direntryl, &bytes, dirData);
	delete[] dirData;
	delete[] buf;
	if(bytes != contDir->size && r == Result::ok){
		PAFFS_DBG(PAFFS_TRACE_BUG, "writeInodeData wrote different bytes than requested"
				", but returned OK");
	}

	return tree.updateExistingInode(contDir);

}


//TODO: mark deleted treeCacheNodes as dirty
Result Device::removeInodeFromDir(Inode* contDir, Inode* elem){
	if(driver == nullptr){
		PAFFS_DBG(PAFFS_TRACE_ERROR, "Device has no driver set!");
		return Result::fail;
	}
	if(contDir == nullptr){
		return Result::bug;
	}

	char* dirData = new char[contDir->size];
	unsigned int bytes = 0;
	Result r;
	if(contDir->reservedPages > 0){		//if Directory is not empty
		r = dataIO.readInodeData(contDir, 0, contDir->size, &bytes, dirData);
		if(r != Result::ok || bytes != contDir->size){
			lasterr = r;
			delete[] dirData;
			return r;
		}
	}else{
		delete[] dirData;
		return Result::nf;	//did not find directory entry, because dir is empty
	}


	DirEntryCount *entries = reinterpret_cast<DirEntryCount*> (&dirData[0]);
	FileSize pointer = sizeof(DirEntryCount);
	while(pointer < contDir->size){
		DirEntryLength entryl = static_cast<DirEntryLength> (dirData[pointer]);
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
			delete[] dirData;
			return r;
		}
		pointer += entryl;
	}
	delete[] dirData;
	return Result::nf;
}

Result Device::mkDir(const char* fullPath, Permission mask){
	if(driver == nullptr){
		PAFFS_DBG(PAFFS_TRACE_ERROR, "Device has no driver set!");
		return Result::fail;
	}
	if(!mounted)
		return Result::notMounted;
	unsigned int lastSlash = 0;

	Inode parDir;
	Result res = getParentDir(fullPath, &parDir, &lastSlash);
	if(res != Result::ok)
		return res;

	if(strlen(&fullPath[lastSlash]) > maxDirEntryLength){
		return Result::objNameTooLong;
	}

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
	if(driver == nullptr){
		PAFFS_DBG(PAFFS_TRACE_ERROR, "Device has no driver set!");
		lasterr = Result::fail;
		return nullptr;
	}
	if(!mounted){
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

	char* dirData = new char[dirPinode.size];
	unsigned int br = 0;
	if(dirPinode.reservedPages > 0){
		r = dataIO.readInodeData(&dirPinode, 0, dirPinode.size, &br, dirData);
		if(r != Result::ok || br != dirPinode.size){
			lasterr = r;
			return nullptr;
		}
	}else{
		memset(dirData, 0, dirPinode.size);
	}

	Dir* dir = new Dir;
	dir->self = new Dirent;
	dir->self->name = const_cast<char*>("not_impl.");
	dir->self->node = new Inode;
	*dir->self->node = dirPinode;
	dir->self->parent = nullptr;	//no caching, so we pobably dont have the parent
	dir->no_entrys = dirData[0];
	dir->childs = new Dirent [dir->no_entrys];
	dir->pos = 0;

	unsigned int p = sizeof(DirEntryCount);
	unsigned int entry;
	for(entry = 0; p < dirPinode.size; entry++){
		DirEntryLength direntryl = dirData[p];
		unsigned int dirnamel = direntryl - sizeof(DirEntryLength) - sizeof(InodeNo);
		if(dirnamel > 1 << sizeof(DirEntryLength) * 8){
			//We have an error while reading
			PAFFS_DBG(PAFFS_TRACE_BUG, "Dirname length was bigger than possible (%u)!", dirnamel);
			delete[] dir->childs;
			delete[] dirData;
			delete dir->self->node;
			delete dir->self;
			delete dir;
			lasterr = Result::bug;
			return nullptr;
		}
		p += sizeof(DirEntryLength);
		memcpy(&dir->childs[entry].no, &dirData[p], sizeof(InodeNo));
		dir->childs[entry].node = nullptr;
		p += sizeof(InodeNo);
		dir->childs[entry].name = new char[dirnamel+2];    //+2 weil 1. Nullbyte und 2. Vielleicht ein Zeichen '/' dazukommt
		memcpy(dir->childs[entry].name, &dirData[p], dirnamel);
		dir->childs[entry].name[dirnamel] = 0;
		p += dirnamel;
	}

	delete[] dirData;

	if(entry != dir->no_entrys){
		PAFFS_DBG(PAFFS_TRACE_BUG, "Directory stated it had %u entries, but has actually %u!", dir->no_entrys, entry);
		lasterr = Result::bug;
		return nullptr;
	}

	return dir;
}

Result Device::closeDir(Dir* dir){
	if(driver == nullptr){
		PAFFS_DBG(PAFFS_TRACE_ERROR, "Device has no driver set!");
		return Result::fail;
	}
	if(!mounted)
		return Result::notMounted;
	if(dir->childs == nullptr)
		return Result::einval;
	for(int i = 0; i < dir->no_entrys; i++){
		delete[] dir->childs[i].name;
		if(dir->childs[i].node != nullptr)
			delete dir->childs[i].node;
		//delete dir->childs[i];
	}
	delete[] dir->childs;
	delete dir->self->node;
	delete dir->self;
	delete dir;
	return Result::ok;
}

/**
 * TODO: What happens if dir is changed after opendir?
 */
Dirent* Device::readDir(Dir* dir){
	if(driver == nullptr){
		PAFFS_DBG(PAFFS_TRACE_ERROR, "Device has no driver set!");
		lasterr = Result::fail;
		return nullptr;
	}
	if(!mounted){
		lasterr = Result::notMounted;
		return nullptr;
	}
	if(dir->no_entrys == 0)
		return nullptr;

	if(dir->pos == dir->no_entrys){
		return nullptr;
	}

/*	if(dir->childs == nullptr){
		PAFFS_DBG(PAFFS_TRACE_ERROR, "READ DIR on dir with nullptr dirents");
		lasterr = Result::bug;
		return nullptr;
	}*/

	if(dir->pos > dir->no_entrys){
		PAFFS_DBG(PAFFS_TRACE_ERROR, "READ DIR on dir that points further than its contents");
		lasterr = Result::bug;
		return nullptr;
	}

	/*if(dir->childs[dir->pos] == nullptr){
		PAFFS_DBG(PAFFS_TRACE_ERROR, "READ DIR on dir with nullptr Dirent no. %d", dir->pos);
		lasterr = Result::bug;
		return nullptr;
	}*/

	if(dir->childs[dir->pos].node != nullptr){
		return &dir->childs[dir->pos++];
	}
	Inode item;
	Result r = tree.getInode(dir->childs[dir->pos].no, &item);
	if(r != Result::ok){
	   lasterr = Result::bug;
	   return nullptr;
	}
	if((item.perm & R) == 0){
		lasterr = Result::noperm;
		return nullptr;
	}


	dir->childs[dir->pos].node = new Inode;
	*dir->childs[dir->pos].node = item;
	if(dir->childs[dir->pos].node->type == InodeType::dir){
		int namel = strlen(dir->childs[dir->pos].name);
		dir->childs[dir->pos].name[namel] = '/';
		dir->childs[dir->pos].name[namel+1] = 0;
	}
	return &dir->childs[dir->pos++];
}

void Device::rewindDir(Dir* dir){
	if(driver == nullptr){
		PAFFS_DBG(PAFFS_TRACE_ERROR, "Device has no driver set!");
		lasterr = Result::fail;
		return;
	}
    dir->pos = 0;
}

Result Device::createFile(Inode* outFile, const char* fullPath, Permission mask){
	if(driver == nullptr){
		PAFFS_DBG(PAFFS_TRACE_ERROR, "Device has no driver set!");
		return Result::fail;
	}
	if(!mounted)
		return Result::notMounted;
	unsigned int lastSlash = 0;

	Inode parDir;
	Result res = getParentDir(fullPath, &parDir, &lastSlash);
	if(res != Result::ok){
		return res;
	}

	if(strlen(&fullPath[lastSlash]) > maxDirEntryLength){
		return Result::objNameTooLong;
	}

	if((res = createFilInode(outFile, mask)) != Result::ok){
		PAFFS_DBG(PAFFS_TRACE_ERROR, "Could not create fileInode: %s", err_msg(res));
		return res;
	}
	res = tree.insertInode(outFile);
	if(res != Result::ok){
		PAFFS_DBG(PAFFS_TRACE_ERROR, "Could not insert Inode into tree: %s", err_msg(res));
		return res;
	}

	return insertInodeInDir(&fullPath[lastSlash], &parDir, outFile);
}

Obj* Device::open(const char* path, Fileopenmask mask){
	if(driver == nullptr){
		PAFFS_DBG(PAFFS_TRACE_ERROR, "Device has no driver set!");
		lasterr = Result::fail;
		return nullptr;
	}
	if(!mounted){
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

	Obj* obj = new Obj;
	obj->dirent = new Dirent;
	obj->dirent->name = new char[strlen(path)];
	obj->dirent->node = new Inode;
	*obj->dirent->node = file;
	obj->dirent->parent = nullptr;		//TODO: Sollte aus cache gesucht werden, erstellt in "getInodeOfElem(path))" ?

	memcpy(obj->dirent->name, path, strlen(path));

	if(mask & FA){
		obj->fp = file.size;
	}else{
		obj->fp = 0;
	}

	obj->rdnly = ! (mask & FW);
	return obj;
}

Result Device::close(Obj* obj){
	if(driver == nullptr){
		PAFFS_DBG(PAFFS_TRACE_ERROR, "Device has no driver set!");
		return Result::fail;
	}
	if(!mounted)
		return Result::notMounted;
	if(obj == nullptr)
		return Result::einval;
	flush(obj);
	delete obj->dirent->node;
	delete[] obj->dirent->name;
	delete obj->dirent;
	delete obj;

	return Result::ok;
}

Result Device::touch(const char* path){
	if(driver == nullptr){
		PAFFS_DBG(PAFFS_TRACE_ERROR, "Device has no driver set!");
		return Result::fail;
	}
	if(!mounted)
		return Result::notMounted;
	if(readOnly){
		return Result::nosp;
	}

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
		file.mod = systemClock.now().convertTo<outpost::time::GpsTime>().timeSinceEpoch().milliseconds();
		return tree.updateExistingInode(&file);
	}

}


Result Device::getObjInfo(const char *fullPath, ObjInfo* nfo){
	if(driver == nullptr){
		PAFFS_DBG(PAFFS_TRACE_ERROR, "Device has no driver set!");
		return Result::fail;
	}
	if(!mounted)
		return Result::notMounted;
	Inode object;
	Result r;
	if((r = getInodeOfElem(&object, fullPath)) != Result::ok){
		return lasterr = r;
	}
	nfo->created = outpost::time::GpsTime::afterEpoch(outpost::time::Milliseconds(object.crea));
	nfo->modified = outpost::time::GpsTime::afterEpoch(outpost::time::Milliseconds(object.crea));;
	nfo->perm = object.perm;
	nfo->size = object.size;
	nfo->isDir = object.type == InodeType::dir;
	return Result::ok;
}

Result Device::read(Obj* obj, char* buf, unsigned int bytes_to_read, unsigned int *bytes_read){
	if(driver == nullptr){
		PAFFS_DBG(PAFFS_TRACE_ERROR, "Device has no driver set!");
		return Result::fail;
	}
	if(!mounted)
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

	//*bytes_read = bytes_to_read;
	obj->fp += *bytes_read;
	return Result::ok;
}

Result Device::write(Obj* obj, const char* buf, unsigned int bytes_to_write, unsigned int *bytes_written){
	if(driver == nullptr){
		PAFFS_DBG(PAFFS_TRACE_ERROR, "Device has no driver set!");
		return Result::fail;
	}
	*bytes_written = 0;
	if(!mounted)
		return Result::notMounted;
	if(obj == nullptr)
		return Result::einval;

	if(readOnly){
		return Result::nosp;
	}

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
	if(*bytes_written != bytes_to_write){
		PAFFS_DBG(PAFFS_TRACE_ERROR, "Could not write Inode Data in whole! (Should: %d, was: %d)",
				bytes_to_write, *bytes_written);
		//TODO: Handle error, maybe rewrite
		return Result::fail;
	}

	obj->dirent->node->mod = systemClock.now().convertTo<outpost::time::GpsTime>().timeSinceEpoch().milliseconds();

	obj->fp += *bytes_written;
	if(obj->fp > obj->dirent->node->size){
		//size was increased
		if(obj->dirent->node->reservedPages * dataBytesPerPage < obj->fp){
			PAFFS_DBG(PAFFS_TRACE_WRITE, "Reserved size is smaller than actual size "
					"which is OK if we skipped pages");
		}
	}
	return tree.updateExistingInode(obj->dirent->node);
}

Result Device::seek(Obj* obj, int m, Seekmode mode){
	if(driver == nullptr){
		PAFFS_DBG(PAFFS_TRACE_ERROR, "Device has no driver set!");
		return Result::fail;
	}
	if(!mounted)
		return Result::notMounted;
	switch(mode){
	case Seekmode::set :
		if(m < 0)
			return lasterr = Result::einval;
		obj->fp = m;
		break;
	case Seekmode::end :
		if(-m > static_cast<int>(obj->dirent->node->size))
			return Result::einval;
		obj->fp = obj->dirent->node->size + m;
		break;
	case Seekmode::cur :
		if(static_cast<int>(obj->fp) + m < 0)
			return Result::einval;
		obj->fp += m;
		break;
	}

	return Result::ok;
}


Result Device::flush(Obj* obj){
	if(driver == nullptr){
		PAFFS_DBG(PAFFS_TRACE_ERROR, "Device has no driver set!");
		return Result::fail;
	}

	(void) obj;
	//TODO: When Inodes get Link to its PAC, it is committed here
	//dataIO.pac.setTargetInode(obj->dirent->node);
	//dataIO.pac.commit();

	if(!mounted)
		return Result::notMounted;
	return Result::ok;
}

Result Device::chmod(const char* path, Permission perm){
	if(!mounted)
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
	if(driver == nullptr){
		PAFFS_DBG(PAFFS_TRACE_ERROR, "Device has no driver set!");
		return Result::fail;
	}
	if(!mounted)
		return Result::notMounted;
	if(readOnly){
		return Result::nosp;
	}

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
	if(driver == nullptr){
		PAFFS_DBG(PAFFS_TRACE_ERROR, "Device has no driver set!");
		return Result::fail;
	}
	PAFFS_DBG_S(PAFFS_TRACE_VERBOSE, "Device has driver set");
	if(mounted)
		return Result::alrMounted;
	PAFFS_DBG_S(PAFFS_TRACE_VERBOSE, "Device is not yet mounted");

	memset(areaMap, 0, sizeof(Area) * areasNo);
	PAFFS_DBG_S(PAFFS_TRACE_VERBOSE, "Cleared %zu Byte in Areamap", sizeof(Area) * areasNo);

	activeArea[AreaType::superblock] = 0;
	activeArea[AreaType::index] = 0;
	activeArea[AreaType::data] = 0;

	if(areasNo < AreaType::no - 2){
		PAFFS_DBG(PAFFS_TRACE_ERROR, "Device too small, at least %u Areas are needed!",
				static_cast<unsigned>(AreaType::no));
		return Result::einval;
	}

	if(blocksPerArea < 2){
		PAFFS_DBG(PAFFS_TRACE_ERROR, "Device too small, at least 2 Blocks per Area are needed!");
		return Result::einval;
	}

	if(dataPagesPerArea > dataBytesPerPage * 8){
		PAFFS_DBG(PAFFS_TRACE_ERROR, "Device Areas too big, Area Summary would not fit a single page!");
		return Result::einval;
	}

	if(blocksTotal % blocksPerArea != 0){
		PAFFS_DBG(PAFFS_TRACE_ERROR, "'blocksPerArea' does not divide "
				"%u blocks evenly! (define %u, rest: %u)",
				blocksTotal, blocksPerArea, blocksTotal % blocksPerArea);
		return Result::einval;
	}
	PAFFS_DBG_S(PAFFS_TRACE_VERBOSE, "Init success");
	return Result::ok;
}

Result Device::destroyDevice(){
	if(driver == nullptr){
		PAFFS_DBG(PAFFS_TRACE_ERROR, "Device has no driver set!");
		return Result::fail;
	}
	memset(activeArea, 0, sizeof(AreaPos)*AreaType::no);
	return Result::ok;
}


};
