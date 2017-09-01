/*
 * pools.cpp
 *
 *  Created on: Sep 4, 2017
 *      Author: Pascal Pieper
 */

#include "pools.hpp"


namespace paffs{

SmartInodePtr::SmartInodePtr() : mInode(nullptr), mPool(nullptr){
	//printf("Construc SIP %p\n", this);
};

SmartInodePtr::SmartInodePtr(const SmartInodePtr &other) : mInode(nullptr), mPool(nullptr){
	*this = other;
}

SmartInodePtr::~SmartInodePtr(){
	if(mInode == nullptr){
		return;
	}
	mPool->removeInodeReference(mInode->no);
	mInode = nullptr;
	mPool = nullptr;
}

void SmartInodePtr::setInode(Inode &inode, InodePoolBase &pool){
	if(mInode != nullptr){
		mPool->removeInodeReference(mInode->no);
	}
	mInode = &inode;
	mPool = &pool;
}

Inode* SmartInodePtr::operator->() const{
	return mInode;
}

SmartInodePtr::operator Inode*() const {
	return mInode;
}

SmartInodePtr& SmartInodePtr::operator=(const SmartInodePtr &other){
	if(other.mInode != nullptr){
		other.mPool->getExistingInode(other.mInode->no, *this);
	}
	return *this;
}

};
