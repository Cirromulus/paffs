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
#include "journal.hpp"
#include <outpost/rtos/clock.h>
#include <outpost/time/clock.h>

#include "pools.hpp"


#pragma once

namespace paffs {

extern outpost::rtos::SystemClock systemClock;

class Device{
	InodePool<maxNumberOfInodes> inodePool;
	ObjectPool<maxNumberOfFiles, Obj> filesPool;

public:
	Driver& driver;
	AreaPos activeArea[AreaType::no];
	AreaPos usedAreas;
	Result lasterr;
	bool mounted;
	bool readOnly;

	Btree tree;
	SummaryCache sumCache;
	AreaManagement areaMgmt;
	DataIO dataIO;
	Superblock superblock;
	MramPersistence journalPersistence;
	Journal journal;

	/*
	 * Default constructor is for uninitialized Devices only
	 */
	//Device();
	Device(Driver& driver);
	~Device();

	/**
	 * \param[in] badBlockList may be empty signalling no known bad blocks
	 * \param[in] complete if true, delete complete flash (may take a while)
	 * if false (default), only the superblocks are erased,
	 * everything else is considered deleted
	 */
	Result format(const BadBlockList &badBlockList, bool complete = false);

	Result mnt(bool readOnlyMode = false);
	Result unmnt();

	//Directory
	Result mkDir(const char* fullPath, Permission mask);
	Dir* openDir(const char* path);
	Result closeDir(Dir* &dir);
	Dirent* readDir(Dir& dir);
	void rewindDir(Dir& dir);

	//File
	Obj* open(const char* path, Fileopenmask mask);
	Result close(Obj& obj);
	Result touch(const char* path);
	Result getObjInfo(const char *fullPath, ObjInfo& nfo);
	Result read(Obj& obj, char* buf, unsigned int bytes_to_read,
			unsigned int *bytes_read);
	Result write(Obj& obj, const char* buf, unsigned int bytes_to_write,
			unsigned int *bytes_written);
	Result seek(Obj& obj, int m, Seekmode mode);
	Result flush(Obj& obj);
	Result truncate(const char* path, unsigned int newLength);
	Result remove(const char* path);
	Result chmod(const char* path, Permission perm);
	Result getListOfOpenFiles(Obj* list[]);
	uint8_t getNumberOfOpenFiles();
	uint8_t getNumberOfOpenInodes();

private:

	Result initializeDevice();
	Result destroyDevice();

	Result createInode(SmartInodePtr &outInode, Permission mask);
	Result createDirInode(SmartInodePtr &outInode, Permission mask);
	Result createFilInode(SmartInodePtr &outInode, Permission mask);
	Result getParentDir(const char* fullPath, SmartInodePtr &parDir,
			unsigned int *lastSlash);
	Result getInodeNoInDir(InodeNo& outInode, Inode& folder, const char* name);
	/**
	 * @param outInode has to point to an Inode, it is used as a buffer!
	 */
	Result getInodeOfElem(SmartInodePtr &outInode, const char* fullPath);
	/**
	 * @warn does not store the target into List, just checks whether it has to load it from tree
	 * TODO: Future possibility is to store this target into cache (no buffer needed)
	 *
	 * @param target shall not point to valid data
	 */
	Result findOrLoadInode(InodeNo no, SmartInodePtr &target);

	//newElem should be already inserted in Tree
	Result insertInodeInDir(const char* name, Inode& contDir, Inode& newElem);
	Result removeInodeFromDir(Inode& contDir, InodeNo elem);
	Result createFile(SmartInodePtr& outFile, const char* fullPath, Permission mask);
};

}  // namespace paffs
