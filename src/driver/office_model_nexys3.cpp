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
	nand->enableLatchUpProtection();
	return Result::ok;
}
Result
OfficeModelNexys3Driver::deInitializeNand()
{
	//FIXME Does this actually turn off NAND?
	nand->disableLatchUpProtection();
	return Result::ok;
}

Result
OfficeModelNexys3Driver::writePage(PageAbs page_no,
                                   void* data, unsigned int data_len)
{
	if(data_len == 0 || data_len > totalBytesPerPage){
		PAFFS_DBG(PAFFS_TRACE_ERROR, "Invalid write size (%" PRId16 ")", data_len);
		return Result::invalidInput;
	}

	PAFFS_DBG_S(PAFFS_TRACE_WRITE, "Write %u bytes at page %" PRIu64, data_len, page_no);

	if(totalBytesPerPage != data_len)
	{
		memset(buf+data_len, 0xFF, totalBytesPerPage - data_len);
	}
	if(data != buf)
	{
	    memcpy(buf, data, data_len);
	}

    unsigned char* p = reinterpret_cast<unsigned char*>(&buf[dataBytesPerPage+2]);
    for(int i = 0; i < dataBytesPerPage; i+=256, p+=3)
        YaffsEcc::calc(reinterpret_cast<unsigned char*>(&buf[i]), p);

	nand->writePage(bank, device, page_no, reinterpret_cast<uint8_t*>(buf));
	return Result::ok;
}
Result
OfficeModelNexys3Driver::readPage(PageAbs page_no,
                                  void* data, unsigned int data_len)
{
	PAFFS_DBG_S(PAFFS_TRACE_READ, "Read %u bytes at page %" PRIu64, data_len, page_no);
	if(data_len == 0 || data_len > totalBytesPerPage){
		PAFFS_DBG(PAFFS_TRACE_ERROR, "Invalid read size (%u)", data_len);
		return Result::invalidInput;
	}

	nand->readPage(bank, device, page_no, reinterpret_cast<uint8_t*>(buf));
    unsigned char read_ecc[3];
    unsigned char *p = reinterpret_cast<unsigned char*>(&buf[dataBytesPerPage + 2]);
    Result ret = Result::ok;
    for(int i = 0; i < dataBytesPerPage; i+=256, p+=3) {
        YaffsEcc::calc(reinterpret_cast<unsigned char*>(buf) + i, read_ecc);
        Result r = YaffsEcc::correct(reinterpret_cast<unsigned char*>(buf), p, read_ecc);
        //ok < corrected < notcorrected
        if (r > ret)
            ret = r;
    }
	if(data != buf)
	{
	    memcpy(data, buf, data_len);
	}
	(void) ret; //TODO: Return actual ECC result
	return Result::ok;
}
Result
OfficeModelNexys3Driver::eraseBlock(BlockAbs block_no)
{
	nand->eraseBlock(bank, device, block_no);
	return Result::ok;
}
Result
OfficeModelNexys3Driver::markBad(BlockAbs block_no)
{
	memset(buf, 0, totalBytesPerPage);
    for (size_t page = 0; page < 2; ++page){
        size_t pageNumber = block_no * pagesPerBlock + page;
        nand->writePage(bank, device, pageNumber, reinterpret_cast<uint8_t*>(buf));
    }
    return Result::ok;
}

Result
OfficeModelNexys3Driver::checkBad(BlockAbs block_no)
{
    for (size_t page = 0; page < 2; ++page){
        size_t pageNumber = block_no * pagesPerBlock + page;
        nand->readPage(bank, device, pageNumber, reinterpret_cast<uint8_t*>(buf));
        if (static_cast<uint8_t>(buf[4096]) != 0xFF)
            return Result::badflash;
    }
	return Result::ok;
}


bool
OfficeModelNexys3Driver::initSpaceWire()
{
    if (!spacewire.open()){
    	printf("Spacewire connection opening .. ");
        printf("failed\n");
        return false;
    }

    spacewire.up(outpost::time::Milliseconds(0));

    if (!spacewire.isUp()){
    	printf("check link .. ");
        printf("down\n");
        return false;
    }
    return true;
}
}
