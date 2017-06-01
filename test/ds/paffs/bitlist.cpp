/*
 * treeCache.cpp
 *
 *  Created on: Feb 22, 2017
 *      Author: user
 */

#include <iostream>

#include "commonTest.hpp"

static constexpr unsigned length = 100;

TEST (Bitlist, SetAndGetBits) {
	paffs::BitList<length> bitlist;
	for(unsigned i = 0; i < length; i++){
		ASSERT_EQ(bitlist.getBit(i), false);
	}
	for(unsigned i = 0; i < length; i++){
		bitlist.setBit(i);
	}
	for(unsigned i = 0; i < length; i++){
		ASSERT_EQ(bitlist.getBit(i), true);
	}
	for(unsigned i = 0; i < length; i++,i++){
		bitlist.resetBit(i);
	}
	for(unsigned i = 0; i < length; i++){
		ASSERT_EQ(bitlist.getBit(i), i % 2 != 0);
	}
}

