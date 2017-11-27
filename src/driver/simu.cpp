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

#include "simu.hpp"
#include "yaffs_ecc.hpp"

#include "../commonTypes.hpp"
#include "../paffs_trace.hpp"
#include <string.h>


namespace paffs{

Driver*
getDriver(const uint8_t){
	Driver* out = new SimuDriver();
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
		out = new SimuDriver(fc);
	else
		out = new SimuDriver(fc, mram);
	return out;
}


Result
SimuDriver::initializeNand(){
	memset(buf, 0xFF, totalBytesPerPage);
	return Result::ok;
}
Result
SimuDriver::deInitializeNand(){
	return Result::ok;
}

Result
SimuDriver::writePage(PageAbs page, void* data, uint16_t dataLen)
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

	unsigned char* p = reinterpret_cast<unsigned char*>(&buf[dataBytesPerPage+2]);
	for(int i = 0; i < dataBytesPerPage; i+=256, p+=3)
		YaffsEcc::calc(reinterpret_cast<unsigned char*>(&buf[i]), p);

	Nandaddress d = translatePageToAddress(page);

	if(cell->writePage(d.plane, d.block, d.page, reinterpret_cast<unsigned char*>(buf)) < 0){
		return Result::fail;
	}
	return Result::ok;
}
Result
SimuDriver::readPage(PageAbs page, void* data, uint16_t dataLen)
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

	if(cell->readPage(d.plane, d.block, d.page, reinterpret_cast<unsigned char*>(buf)) < 0){
		return Result::fail;
	}
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
	    memcpy(data, buf, dataLen);
	}
	return ret;
}
Result
SimuDriver::eraseBlock(BlockAbs block_no)
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
SimuDriver::markBad(BlockAbs block_no)
{
	memset(buf, 0, totalBytesPerPage);
	for(unsigned page = 0; page < pagesPerBlock; page++){
		Nandaddress d = translatePageToAddress(block_no * pagesPerBlock + page);
		if(cell->writePage(d.plane, d.block, d.page, reinterpret_cast<unsigned char*>(buf)) < 0){
			//ignore return Result::fail;
		}
	}
	return Result::ok;
}
Result
SimuDriver::checkBad(BlockAbs block_no)
{
	memset(buf, 0, totalBytesPerPage);
	for(unsigned page = 0; page < pagesPerBlock; page++){
		Nandaddress d = translatePageToAddress(block_no * pagesPerBlock + page);
		if(cell->readPage(d.plane, d.block, d.page, reinterpret_cast<unsigned char*>(buf)) < 0){
			return Result::badflash;
		}
		if(buf[dataBytesPerPage + 5] == 0)
			return Result::badflash;
	}
	return Result::ok;
}

Result
SimuDriver::writeMRAM(PageAbs startByte,
                      const void* data, unsigned int dataLen)
{
	unsigned const char* tmp = static_cast<unsigned const char*>(data);
	for(unsigned int i = 0; i < dataLen; i++){
		mram->setByte(startByte + i, tmp[i]);
	}
	return Result::ok;
}
Result
SimuDriver::readMRAM(PageAbs startByte,
                     void* data, unsigned int dataLen){
	unsigned char *tmp = static_cast<unsigned char*>(data);
	for(unsigned int i = 0; i < dataLen; i++){
		tmp[i] = mram->getByte(startByte + i);
	}
	return Result::ok;
}

Nandaddress
SimuDriver::translatePageToAddress(PageAbs page)
{
	Nandaddress r;
	r.page = page % simu::pagesPerBlock;
	r.block = (page / simu::pagesPerBlock) % simu::blocksPerPlane;
	r.plane = (page / simu::pagesPerBlock) / simu::blocksPerPlane;
	return r;
}

Nandaddress
SimuDriver::translateBlockToAddress(BlockAbs block)
{
	Nandaddress r;
	r.plane = block / simu::blocksPerPlane;
	r.block = block % simu::blocksPerPlane;
	r.page = 0;
	return r;
}

}
