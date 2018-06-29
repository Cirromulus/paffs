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

#include "simuBlock.hpp"
#include "yaffs_ecc.hpp"

#include "../commonTypes.hpp"
#include "../paffs_trace.hpp"
#include <string.h>


namespace paffs{

Driver*
getDriver(const uint8_t){
	Driver* out = new SimuBlockDriver();
	return out;
}

Driver*
getDriverSpecial(const uint8_t, void* fc, void *mram){
	if(fc == NULL){
		std::cerr << "Invalid flashCell pointer given!" << std::endl;
		return NULL;
	}
	Driver* out;
	if(mram == nullptr)
		out = new SimuBlockDriver(fc);
	else
		out = new SimuBlockDriver(fc, mram);
	return out;
}

SimuBlockDriver::SimuBlockDriver()
{
    selfLoadedFlash = true;
    selfLoadedMRAM = true;
    cell = new FlashCell();
    mram = new Mram(mramSize);
}
SimuBlockDriver::SimuBlockDriver(void *c)
{
    selfLoadedMRAM = true;
    cell = static_cast<FlashCell*>(c);
    mram = new Mram(mramSize);
}
SimuBlockDriver::SimuBlockDriver(void *c, void *m)
{
    cell = static_cast<FlashCell*>(c);
    mram = static_cast<Mram*>(m);
}
SimuBlockDriver::~SimuBlockDriver()
{
    if(selfLoadedFlash)
        delete cell;
    if(selfLoadedMRAM)
        delete mram;
}


Result
SimuBlockDriver::initializeNand(){
    memset(buf, 0xFF, totalBytesPerPage);
    return Result::ok;
}
Result
SimuBlockDriver::deInitializeNand(){
    return Result::ok;
}

Result
SimuBlockDriver::writePage(PageAbs page, void* data, uint16_t dataLen)
{
    //TODO: Simple write-trough buffer by noting address
    if(!cell)
    {
        return Result::fail;
    }
    if(dataLen > totalBytesPerPage)
    {
        PAFFS_DBG(PAFFS_TRACE_BUG, "Tried to write %u Bytes to a page of %u!", dataLen, totalBytesPerPage);
        return Result::fail;
    }

    if(totalBytesPerPage != dataLen)
    {
        memset(buf+dataLen, 0xFF, totalBytesPerPage - dataLen);
    }
    if(data != buf)
    {
        memcpy(buf, data, dataLen);
    }

    if(dataLen <= dataBytesPerPage)
    {
        uint8_t* p = &buf[dataBytesPerPage+2];
        for(int i = 0; i < dataBytesPerPage; i+=256, p+=3)
        {
            YaffsEcc::calc(&buf[i], p);
        }
    }

    Nandaddress d = translatePageToAddress(page);

    if(cell->writePage(d.plane, d.block, d.page, buf) < 0){
        return Result::fail;
    }
    return Result::ok;
}
Result
SimuBlockDriver::readPage(PageAbs page, void* data, uint16_t dataLen)
{
    if(!cell)
    {
        return Result::fail;
    }
    if(dataLen > totalBytesPerPage)
    {
        PAFFS_DBG(PAFFS_TRACE_BUG, "Tried reading more than a page width!");
        return Result::invalidInput;
    }
    //TODO: Simple write-trough buffer by checking if same address

    Nandaddress d = translatePageToAddress(page);
    if(cell->readPage(d.plane, d.block, d.page, reinterpret_cast<unsigned char*>(buf)) < 0)
    {
        return Result::fail;
    }
    uint8_t readEcc[3];
    uint8_t *p = &buf[dataBytesPerPage + 2];
    Result ret = Result::ok;
    for(int i = 0; i < dataBytesPerPage; i+=256, p+=3)
    {
        YaffsEcc::calc(buf + i, readEcc);
        Result r = YaffsEcc::correct(buf, p, readEcc);
        //ok < corrected < notcorrected
        if (r > ret)
        {
            ret = r;
        }
    }
    if(data != buf)
    {
        memcpy(data, buf, dataLen);
    }
    return ret;
}
Result
SimuBlockDriver::eraseBlock(BlockAbs block_no)
{
    if(!cell)
        return Result::fail;
    if(block_no > blocksTotal){
        PAFFS_DBG(PAFFS_TRACE_BUG, "Tried erasing Block out of bounds!"
                "Was %" PRIu32 ", should < %" PRIu32, block_no, blocksTotal);
    }
    Nandaddress d = translateBlockToAddress(block_no);
    return cell->eraseBlock(d.plane, d.block) == 0 ? Result::ok : Result::fail;
}
Result
SimuBlockDriver::markBad(BlockAbs block_no)
{
    memset(buf, 0, totalBytesPerPage);
    Nandaddress d = translatePageToAddress(block_no * pagesPerBlock);
    // bad Block marker gets in Simudriver only written to first page to test bad block safety
    cell->writePage(d.plane, d.block, d.page, buf);
    return Result::ok;
}
Result
SimuBlockDriver::checkBad(BlockAbs block_no)
{
    memset(buf, 0, totalBytesPerPage);
    // bad Block marker gets in Simudriver only written to first page to test bad block safety
    Nandaddress d = translatePageToAddress(block_no * pagesPerBlock);
    if(cell->readPage(d.plane, d.block, d.page, buf) < 0)
    {
        return Result::badFlash;
    }
    if(buf[dataBytesPerPage + 5] != 0xFF)
    {
        return Result::badFlash;
    }
    return Result::ok;
}

Result
SimuBlockDriver::writeMRAM(PageAbs startByte,
                      const void* data, uint32_t dataLen)
{
	mram->writeBlock(startByte, data, dataLen);
	return Result::ok;
}
Result
SimuBlockDriver::readMRAM(PageAbs startByte,
                     void* data, uint32_t dataLen){
    mram->readBlock(startByte, data, dataLen);
	return Result::ok;
}


Nandaddress
SimuBlockDriver::translatePageToAddress(PageAbs page)
{
    Nandaddress r;
    r.page = page % simu::pagesPerBlock;
    r.block = (page / simu::pagesPerBlock) % simu::blocksPerPlane;
    r.plane = (page / simu::pagesPerBlock) / simu::blocksPerPlane;
    return r;
}

Nandaddress
SimuBlockDriver::translateBlockToAddress(BlockAbs block)
{
    Nandaddress r;
    r.plane = block / simu::blocksPerPlane;
    r.block = block % simu::blocksPerPlane;
    r.page = 0;
    return r;
}


}
