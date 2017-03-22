/**
 * File created on 19.05.2016
 * Author: Pascal Pieper
 */

#include "paffs.hpp"
#include "device.hpp"

#include <stdio.h>
#include <stdlib.h>


namespace paffs{

unsigned int traceMask =
	PAFFS_TRACE_INFO |
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

void Paffs::printCacheSizes(){
	for(uint8_t i = 0; i < maxNumberOfDevices; i++){
		if(validDevices[i]){
			PAFFS_DBG_S(PAFFS_TRACE_INFO, "---------DEVICE %d--------------", i);
			PAFFS_DBG_S(PAFFS_TRACE_INFO,
					"TreeNode size: %zu Byte, TreeCacheNode size: %zu Byte. Cachable Nodes: %d.\n"
					"\tOverall TreeCache size: %zu Byte.",
						sizeof(TreeNode), sizeof(TreeCacheNode), treeNodeCacheSize, treeNodeCacheSize*sizeof(TreeCacheNode)
			);

			PAFFS_DBG_S(PAFFS_TRACE_INFO,
					"Packed AreaSummary size: %zu Byte. Cacheable Summaries: %d.\n"
					"\tOverall AreaSummary cache size: %zu Byte.",
					sizeof(dataPagesPerArea / 4 + 2), areaSummaryCacheSize,
					sizeof(dataPagesPerArea / 4 + 2) * areaSummaryCacheSize
			);

			PAFFS_DBG_S(PAFFS_TRACE_INFO,
					"Total static size of PAFFS object: %zu",
					sizeof(Paffs)
			);

			PAFFS_DBG_S(PAFFS_TRACE_INFO, "--------------------------------");
		}
	}
}

Paffs::Paffs(std::vector<Driver*> &deviceDrivers){
	memset(validDevices, false, maxNumberOfDevices * sizeof(bool));
	memset(devices, 0, maxNumberOfDevices * sizeof(void*));
	int i = 0;
	for(std::vector<Driver*>::iterator it = deviceDrivers.begin();
			it != deviceDrivers.end();it++,i++){
		if(i >= maxNumberOfDevices){
			PAFFS_DBG(PAFFS_TRACE_ERROR, "Too many device Drivers given! Accepting max %d.", maxNumberOfDevices);
			break;
		}
		validDevices[i] = true;
		devices[i] = new Device(*it);
	}
}
Paffs::~Paffs(){
	for(int i = 0; i < maxNumberOfDevices; i++){
		if(devices[i] != nullptr)
			delete devices[i];
	}
};


Result Paffs::getLastErr(){
	//TODO: Actually choose which error to display
	return devices[0]->lasterr;
}

void Paffs::resetLastErr(){
	for(uint8_t i = 0; i < maxNumberOfDevices; i++){
		if(validDevices[i])
			devices[i]->lasterr = Result::ok;
	}
}

Result Paffs::format(){
	//TODO: Handle errors
	Result r = Result::ok;
	for(uint8_t i = 0; i < maxNumberOfDevices; i++){
		if(validDevices[i]){
			Result r_ = devices[i]->format();
			if(r == Result::ok)
				r = r_;
				r = r_;
		}
	}
	return r;
}

Result Paffs::mount(){
	//TODO: Handle errors
	Result r = Result::ok;
	for(uint8_t i = 0; i < maxNumberOfDevices; i++){
		if(validDevices[i]){
			Result r_ = devices[i]->mnt();
			if(r == Result::ok)
				r = r_;
		}
	}
	return r;
}
Result Paffs::unmount(){
	//TODO: Handle errors
	Result r = Result::ok;
	for(uint8_t i = 0; i < maxNumberOfDevices; i++){
		if(validDevices[i]){
			Result r_ = devices[i]->unmnt();
			if(r == Result::ok)
				r = r_;
		}
	}
	return r;
}

Result Paffs::mkDir(const char* fullPath, Permission mask){
	//TODO: Handle errors
	Result r = Result::ok;
	for(uint8_t i = 0; i < maxNumberOfDevices; i++){
		if(validDevices[i]){
			Result r_ = devices[i]->mkDir(fullPath, mask);
			if(r == Result::ok)
				r = r_;
		}
	}
	return r;
}

Dir* Paffs::openDir(const char* path){
	//TODO: Handle multiple positions
	return devices[0]->openDir(path);
}

Result Paffs::closeDir(Dir* dir){
	//TODO: Handle multiple positions
	return devices[0]->closeDir(dir);
}


Dirent* Paffs::readDir(Dir* dir){
	//TODO: Handle multiple positions
	return devices[0]->readDir(dir);
}

void Paffs::rewindDir(Dir* dir){
	//TODO: Handle multiple positions
	devices[0]->rewindDir(dir);
}


Obj* Paffs::open(const char* path, Fileopenmask mask){
	return devices[0]->open(path, mask);
}

Result Paffs::close(Obj* obj){
	return devices[0]->close(obj);
}

Result Paffs::touch(const char* path){
	return devices[0]->touch(path);
}

Result Paffs::getObjInfo(const char *fullPath, ObjInfo* nfo){
	return devices[0]->getObjInfo(fullPath, nfo);
}

Result Paffs::read(Obj* obj, char* buf, unsigned int bytes_to_read, unsigned int *bytes_read){
	return devices[0]->read(obj, buf, bytes_to_read, bytes_read);
}

Result Paffs::write(Obj* obj, const char* buf, unsigned int bytes_to_write, unsigned int *bytes_written){
	return devices[0]->write(obj, buf, bytes_to_write, bytes_written);
}

Result Paffs::seek(Obj* obj, int m, Seekmode mode){
	return devices[0]->seek(obj, m, mode);
}

Result Paffs::flush(Obj* obj){
	return devices[0]->flush(obj);
}

Result Paffs::chmod(const char* path, Permission perm){
	return devices[0]->chmod(path, perm);
}
Result Paffs::remove(const char* path){
	return devices[0]->remove(path);
}

//ONLY FOR DEBUG
Device* Paffs::getDevice(){
	return devices[0];
}

void Paffs::setTraceMask(unsigned int mask){
	traceMask = mask;
}

}
