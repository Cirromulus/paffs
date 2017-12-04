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

#include "office_model_nexys3.hpp"

#include "../commonTypes.hpp"
#include "../paffs_trace.hpp"
#include "yaffs_ecc.hpp"
#include <string.h>
#include <inttypes.h>


namespace paffs{

using namespace outpost::hal;
using namespace outpost::iff;
using namespace outpost::leon3;

Driver*
getDriver(const uint8_t deviceId)
{
	Driver* out = new OfficeModelNexys3Driver(0, deviceId);
	return out;
}

Driver*
getDriverSpecial(const uint8_t, void*, void*)
{
	printf("No Special parameter implemented!\n");
	return NULL;
}

Result
OfficeModelNexys3Driver::initializeNand()
{
	mNand->enableLatchUpProtection();
	return Result::ok;
}
Result
OfficeModelNexys3Driver::deInitializeNand()
{
	//FIXME Does this actually turn off NAND?
	mNand->disableLatchUpProtection();
	return Result::ok;
}

Result
OfficeModelNexys3Driver::writePage(PageAbs page,
                                   void* data, uint16_t dataLen)
{
	if(dataLen == 0 || dataLen > totalBytesPerPage){
		PAFFS_DBG(PAFFS_TRACE_ERROR, "Invalid write size (%" PRId16 ")", dataLen);
		return Result::invalidInput;
	}

	PAFFS_DBG_S(PAFFS_TRACE_WRITE, "Write %" PRIu16 " bytes at page %" PTYPE_PAGEABS, dataLen, page);

	if(totalBytesPerPage != dataLen)
	{
		memset(buf+dataLen, 0xFF, totalBytesPerPage - dataLen);
	}
	if(data != buf)
	{
	    memcpy(buf, data, dataLen);
	}

	uint8_t* p = &buf[dataBytesPerPage+2];
    for(int i = 0; i < dataBytesPerPage; i+=256, p+=3)
        YaffsEcc::calc(&buf[i], p);

	mNand->writePage(mBank, mDevice, page, buf);
	return Result::ok;
}
Result
OfficeModelNexys3Driver::readPage(PageAbs page,
                                  void* data, uint16_t dataLen)
{
	PAFFS_DBG_S(PAFFS_TRACE_READ, "Read %" PRIu16 " bytes at page %" PTYPE_PAGEABS, dataLen, page);
	if(dataLen == 0 || dataLen > totalBytesPerPage){
		PAFFS_DBG(PAFFS_TRACE_ERROR, "Invalid read size (%u)", dataLen);
		return Result::invalidInput;
	}

	mNand->readPage(mBank, mDevice, page, reinterpret_cast<uint8_t*>(buf));
	uint8_t read_ecc[3];
	uint8_t *p = &buf[dataBytesPerPage + 2];
    Result ret = Result::ok;
    for(int i = 0; i < dataBytesPerPage; i+=256, p+=3) {
        YaffsEcc::calc(buf + i, read_ecc);
        Result r = YaffsEcc::correct(buf, p, read_ecc);
        //ok < corrected < notcorrected
        if (r > ret)
            ret = r;
    }
	if(data != buf)
	{
	    memcpy(data, buf, dataLen);
	}
	(void) ret; //TODO: Return actual ECC result
	return Result::ok;
}
Result
OfficeModelNexys3Driver::eraseBlock(BlockAbs block)
{
	mNand->eraseBlock(mBank, mDevice, block);
	return Result::ok;
}
Result
OfficeModelNexys3Driver::markBad(BlockAbs block)
{
	memset(buf, 0, totalBytesPerPage);
    for (size_t page = 0; page < 2; ++page){
        size_t pageNumber = block * pagesPerBlock + page;
        mNand->writePage(mBank, mDevice, pageNumber, buf);
    }
    return Result::ok;
}

Result
OfficeModelNexys3Driver::checkBad(BlockAbs block)
{
    for (size_t page = 0; page < 2; ++page){
        size_t pageNumber = block * pagesPerBlock + page;
        mNand->readPage(mBank, mDevice, pageNumber, buf);
        if (static_cast<uint8_t>(buf[4096]) != 0xFF)
            return Result::badflash;
    }
	return Result::ok;
}


bool
OfficeModelNexys3Driver::initSpaceWire()
{
    if (!mSpacewire.open()){
    	printf("Spacewire connection opening .. ");
        printf("failed\n");
        return false;
    }

    mSpacewire.up(outpost::time::Milliseconds(0));

    if (!mSpacewire.isUp()){
    	printf("check link .. ");
        printf("down\n");
        return false;
    }
    return true;
}
}
