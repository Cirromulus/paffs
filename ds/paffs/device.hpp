/*
 * device.hpp
 *
 *  Created on: 15 Feb 2017
 *      Author: Pascal Pieper
 */
#include "commonTypes.hpp"
#include "driver/driver.hpp"
#include "btree.hpp"
#include "area.hpp"
#include "dataIO.hpp"
#include "summaryCache.hpp"
#pragma once

namespace paffs {

class Device{
public:
	Param *param;
	AreaPos activeArea[AreaType::no];
	Area *areaMap;
	Driver *driver;
	Result lasterr;

	Btree tree;
	SummaryCache sumCache;
	AreaManagement areaMgmt;
	DataIO dataIO;

	Device(Driver* driver);
	~Device();

	Result format();
	Result mnt();
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
	Result read(Obj* obj, char* buf, unsigned int bytes_to_read, unsigned int *bytes_read);
	Result write(Obj* obj, const char* buf, unsigned int bytes_to_write, unsigned int *bytes_written);
	Result seek(Obj* obj, int m, Seekmode mode);
	Result flush(Obj* obj);
	Result truncate(Dirent* obj);

	Result chmod(const char* path, Permission perm);
	Result remove(const char* path);

private:

	Result initializeDevice();
	Result destroyDevice();

	Result createInode(Inode* outInode, Permission mask);
	Result createDirInode(Inode* outInode, Permission mask);
	Result createFilInode(Inode* outInode, Permission mask);
	void destroyInode(Inode* node);
	Result getParentDir(const char* fullPath, Inode* parDir, unsigned int *lastSlash);
	Result getInodeInDir( Inode* outInode, Inode* folder, const char* name);
	Result getInodeOfElem( Inode* outInode, const char* fullPath);
	//newElem should be already inserted in Tree
	Result insertInodeInDir(const char* name, Inode* contDir, Inode* newElem);
	Result removeInodeFromDir(Inode* contDir, Inode* elem);
	Result createFile(Inode* outFile, const char* fullPath, Permission mask);
};

}  // namespace paffs
