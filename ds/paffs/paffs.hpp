/**
 * File created on 19.05.2016
 * Author: Pascal Pieper
 */

#pragma once

#include "commonTypes.hpp"
#include "paffs_trace.hpp"
#include "device.hpp"

namespace paffs{

class Paffs{
	Device device;

public:
	Paffs();
	Paffs(void* fc);
	~Paffs();

	Result format(const char* devicename);
	Result mnt(const char* devicename);
	Result unmnt(const char* devicename);
	Result getLastErr();
	void resetLastErr();

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

	//ONLY FOR DEBUG
	Device* getDevice();

};
}

