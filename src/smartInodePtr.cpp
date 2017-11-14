/*
 * Copyright (c) 2017, German Aerospace Center (DLR)
 *
 * This file is part of the development version of OUTPOST.
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 *
 * Authors:
 * - 2017, Pascal Pieper (DLR RY-AVS)
 */
// ----------------------------------------------------------------------------

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
