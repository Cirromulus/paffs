/*
 * smartInodePtr.hpp
 *
 *  Created on: Sep 4, 2017
 *      Author: Pascal Pieper
 */

#pragma once

namespace paffs{

struct InodePoolBase;
struct Inode;

class SmartInodePtr{
	Inode* mInode;
	InodePoolBase* mPool;
public:
	SmartInodePtr();
	SmartInodePtr(const SmartInodePtr &other);
	~SmartInodePtr();
	void setInode(Inode &inode, InodePoolBase &pool);
	Inode* operator->() const;
	operator Inode*() const;
	SmartInodePtr& operator=(const SmartInodePtr &other);
};

};
