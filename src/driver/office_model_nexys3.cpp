/*
 * fc_driver.c
 *
 *  Created on: 21.06.2016
 *      Author: Pascal Pieper
 */
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

Driver* getDriver(const uint8_t deviceId){
	Driver* out = new OfficeModelNexys3Driver(0, deviceId);
	return out;
}

Driver* getDriverSpecial(const uint8_t deviceId, void* fc){
	(void) deviceId;
	(void) fc;
	printf("No Special parameter implemented!\n");
	return NULL;
}

Result OfficeModelNexys3Driver::initializeNand(){
	nand->enableLatchUpProtection();
	return Result::ok;
}
Result OfficeModelNexys3Driver::deInitializeNand(){
	//FIXME Does this actually turn off NAND?
	nand->disableLatchUpProtection();
	return Result::ok;
}


Result OfficeModelNexys3Driver::writePage(uint64_t page_no,
								void* data, unsigned int data_len){
	if(data_len == 0 || data_len > totalBytesPerPage){
		PAFFS_DBG(PAFFS_TRACE_ERROR, "Invalid write size (%d)", data_len);
		return Result::einval;
	}

	PAFFS_DBG_S(PAFFS_TRACE_WRITE, "Write %u bytes at page %" PRIu64, data_len, page_no);

	if(totalBytesPerPage != data_len){
		memset(buf+data_len, 0xFF, totalBytesPerPage - data_len);
	}
	memcpy(buf, data, data_len);

	unsigned char* p = &buf[dataBytesPerPage+2];
	for(int i = 0; i < dataBytesPerPage; i+=256, p+=3)
		YaffsEcc::calc(static_cast<unsigned char*>(buf) + i, p);

	nand->writePage(bank, device, page_no, static_cast<uint8_t*>(data));
	return Result::ok;
}
Result OfficeModelNexys3Driver::readPage(uint64_t page_no,
								void* data, unsigned int data_len){
	PAFFS_DBG_S(PAFFS_TRACE_READ, "Read %u bytes at page %" PRIu64, data_len, page_no);
	if(data_len == 0 || data_len > totalBytesPerPage){
		PAFFS_DBG(PAFFS_TRACE_ERROR, "Invalid read size (%u)", data_len);
		return Result::einval;
	}

	nand->readPage(bank, device, page_no, buf);
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
	(void) ret; //TODO: Return actual ECC result
	return Result::ok;
}
Result OfficeModelNexys3Driver::eraseBlock(uint32_t block_no){
	nand->eraseBlock(bank, device, block_no);
	return Result::ok;
}
Result OfficeModelNexys3Driver::markBad(uint32_t block_no){
	memset(buf, 0, totalBytesPerPage);
    for (size_t page = 0; page < 2; ++page){
        size_t pageNumber = block_no * pagesPerBlock + page;
        nand->writePage(bank, device, pageNumber, buf);
    }
    return Result::ok;
}

Result OfficeModelNexys3Driver::checkBad(uint32_t block_no){
    for (size_t page = 0; page < 2; ++page){
        size_t pageNumber = block_no * pagesPerBlock + page;
        nand->readPage(bank, device, pageNumber, buf);
        if (buf[4096] != 0xFF)
            return Result::badflash;
    }
	return Result::ok;
}


bool
OfficeModelNexys3Driver::initSpaceWire(){
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
