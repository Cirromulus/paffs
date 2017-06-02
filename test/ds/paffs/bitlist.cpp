/*
 * treeCache.cpp
 *
 *  Created on: Feb 22, 2017
 *      Author: user
 */

#include <iostream>

#include "commonTest.hpp"
#include <bitlist.hpp>

static constexpr unsigned length = 100;

TEST (Bitlist, SetAndGetBits) {
	paffs::BitList<length> bitlist;
	//bitlist.printStatus();
	for(unsigned i = 0; i < length; i++){
		ASSERT_EQ(bitlist.getBit(i), false);
	}
	ASSERT_EQ(bitlist.isSetSomewhere(), false);
	bitlist.setBit(length-1);
	ASSERT_EQ(bitlist.isSetSomewhere(), true);

	for(unsigned i = 0; i < length; i++){
		bitlist.setBit(i);
	}
	//bitlist.printStatus();
	for(unsigned i = 0; i < length; i++){
		ASSERT_EQ(bitlist.getBit(i), true);
	}
	for(unsigned i = 0; i < length; i++,i++){
		bitlist.resetBit(i);
	}
	//bitlist.printStatus();
	for(unsigned i = 0; i < length; i++){
		ASSERT_EQ(bitlist.getBit(i), i % 2 != 0);
	}
}

