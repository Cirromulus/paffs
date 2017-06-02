/**
 * File created on 19.05.2016
 * Author: Pascal Pieper
 */

#include "paffs.hpp"
#include "device.hpp"
#include <stdio.h>
#include <stdlib.h>
#include <inttypes.h>


namespace paffs{

const Param stdParam{
	totalBytesPerPage, oobBytesPerPage, pagesPerBlock, blocksTotal, jumpPadNo,
	dataBytesPerPage, areasNo, blocksPerArea,
	totalPagesPerArea, dataPagesPerArea, superChainElems
};

unsigned int traceMask =
	//PAFFS_TRACE_VERBOSE |
	//PAFFS_TRACE_READ |
	PAFFS_TRACE_INFO |
	PAFFS_TRACE_AREA |
	PAFFS_TRACE_ERROR |
	PAFFS_TRACE_BUG |
	//PAFFS_TRACE_TREE |
	//PAFFS_TRACE_TREECACHE |
	//PAFFS_TRACE_ASCACHE |
	//PAFFS_TRACE_SCAN |
	//PAFFS_TRACE_WRITE |
	PAFFS_TRACE_SUPERBLOCK |
	//PAFFS_TRACE_ALLOCATE |
	//PAFFS_TRACE_VERIFY_AS |
	PAFFS_TRACE_VERIFY_TC |
	PAFFS_WRITE_VERIFY_AS |
	//PAFFS_TRACE_GC |
	//PAFFS_TRACE_GC_DETAIL |
	0;

const char* resultMsg[] = {
		"ok",
		"Unknown error",
		"Object not found",
		"Object already exists",
		"Object too big",
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
		"Object name is too big",
		"You should not be seeing this..."
};

const char* err_msg(Result pr){
	return resultMsg[static_cast<int>(pr)];
}

void Paffs::printCacheSizes(){
	PAFFS_DBG_S(PAFFS_TRACE_INFO, "-----------Devices: %u-----------", maxNumberOfDevices);
	PAFFS_DBG_S(PAFFS_TRACE_INFO,
			"TreeNode size: %zu Byte, TreeCacheNode size: %zu Byte. Cachable Nodes: %u.\n"
			"\tBranch order: %u, Leaf order: %u\n"
			"\tOverall TreeCache size: %zu Byte.",
				sizeof(TreeNode), sizeof(TreeCacheNode), treeNodeCacheSize,
				branchOrder, leafOrder,
				treeNodeCacheSize*sizeof(TreeCacheNode)
	);

	PAFFS_DBG_S(PAFFS_TRACE_INFO,
			"Packed AreaSummary size: %zu Byte. Cacheable Summaries: %u.\n"
			"\tOverall AreaSummary cache size: %zu Byte.",
			sizeof(dataPagesPerArea / 4 + 2), areaSummaryCacheSize,
			sizeof(dataPagesPerArea / 4 + 2) * areaSummaryCacheSize
	);

	PAFFS_DBG_S(PAFFS_TRACE_INFO,
			"Size of AreaMap Entry: %zu Byte. Areas: %u.\n"
			"\tOverall AreaMap Size: %zu Byte.",
				sizeof(Area), areasNo,
				sizeof(Area) * areasNo
	);

	PAFFS_DBG_S(PAFFS_TRACE_INFO,
			"Size of Address: %zu. Addresses per Page: %u\n"
			"\tBufferable Addresses in DataIO: %" PRIu32 ",\n"
			"\treducing the maximal filesize to %" PRIu32 " byte.\n"
			"\tOverall AddrBuffer Size: %zu Byte",
			sizeof(Addr), addrsPerPage,
			maxAddrs, dataBytesPerPage * maxAddrs,
			sizeof(Addr) * maxAddrs
	);

	PAFFS_DBG_S(PAFFS_TRACE_INFO, "--------------------------------\n");
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
	printCacheSizes();
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

Result Paffs::format(bool complete){
	//TODO: Handle errors
	PAFFS_DBG_S(PAFFS_TRACE_INFO, "-----------------");

	PAFFS_DBG_S(PAFFS_TRACE_INFO, "Formatting infos:\n\t"
			"dataBytesPerPage  : %04u\n\t"
			"oobBytesPerPage   : %04u\n\t"
			"pagesPerBlock     : %04u\n\t"
			"blocks            : %04u\n\t"
			"blocksPerArea     : %04u\n\t"
			"jumpPadNo         : %04u\n\n\t"

			"totalBytesPerPage : %04u\n\t"
			"areasNo           : %04u\n\t"
			"totalPagesPerArea : %04u\n\t"
			"dataPagesPerArea  : %04u\n\t"
			"areaSummarySize   : %04u\n\t"
			"superChainElems   : %04u\n\t"
			, dataBytesPerPage, oobBytesPerPage, pagesPerBlock
			, blocksTotal, blocksPerArea, jumpPadNo
			, totalBytesPerPage, areasNo, totalPagesPerArea, dataPagesPerArea
			, areaSummarySize, superChainElems
			);

	PAFFS_DBG_S(PAFFS_TRACE_INFO, "-----------------\n");

	Result r = Result::ok;
	for(uint8_t i = 0; i < maxNumberOfDevices; i++){
		if(validDevices[i]){
			Result r_ = devices[i]->format(complete);
			if(r != Result::ok)
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
			if(r != Result::ok){
				PAFFS_DBG(PAFFS_TRACE_ERROR, "Could not unmount device %d!", i);
				r = r_;
			}
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
