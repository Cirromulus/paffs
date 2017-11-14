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

#include "null.hpp"

#include "../commonTypes.hpp"
#include <stdio.h>

namespace paffs{

Driver* getDriver(const uint8_t deviceId){
	(void) deviceId;
	Driver* out = new NullDriver();
	return out;
}

Driver* getDriverSpecial(const uint8_t deviceId, void* fc, void* mram){
	(void) deviceId;
	(void) fc;
	(void) mram;
	Driver* out = new NullDriver();
	return out;
}


Result NullDriver::initializeNand(){
	return Result::nimpl;
}
Result NullDriver::deInitializeNand(){
	return Result::nimpl;
}

Result NullDriver::writePage(uint64_t page_no,
								void* data, unsigned int data_len){
	(void) page_no;
	(void) data;
	(void) data_len;
	printf("WritePage from Nulldriver.\n");
	return Result::nimpl;
}
Result NullDriver::readPage(uint64_t page_no,
								void* data, unsigned int data_len){
	(void) page_no;
	(void) data;
	(void) data_len;
	printf("ReadPage from Nulldriver.\n");
	return Result::nimpl;
}
Result NullDriver::eraseBlock(uint32_t block_no){
	(void) block_no;
	printf("EraseBlock from Nulldriver.\n");
	return Result::nimpl;
}

Result NullDriver::markBad(uint32_t block_no){
	(void) block_no;
	printf("MarkBad from Nulldriver.\n");
	return Result::nimpl;
}

Result NullDriver::checkBad(uint32_t block_no){
	(void) block_no;
	printf("CheckBad from Nulldriver.\n");
	return Result::nimpl;
}


}
