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

#include "office_model_2_artix7.hpp"

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
	Driver* out = new OfficeModel2Artix7Driver(0, deviceId);
	return out;
}

Driver*
getDriverSpecial(const uint8_t, void*, void*)
{
	printf("No Special parameter implemented!\n");
	return NULL;
}

Result
OfficeModel2Artix7Driver::initializeNand()
{
	mNand->enableLatchUpProtection();
	return mNand->isReady() ? Result::ok : Result::fail;
}
Result
OfficeModel2Artix7Driver::deInitializeNand()
{
	//FIXME Does this actually turn off NAND?
	mNand->disableLatchUpProtection();
	return Result::ok;
}

Result
OfficeModel2Artix7Driver::writePage(PageAbs page,
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

	return mNand->writePage(mBank, mDevice, page, buf) ? Result::ok : Result::fail;
}
Result
OfficeModel2Artix7Driver::readPage(PageAbs page,
                                  void* data, uint16_t dataLen)
{
	PAFFS_DBG_S(PAFFS_TRACE_READ, "Read %" PRIu16 " bytes at page %" PTYPE_PAGEABS, dataLen, page);
	if(dataLen == 0 || dataLen > totalBytesPerPage){
		PAFFS_DBG(PAFFS_TRACE_ERROR, "Invalid read size (%u)", dataLen);
		return Result::invalidInput;
	}

	if(!mNand->readPage(mBank, mDevice, page, reinterpret_cast<uint8_t*>(buf)))
	{
	    return Result::fail;
	}
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
OfficeModel2Artix7Driver::eraseBlock(BlockAbs block)
{
	mNand->eraseBlock(mBank, mDevice, block);
	return Result::ok;
}
Result
OfficeModel2Artix7Driver::markBad(BlockAbs block)
{
	memset(buf, 0, totalBytesPerPage);
    for (size_t page = 0; page < 2; ++page){
        size_t pageNumber = block * pagesPerBlock + page;
        mNand->writePage(mBank, mDevice, pageNumber, buf);
    }
    return Result::ok;
}

Result
OfficeModel2Artix7Driver::checkBad(BlockAbs block)
{
    for (size_t page = 0; page < 2; ++page){
        size_t pageNumber = block * pagesPerBlock + page;
        mNand->readPage(mBank, mDevice, pageNumber, buf);
        if (static_cast<uint8_t>(buf[4096]) != 0xFF)
            return Result::badFlash;
    }
	return Result::ok;
}

Result
OfficeModel2Artix7Driver::writeMRAM(PageAbs startByte,
                      const void* data, uint32_t dataLen)
{
    if(startByte + dataLen > mramSize)
    {
        return Result::invalidInput;
    }
    if(dataLen > sizeof(journalEntry::Max))
    {
        return Result::invalidInput;
    }

    bool startMisaligned = startByte % 4 != 0 || (startByte + dataLen) % 4 != 0;

    uint32_t cur = 0;
    const uint8_t* bytewiseData = static_cast<const uint8_t*>(data);

    if(startMisaligned)
    {
        uint32_t word = MRAMStartAddr[startByte / 4];
        uint8_t bytesMisaligned = 4 - startByte % 4;
        if(bytesMisaligned > dataLen)
        {
            bytesMisaligned = dataLen;
        }
        memcpy(reinterpret_cast<uint8_t*>(&word) + startByte % 4, bytewiseData, bytesMisaligned);
        MRAMStartAddr[startByte / 4] = word;
        cur = bytesMisaligned;
    }

    if(cur == dataLen)
    {   //write smaller than one word (4 Byte)
        return Result::ok;
    }

    uint32_t alignedEnd = ((startByte + dataLen) / 4) * 4;

    //align rest of the input bytes
    memcpy(mJEBuf, &bytewiseData[cur], dataLen - cur);
    for(uint_fast16_t i = 0; i * 4 < alignedEnd - (startByte + cur); i++)
    {   //copy in word-width
        MRAMStartAddr[((startByte + cur)/ 4) + i] = mJEBuf[i];
    }
    cur = alignedEnd;

    if(alignedEnd != startByte + dataLen)
    {
        uint32_t word = MRAMStartAddr[(startByte + dataLen) / 4];
        uint8_t bytesMisaligned = (startByte + dataLen) % 4;
        memcpy(&word, &bytewiseData[cur], bytesMisaligned);
        MRAMStartAddr[(startByte + dataLen) / 4] = word;
    }

    return Result::ok;
}
Result
OfficeModel2Artix7Driver::readMRAM(PageAbs startByte,
                     void* data, uint32_t dataLen){
    if(startByte + dataLen > mramSize)
    {
        return Result::noSpace;
    }
    if(dataLen > sizeof(journalEntry::Max))
    {
        return Result::invalidInput;
    }
    bool startMisaligned = startByte % 4 != 0 || (startByte + dataLen) % 4 != 0;

    uint32_t cur = 0;
    uint8_t* bytewiseData = static_cast<uint8_t*>(data);

    if(startMisaligned)
    {
        uint32_t word = MRAMStartAddr[startByte / 4];
        uint8_t bytesMisaligned = 4 - startByte % 4;
        if(bytesMisaligned > dataLen)
        {
            bytesMisaligned = dataLen;
        }
        memcpy(bytewiseData, reinterpret_cast<uint8_t*>(&word) + startByte % 4, bytesMisaligned);
        cur = bytesMisaligned;
    }

    if(cur == dataLen)
    {   //read smaller than one word (4 Byte)
        return Result::ok;
    }

    uint32_t alignedEnd = ((startByte + dataLen) / 4) * 4;

    //align rest of the input bytes
    for(uint_fast16_t i = 0; i * 4 < alignedEnd - (startByte + cur); i++)
    {   //copy in word-width
        mJEBuf[i] = MRAMStartAddr[((startByte + cur)/ 4) + i];
    }
    memcpy(&bytewiseData[cur], mJEBuf, dataLen - cur);
    cur = alignedEnd;

    if(alignedEnd != startByte + dataLen)
    {
        uint32_t word = MRAMStartAddr[(startByte + dataLen) / 4];
        uint8_t bytesMisaligned = (startByte + dataLen) % 4;
        memcpy(&bytewiseData[cur], &word, bytesMisaligned);
    }
    return Result::ok;
}

bool
OfficeModel2Artix7Driver::initSpaceWire()
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
