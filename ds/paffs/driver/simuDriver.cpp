/*
 * fc_driver.c
 *
 *  Created on: 21.06.2016
 *      Author: urinator
 */
#include "simuDriver.hpp"

#include "../commonTypes.hpp"
#include "../paffs_trace.hpp"
#include <string.h>



namespace paffs{


Result SimuDriver::writePage(uint64_t page_no,
								void* data, unsigned int data_len){
	if(!cell)
		return Result::fail;

	if(data_len > param.totalBytesPerPage){
		PAFFS_DBG(PAFFS_TRACE_BUG, "Tried to write %u Bytes to a page of %u!", data_len, param.totalBytesPerPage);
		return Result::fail;
	}


	if(param.totalBytesPerPage != data_len){
		memset(buf, 0xFF, param.totalBytesPerPage);
	}
	memcpy(buf, data, data_len);

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
	memcpy(data, buf, data_len);
	return Result::ok;
}
Result SimuDriver::eraseBlock(uint32_t block_no){
	if(!cell)
		return Result::fail;

	Nandaddress d = translateBlockToAddress(block_no, cell);
	return cell->eraseBlock(d.plane, d.block) == 0 ? Result::ok : Result::fail;
}
Result SimuDriver::markBad(uint32_t block_no){
	memset(buf, 0, param.totalBytesPerPage);
	for(unsigned page = 0; page < param.pagesPerBlock; page++){
		Nandaddress d = translatePageToAddress(block_no * param.pagesPerBlock + page, cell);
		if(cell->writePage(d.plane, d.block, d.page, buf) < 0){
			//ignore return Result::fail;
		}
	}
	return Result::ok;
}

Result SimuDriver::checkBad(uint32_t block_no){
	(void) block_no;
	return Result::nimpl;
}

Nandaddress SimuDriver::translatePageToAddress(uint64_t page, FlashCell* fc){
	Nandaddress r;
	r.page = page % fc->blockSize;
	r.block = (page / fc->blockSize) % fc->planeSize;
	r.plane = (page / fc->blockSize) / fc->planeSize;
	return r;
}

Nandaddress SimuDriver::translateBlockToAddress(uint32_t block, FlashCell* fc){
	Nandaddress r;
	r.plane = block / fc->planeSize;
	r.block = block % fc->planeSize;
	r.page = 0;
	return r;
}

void SimuDriver::init(){
	//Configure parameters of flash
    param.totalBytesPerPage = cell->pageSize;
    param.oobBytesPerPage = cell->pageSize - cell->pageDataSize;
    param.pagesPerBlock = cell->blockSize;
    param.blocks = cell->planeSize * cell->cellSize;

	buf = new unsigned char[param.totalBytesPerPage];
	memset(buf, 0xFF, param.totalBytesPerPage);
}

}
