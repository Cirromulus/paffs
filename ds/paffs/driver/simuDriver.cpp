/*
 * fc_driver.c
 *
 *  Created on: 21.06.2016
 *      Author: urinator
 */
#include "simuDriver.hpp"

#include "../paffs.hpp"
#include <string.h>



namespace paffs{


Result SimuDriver::writePage(uint64_t page_no,
								void* data, unsigned int data_len){
	if(!cell)
		return Result::fail;


	if(param.total_bytes_per_page != data_len){
		memset(buf, 0xFF, param.total_bytes_per_page);
	}
	memcpy(buf, data, data_len);



	NANDADRESS d = translatePageToAddress(page_no, cell);

	if(cell->writePage(d.plane, d.block, d.page, (unsigned char*)buf) < 0){
		free(buf);
		return Result::fail;
	}
	return Result::ok;
}
Result SimuDriver::readPage(uint64_t page_no,
								void* data, unsigned int data_len){
	if(!cell)
		return Result::fail;


	NANDADRESS d = translatePageToAddress(page_no, cell);

	if(cell->readPage(d.plane, d.block, d.page, (unsigned char*)buf) < 0){
		return Result::fail;
	}
	memcpy(data, buf, data_len);
	return Result::ok;
}
Result SimuDriver::eraseBlock(uint32_t block_no){
	if(!cell)
		return Result::fail;

	NANDADRESS d = translateBlockToAddress(block_no, cell);

	return cell->eraseBlock(d.plane, d.block) == 0 ? Result::ok : Result::fail;
}
Result SimuDriver::markBad(uint32_t block_no){
	return Result::nimpl;
}

Result SimuDriver::checkBad(uint32_t block_no){
	return Result::nimpl;
}

NANDADRESS SimuDriver::translatePageToAddress(uint64_t page, FlashCell* fc){
	NANDADRESS r;
	r.page = page % fc->blockSize;
	r.block = (page / fc->blockSize) % fc->planeSize;
	r.plane = (page / fc->blockSize) / fc->planeSize;
	return r;
}

NANDADRESS SimuDriver::translateBlockToAddress(uint32_t block, FlashCell* fc){
	NANDADRESS r;
	r.plane = block / fc->planeSize;
	r.block = block % fc->planeSize;
	r.page = 0;
	return r;
}

void SimuDriver::init(){
	buf = malloc(param.total_bytes_per_page);
	//TODO: Configure parameters of flash

}

}
