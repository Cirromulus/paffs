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

Driver* getDriver(const uint8_t deviceId){
	(void) deviceId;
	Driver* out = new SimuDriver();
	return out;
}

Driver* getDriverSpecial(const uint8_t deviceId, void* fc, void *mram){
	(void) deviceId;
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


Result SimuDriver::initializeNand(){
	memset(buf, 0xFF, totalBytesPerPage);
	return Result::ok;
}
Result SimuDriver::deInitializeNand(){
	return Result::ok;
}

Result SimuDriver::writePage(uint64_t page_no,
								void* data, unsigned int data_len){
	if(!cell)
		return Result::fail;

	if(data_len > totalBytesPerPage){
		PAFFS_DBG(PAFFS_TRACE_BUG, "Tried to write %u Bytes to a page of %u!", data_len, totalBytesPerPage);
		return Result::fail;
	}

	if(totalBytesPerPage != data_len){
		memset(buf+data_len, 0xFF, totalBytesPerPage - data_len);
	}
	memcpy(buf, data, data_len);

	unsigned char* p = &buf[dataBytesPerPage+2];
	for(int i = 0; i < dataBytesPerPage; i+=256, p+=3)
		YaffsEcc::calc(static_cast<unsigned char*>(buf) + i, p);

	Nandaddress d = translatePageToAddress(page_no, cell);

	if(cell->writePage(d.plane, d.block, d.page, static_cast<unsigned char*>(buf)) < 0){
		return Result::fail;
	}
	return Result::ok;
}
Result SimuDriver::readPage(uint64_t page_no,
								void* data, unsigned int data_len){
	if(!cell)
		return Result::fail;


	Nandaddress d = translatePageToAddress(page_no, cell);

	if(cell->readPage(d.plane, d.block, d.page, buf) < 0){
		return Result::fail;
	}
	unsigned char read_ecc[3];
	unsigned char *p = &buf[dataBytesPerPage + 2];
	Result ret = Result::ok;
	for(int i = 0; i < dataBytesPerPage; i+=256, p+=3) {
		YaffsEcc::calc(static_cast<unsigned char*>(buf) + i, read_ecc);
		Result r = YaffsEcc::correct(static_cast<unsigned char*>(buf), p, read_ecc);
		//ok < corrected < notcorrected
		if (r > ret)
			ret = r;
	}

	memcpy(data, buf, data_len);
	return ret;
}
Result SimuDriver::eraseBlock(uint32_t block_no){
	if(!cell)
		return Result::fail;
	if(block_no > blocksTotal){
		PAFFS_DBG(PAFFS_TRACE_BUG, "Tried erasing Block out of bounds!"
				"Was %" PRIu32 ", should < %" PRIu32, block_no, blocksTotal);
	}
	Nandaddress d = translateBlockToAddress(block_no, cell);
	return cell->eraseBlock(d.plane, d.block) == 0 ? Result::ok : Result::fail;
}
Result SimuDriver::markBad(uint32_t block_no){
	memset(buf, 0, totalBytesPerPage);
	for(unsigned page = 0; page < pagesPerBlock; page++){
		Nandaddress d = translatePageToAddress(block_no * pagesPerBlock + page, cell);
		if(cell->writePage(d.plane, d.block, d.page, buf) < 0){
			//ignore return Result::fail;
		}
	}
	return Result::ok;
}
Result SimuDriver::checkBad(uint32_t block_no){
	memset(buf, 0, totalBytesPerPage);
	for(unsigned page = 0; page < pagesPerBlock; page++){
		Nandaddress d = translatePageToAddress(block_no * pagesPerBlock + page, cell);
		if(cell->readPage(d.plane, d.block, d.page, buf) < 0){
			return Result::badflash;
		}
		if(buf[dataBytesPerPage + 5] == 0)
			return Result::badflash;
	}
	return Result::ok;
}

Result SimuDriver::writeMRAM(PageAbs startByte,
                             const void* data, unsigned int dataLen){
	unsigned const char* tmp = static_cast<unsigned const char*>(data);
	for(unsigned int i = 0; i < dataLen; i++){
		mram->setByte(startByte + i, tmp[i]);
	}
	return Result::ok;
}
Result SimuDriver::readMRAM(PageAbs startByte,
                            void* data, unsigned int dataLen){
	unsigned char *tmp = static_cast<unsigned char*>(data);
	for(unsigned int i = 0; i < dataLen; i++){
		tmp[i] = mram->getByte(startByte + i);
	}
	return Result::ok;
}

Nandaddress SimuDriver::translatePageToAddress(uint64_t page, FlashCell* fc){
	(void) fc;
	Nandaddress r;
	r.page = page % simu::pagesPerBlock;
	r.block = (page / simu::pagesPerBlock) % simu::blocksPerPlane;
	r.plane = (page / simu::pagesPerBlock) / simu::blocksPerPlane;
	return r;
}

Nandaddress SimuDriver::translateBlockToAddress(uint32_t block, FlashCell* fc){
	(void) fc;
	Nandaddress r;
	r.plane = block / simu::blocksPerPlane;
	r.block = block % simu::blocksPerPlane;
	r.page = 0;
	return r;
}

}
