/*
 * inodePool.cpp
 *
 *  Created on: Sep 4, 2017
 *      Author: user
 */


#include "commonTest.hpp"
#include <paffs.hpp>
#include <stdio.h>

using namespace testing;
using namespace paffs;

class SmartInodePointer : public testing::Test{
public:
	static constexpr unsigned int inodePoolSize = 10;
	paffs::InodePool<inodePoolSize> inodePool;

	SmartInodePtr getInode(InodeNo no){
		SmartInodePtr ret;
		Result r = inodePool.requireNewInode(no, ret);
		EXPECT_EQ(r, Result::ok);
		return ret;
	}

	void checkPoolRefs(InodeNo no, unsigned int refs){
		InodePoolBase::InodeMap::iterator it = inodePool.map.find(no);
		if(refs == 0){
			ASSERT_EQ(it, inodePool.map.end());
			return;
		}
		ASSERT_NE(it, inodePool.map.end());
		ASSERT_EQ(it->second.second, refs);
	}

	virtual void SetUp(){};

	virtual void TearDown(){
		ASSERT_EQ(inodePool.pool.getUsage(), 0u);
	};

	virtual ~SmartInodePointer(){};
};

SmartInodePtr getInode(InodeNo no);

void checkPoolRefs(InodeNo no, unsigned int refs);

TEST(SmartInodePointer, CopyOperator){
	{
		SmartInodePtr a;

		inodePool.requireNewInode(0, a);
		ASSERT_EQ(inodePool.pool.getUsage(), 1u);
		checkPoolRefs(0, 1);
		SmartInodePtr b = a;				//copy Constructor
		checkPoolRefs(0, 2);
		{
			SmartInodePtr c;
			c = a;							//copy operator
			checkPoolRefs(0, 3);
			ASSERT_EQ(inodePool.pool.getUsage(), 1u);
			c = b;							//copy overwrite (same)
			checkPoolRefs(0, 3);
			ASSERT_EQ(inodePool.pool.getUsage(), 1u);
		}
		checkPoolRefs(0, 2);

		inodePool.requireNewInode(1, b);	//overwrite b
		ASSERT_EQ(inodePool.pool.getUsage(), 2u);
		checkPoolRefs(0, 1);
		checkPoolRefs(1, 1);
		b = a;								//copy overwrite (different)
		checkPoolRefs(0, 2);
		checkPoolRefs(1, 0);
	}
	ASSERT_EQ(inodePool.pool.getUsage(), 0u);
	SmartInodePtr z = getInode(0);
	ASSERT_EQ(inodePool.pool.getUsage(), 1u);
	checkPoolRefs(0, 1);
	z = getInode(1);
	checkPoolRefs(0, 0);
	checkPoolRefs(1, 1);
	z = getInode(2);
	checkPoolRefs(0, 0);
	checkPoolRefs(1, 0);
	checkPoolRefs(2, 1);
	Result r = inodePool.getExistingInode(2, z);
	ASSERT_EQ(r, paffs::Result::ok);
	checkPoolRefs(0, 0);
	checkPoolRefs(1, 0);
	checkPoolRefs(2, 1);
}

TEST(SmartInodePointer, sizes){
	SmartInodePtr ar[size+1];
	for(int i = 0; i < size; i++){
		ar[i] = getInode(i);
	}
	Result r = inodePool.requireNewInode(size+1, ar[size]);
	ASSERT_EQ(r, Result::nosp);
}

SmartInodePtr getInode(InodeNo no){
	SmartInodePtr ret;
	Result r = inodePool.requireNewInode(no, ret);
	EXPECT_EQ(r, Result::ok);
	return ret;
}

void checkPoolRefs(InodeNo no, unsigned int refs){
	InodePoolBase::InodeMap::iterator it = inodePool.map.find(no);
	if(refs == 0){
		ASSERT_EQ(it, inodePool.map.end());
		return;
	}
	ASSERT_NE(it, inodePool.map.end());
	ASSERT_EQ(it->second.second, refs);				//Inode Refcount
}
