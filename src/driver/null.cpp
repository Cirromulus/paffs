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

Driver*
getDriver(const uint8_t){
	Driver* out = new NullDriver();
	return out;
}

Driver*
getDriverSpecial(const uint8_t, void*, void*)
{
	Driver* out = new NullDriver();
	return out;
}


Result
NullDriver::initializeNand()
{
	return Result::nimpl;
}
Result
NullDriver::deInitializeNand()
{
	return Result::nimpl;
}

Result
NullDriver::writePage(PageAbs, void*, uint16_t)
{
	printf("WritePage from Nulldriver.\n");
	return Result::nimpl;
}
Result
NullDriver::readPage(PageAbs, void* , uint16_t)
{
	printf("ReadPage from Nulldriver.\n");
	return Result::nimpl;
}
Result
NullDriver::eraseBlock(BlockAbs)
{
	printf("EraseBlock from Nulldriver.\n");
	return Result::nimpl;
}

Result
NullDriver::markBad(BlockAbs)
{
	printf("MarkBad from Nulldriver.\n");
	return Result::nimpl;
}

Result
NullDriver::checkBad(BlockAbs)
{
	printf("CheckBad from Nulldriver.\n");
	return Result::nimpl;
}


}
