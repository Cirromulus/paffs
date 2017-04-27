/*
 * fc_driver.c
 *
 *  Created on: 21.06.2016
 *      Author: urinator
 */
#include "office_model_nexys3.hpp"

#include "../commonTypes.hpp"
#include "../paffs_trace.hpp"
#include <string.h>

//debug
#include <rtems.h>
#include <rtems/stackchk.h>

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


Result OfficeModelNexys3Driver::writePage(uint64_t page_no,
								void* data, unsigned int data_len){
	if(data_len == 0 || data_len > totalBytesPerPage){
		PAFFS_DBG(PAFFS_TRACE_ERROR, "Invalid write size (%d)", data_len);
		return Result::einval;
	}

	rtems_stack_checker_report_usage();

	if(data_len != totalBytesPerPage){
		memset(buf, 0xFF, totalBytesPerPage);
	}
	memcpy(buf, data, data_len);
	printf("Write Page at %d, %d, page %d was omitted because of testing\n");
	//nand->writePage(bank, device, page_no, static_cast<uint8_t*>(data));
	return Result::ok;
}
Result OfficeModelNexys3Driver::readPage(uint64_t page_no,
								void* data, unsigned int data_len){
	PAFFS_DBG_S(PAFFS_TRACE_READ, "Trying to read %d bytes at page %lu ", data_len, page_no);
	if(data_len == 0 || data_len > totalBytesPerPage){
		PAFFS_DBG(PAFFS_TRACE_ERROR, "Invalid read size (%d)", data_len);
		return Result::einval;
	}

	PAFFS_DBG_S(PAFFS_TRACE_READ, "reading page...");

	rtems_stack_checker_report_usage();

	nand->readPage(bank, device, page_no, buf);

	rtems_stack_checker_report_usage();

	PAFFS_DBG_S(PAFFS_TRACE_READ, "Did read page");
	memcpy(data, buf, data_len);
	return Result::ok;
}
Result OfficeModelNexys3Driver::eraseBlock(uint32_t block_no){
	nand->eraseBlock(bank, device, block_no);
	return Result::nimpl;
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
