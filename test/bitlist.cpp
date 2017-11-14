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

