/*
 * pools.hpp
 *
 *  Created on: Sep 4, 2017
 *      Author: Pascal Pieper
 */

#pragma once
#include "commonTypes.hpp"
#include "bitlist.hpp"
#include <map>
namespace paffs{

template<size_t size, typename T> struct ObjectPool{
	BitList<size> activeObjects;
	T objects[size];
	Result getNewObject(T* &outObj){
		size_t objOffs = activeObjects.findFirstFree();
		if(objOffs >= size){
			return Result::nospace;
		}
		activeObjects.setBit(objOffs);
		objects[objOffs] = T();
		outObj = &objects[objOffs];
		return Result::ok;
	}
	Result freeObject(T* obj){
		if(!isFromPool(obj)){
			PAFFS_DBG(PAFFS_TRACE_BUG, "Tried freeing an Object out of range!");
			return Result::einval;
		}
		if(!activeObjects.getBit(obj - objects)){
			PAFFS_DBG(PAFFS_TRACE_BUG, "Tried freeing an unused Object!");
			return Result::bug;
		}
		activeObjects.resetBit(obj - objects);
		obj->~T();
		return Result::ok;
	}
	bool isFromPool(T* obj){
		return (obj - objects >= 0 &&
				static_cast<unsigned int>(obj - objects) < size );
	}
	size_t getUsage(){
		uint8_t usedObjects = 0;
		for(unsigned int i = 0; i < size; i++){
			if(activeObjects.getBit(i)){
				usedObjects++;
			}
		}
		return usedObjects;
	}

	void clear(){
		activeObjects.clear();
	}
};

struct InodePoolBase{
	typedef std::pair<Inode*, uint8_t> InodeWithRefcount;
	typedef std::map<InodeNo, InodeWithRefcount>  InodeMap;
	typedef std::pair<InodeNo, InodeWithRefcount> InodeMapElem;

	virtual ~InodePoolBase(){};
	virtual Result getExistingInode(InodeNo no, SmartInodePtr &target) = 0;
	virtual Result removeInodeReference(InodeNo no) = 0;
};

template<size_t size> struct InodePool : InodePoolBase{

	ObjectPool<size, Inode> pool;
	InodeMap map;

	Result getExistingInode(InodeNo no, SmartInodePtr &target) override {
		InodeMap::iterator it = map.find(no);
		if(it == map.end()){
			return Result::nf;
		}
		Inode* inode = it->second.first;	//Inode Pointer
		it->second.second++;				//Inode Refcount
		//printf("Changed Refcount of %u to %u\n", no, it->second.second);
		target.setInode(*inode, *this);
		return Result::ok;
	}
	Result requireNewInode(InodeNo no, SmartInodePtr &target){
		InodeMap::iterator it = map.find(no);
		if(it != map.end()){
			PAFFS_DBG(PAFFS_TRACE_BUG, "Tried adding existing inode to Pool");
			return Result::bug;
		}
		Inode* inode;
		//Not yet opened
		Result r = pool.getNewObject(inode);
		if(r != Result::ok){
			PAFFS_DBG(PAFFS_TRACE_ERROR, "Could not get new Inode from Pool");
			return r;
		}
		inode->no = no;
		target.setInode(*inode, *this);
		map.insert(InodeMapElem(no,
				InodeWithRefcount(target, 1)));
		return Result::ok;
	}
	virtual Result removeInodeReference(InodeNo no) override {
		InodeMap::iterator it = map.find(no);
		if(it == map.end()){
			PAFFS_DBG(PAFFS_TRACE_BUG, "inode was not found in pool!");
			return Result::bug;
		}
		if(it->second.second == 0){
			PAFFS_DBG(PAFFS_TRACE_BUG, "Obj's Refcount is already zero!");
			return Result::bug;
		}
		it->second.second--;			//Inode refcount
		//printf("Changed Refcount of %u to %u\n", no, it->second.second);
		if(it->second.second == 0){
			Result r = pool.freeObject(it->second.first);	//Inode
			if(r != Result::ok){
				PAFFS_DBG(PAFFS_TRACE_ERROR, "Could not free Inode from Pool in Openlist!");
			}
			map.erase(it);
		}

		return Result::ok;
	};
	Result removeInode(InodeNo no){
		InodeMap::iterator it = map.find(no);
		if(it == map.end()){
			PAFFS_DBG(PAFFS_TRACE_BUG, "inode was not found in pool!");
			return Result::bug;
		}
		if(it->second.second == 0){
			PAFFS_DBG(PAFFS_TRACE_BUG, "Obj's Refcount is already zero!");
			return Result::bug;
		}
		Result r = pool.freeObject(it->second.first);	//Inode
		if(r != Result::ok){
			PAFFS_DBG(PAFFS_TRACE_ERROR, "Could not free Inode from Pool in Openlist!");
		}
		map.erase(it);
		return Result::ok;
	}

	void clear(){
		pool.clear();
		map.clear();
	}

	size_t getUsage(){
		return pool.getUsage();
	}
};

};
