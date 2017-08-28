/*
 * device.hpp
 *
 *  Created on: 15 Feb 2017
 *      Author: Pascal Pieper
 */
#include "commonTypes.hpp"
#include "driver/driver.hpp"
#include "area.hpp"
#include "btree.hpp"
#include "dataIO.hpp"
#include "superblock.hpp"
#include "summaryCache.hpp"
#include <outpost/rtos/clock.h>
#include <outpost/time/clock.h>
#include <map>

#pragma once

namespace paffs {

extern outpost::rtos::SystemClock systemClock;

class Device{
	typedef std::pair<Inode*, uint8_t> InodeWithRefcount;
	typedef std::map<InodeNo, InodeWithRefcount>  InodeMap;
	typedef std::pair<InodeNo, InodeWithRefcount> InodeMapElem;
	InodeMap openInodes;

public:
	Driver *driver;
	AreaPos activeArea[AreaType::no];
	Area areaMap[areasNo];
	AreaPos usedAreas;
	Result lasterr;
	bool mounted;
	bool readOnly;

	Btree tree;
	SummaryCache sumCache;
	AreaManagement areaMgmt;
	DataIO dataIO;
	Superblock superblock;

	/*
	 * Default constructor is for uninitialized Devices only
	 */
	//Device();
	Device(Driver* driver);
	~Device();

	Result format(bool complete = false);
	Result mnt(bool readOnlyMode = false);
	Result unmnt();

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
	Result read(Obj* obj, char* buf, unsigned int bytes_to_read,
			unsigned int *bytes_read);
	Result write(Obj* obj, const char* buf, unsigned int bytes_to_write,
			unsigned int *bytes_written);
	Result seek(Obj* obj, int m, Seekmode mode);
	Result flush(Obj* obj);
	Result truncate(const char* path, unsigned int newLength);
	Result remove(const char* path);
	Result chmod(const char* path, Permission perm);

private:

	Result initializeDevice();
	Result destroyDevice();

	Result createInode(Inode* outInode, Permission mask);
	Result createDirInode(Inode* outInode, Permission mask);
	Result createFilInode(Inode* outInode, Permission mask);
	void   destroyInode(Inode* node);
	Result getParentDir(const char* fullPath, Inode* parDir,
			unsigned int *lastSlash);
	Result getInodeInDir( InodeNo *inode, Inode* folder, const char* name);
	/**
	 * @param outInode has to point to an Inode, it is used as a buffer!
	 */
	Result getInodeOfElem(Inode* &outInode, const char* fullPath);
	/**
	 * @warn does not store the target into List, just checks whether it has to load it from tree
	 * TODO: Future possibility is to store this target into cache (no buffer needed)
	 *
	 * @param target has to point to an Inode, it is used as a buffer!
	 */
	Result findOrLoadInode(InodeNo no, Inode* &target);
	void   addOpenInode(InodeNo no, Inode* &target, Inode* source);
	Result removeOpenInode(InodeNo no);

	//newElem should be already inserted in Tree
	Result insertInodeInDir(const char* name, Inode* contDir, Inode* newElem);
	Result removeInodeFromDir(Inode* contDir, Inode* elem);
	Result createFile(Inode* outFile, const char* fullPath, Permission mask);
};

}  // namespace paffs
