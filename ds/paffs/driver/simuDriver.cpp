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
		return PAFFS_FAIL;

	void* buf = malloc(dev->param.total_bytes_per_page );

	if(dev->param.total_bytes_per_page != data_len){
		memset(buf, 0xFF, dev->param.total_bytes_per_page);
	}
	memcpy(buf, data, data_len);



	NANDADRESS d = translatePageToAddress(page_no, cell);

	if(fc->writePage(d.plane, d.block, d.page, (unsigned char*)buf) < 0){
		free(buf);
		return PAFFS_FAIL;
	}
	free(buf);
	return PAFFS_OK;
}
Result SimuDriver::readPage(uint64_t page_no,
								void* data, unsigned int data_len){
	if(!cell)
		return PAFFS_FAIL;

	unsigned char buf [dev->param.total_bytes_per_page];

	NANDADRESS d = translatePageToAddress(page_no, cell);

	if(fc->readPage(d.plane, d.block, d.page, (unsigned char*)buf) < 0){
		return PAFFS_FAIL;
	}
	memcpy(data, buf, data_len);
	return PAFFS_OK;
}
Result SimuDriver::eraseBlock(uint32_t block_no){
	if(!cell)
		return PAFFS_FAIL;

	NANDADRESS d = translateBlockToAddress(block_no, cell);

	return fc->eraseBlock(d.plane, d.block) == 0 ? PAFFS_OK : PAFFS_FAIL;
}
Result SimuDriver::markBad(uint32_t block_no){
	return PAFFS_NIMPL;
}

Result SimuDriver::checkBad(uint32_t block_no){
	return PAFFS_NIMPL;
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

}
