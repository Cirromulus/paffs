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

#include <commonTypes.hpp>
#include <outpost/rtos/timer.h>
#include <outpost/rtos/clock.h>
#pragma once

namespace paffs{

class Driver {
protected:
    uint8_t buf[totalBytesPerPage];
public:
	Driver(){};
	virtual ~Driver(){};

	virtual Result
	initializeNand() = 0;
	virtual Result
	deInitializeNand() = 0;
	virtual uint8_t*
	getPageBuffer()
	{
	    return buf;
	};
	virtual Result
	writePage (PageAbs pageNo, void* data, uint16_t dataLen) = 0;
	virtual Result
	readPage (PageAbs pageNo, void* data, uint16_t dataLen) = 0;
	virtual Result
	eraseBlock (BlockAbs blockNo) = 0;
	virtual Result
	markBad (BlockAbs blockNo) = 0;
	virtual Result
	checkBad (BlockAbs blockNo) = 0;

	virtual Result
	writeMRAM(PageAbs startByte, const void* data, uint32_t dataLen)
	{
		(void) startByte;
		(void) data;
		(void) dataLen;
		return Result::nimpl;
	}
	virtual Result
	readMRAM(PageAbs startByte, void* data, uint32_t dataLen)
	{
		(void) startByte;
		(void) data;
		(void) dataLen;
		return Result::nimpl;
	}
};

Driver* getDriver(const uint8_t deviceId);

Driver* getDriverSpecial(const uint8_t deviceId, void* fc, void* mram = nullptr);
}
