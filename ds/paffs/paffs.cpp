/**
 * File created on 19.05.2016
 * Author: Pascal Pieper
 */

#include "paffs.hpp"
#include "device.hpp"
#include "driver/driverconf.hpp"

#include <stdio.h>
#include <linux/string.h>
#include <time.h>
#include <stdlib.h>



namespace paffs{

unsigned int traceMask =
	PAFFS_TRACE_AREA |
	PAFFS_TRACE_ERROR |
	PAFFS_TRACE_BUG |
	//PAFFS_TRACE_TREE |
	//PAFFS_TRACE_TREECACHE |
	PAFFS_TRACE_ASCACHE |
	//PAFFS_TRACE_SCAN |
	//PAFFS_TRACE_WRITE |
	//PAFFS_TRACE_SUPERBLOCK |
	//PAFFS_TRACE_ALLOCATE |
	//PAFFS_TRACE_VERIFY_AS |
	PAFFS_TRACE_VERIFY_TC |
	PAFFS_WRITE_VERIFY_AS |
	PAFFS_TRACE_GC |
	PAFFS_TRACE_GC_DETAIL |
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
		"Flash needs retirement",
		"Device is not mounted",
		"Device is already mounted",
		"You should not be seeing this..."
};

const char* err_msg(Result pr){
	return resultMsg[static_cast<int>(pr)];
}


Paffs::Paffs() : device(getDriver("1")){}
Paffs::Paffs(void* fc) : device(getDriverSpecial("1", fc)){}
Paffs::~Paffs(){};


Result Paffs::getLastErr(){
	return device.lasterr;
}

void Paffs::resetLastErr(){
	device.lasterr = Result::ok;
}

Result Paffs::format(const char* devicename){
	(void) devicename;
	return device.format();
}

Result Paffs::mount(const char* devicename){
	(void) devicename;
	return device.mnt();
}
Result Paffs::unmount(const char* devicename){
	(void) devicename;
	return device.unmnt();
}

Result Paffs::mkDir(const char* fullPath, Permission mask){
	return device.mkDir(fullPath, mask);
}

Dir* Paffs::openDir(const char* path){
	return device.openDir(path);
}

Result Paffs::closeDir(Dir* dir){
	return device.closeDir(dir);
}


Dirent* Paffs::readDir(Dir* dir){
	return device.readDir(dir);
}

void Paffs::rewindDir(Dir* dir){
	device.rewindDir(dir);
}


Obj* Paffs::open(const char* path, Fileopenmask mask){
	return device.open(path, mask);
}

Result Paffs::close(Obj* obj){
	return device.close(obj);
}

Result Paffs::touch(const char* path){
	return device.touch(path);
}

Result Paffs::getObjInfo(const char *fullPath, ObjInfo* nfo){
	return device.getObjInfo(fullPath, nfo);
}

Result Paffs::read(Obj* obj, char* buf, unsigned int bytes_to_read, unsigned int *bytes_read){
	return device.read(obj, buf, bytes_to_read, bytes_read);
}

Result Paffs::write(Obj* obj, const char* buf, unsigned int bytes_to_write, unsigned int *bytes_written){
	return device.write(obj, buf, bytes_to_write, bytes_written);
}

Result Paffs::seek(Obj* obj, int m, Seekmode mode){
	return device.seek(obj, m, mode);
}

Result Paffs::flush(Obj* obj){
	return device.flush(obj);
}

Result Paffs::chmod(const char* path, Permission perm){
	return device.chmod(path, perm);
}
Result Paffs::remove(const char* path){
	return device.remove(path);
}

//ONLY FOR DEBUG
Device* Paffs::getDevice(){
	return &device;
}

void Paffs::setTraceMask(unsigned int mask){
	traceMask = mask;
}

}
