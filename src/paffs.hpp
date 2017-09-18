/**
 * File created on 19.05.2016
 * Author: Pascal Pieper
 */

#pragma once

#include "commonTypes.hpp"
#include "paffs_trace.hpp"
#include "device.hpp"
#include <vector>

namespace paffs{

extern unsigned int traceMask;

class Paffs{
	//TODO: Devices on bss rather than on heap
	Device *devices[maxNumberOfDevices] = {};
	bool validDevices[maxNumberOfDevices] = {};
	void printCacheSizes();
public:
	Paffs(std::vector<Driver*> &deviceDrivers);
	~Paffs();

	Result format(bool complete = false);
	Result format(const BadBlockList badBlockList[maxNumberOfDevices],
			bool complete = false);
	Result mount(bool readoOnly=false);
	Result unmount();
	Result getLastErr();
	void resetLastErr();

	//Directory
	Result mkDir(const char* fullPath, Permission mask = R | W | X);
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
	Result seek(Obj* obj, int m, Seekmode mode = Seekmode::set);
	Result flush(Obj* obj);
	Result truncate(const char* path, unsigned int newLength);

	Result chmod(const char* path, Permission perm);
	Result remove(const char* path);
	Result getListOfOpenFiles(Obj* list[]);
	uint8_t getNumberOfOpenFiles();
	uint8_t getNumberOfOpenInodes();

	//ONLY FOR DEBUG
	Device* getDevice(unsigned int number);
	void setTraceMask(unsigned int mask);
	unsigned int getTraceMask();
};
}

