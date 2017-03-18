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



namespace paffs{

outpost::rtos::SystemClock systemClock;

Driver* getDriver(const uint8_t deviceId){
	Driver* out = new OfficeModelNexys3Driver();
	out->param.deviceId = deviceId;
	return out;
}

Driver* getDriverSpecial(const uint8_t deviceId, void* fc){
	std::cerr << "No Special parameter implemented!" << std::endl;
	return NULL;
}


Result OfficeModelNexys3Driver::writePage(uint64_t page_no,
								void* data, unsigned int data_len){
	if(param.totalBytesPerPage != data_len){
		memset(buf, 0xFF, param.totalBytesPerPage);
	}
	memcpy(buf, data, data_len);
	nand->writePage(bank, device, page_no, data);
	return Result::ok;
}
Result OfficeModelNexys3Driver::readPage(uint64_t page_no,
								void* data, unsigned int data_len){

	nand->readPage(bank, device, page_no, buf);
	memcpy(data, buf, data_len);
	return Result::ok;
}
Result OfficeModelNexys3Driver::eraseBlock(uint32_t block_no){
	nand->eraseBlock(bank, device, block_no);
	return Result::nimpl;
}
Result OfficeModelNexys3Driver::markBad(uint32_t block_no){
	memset(buf, 0, param.totalBytesPerPage);
    for (size_t page = 0; page < 2; ++page){
        size_t pageNumber = block_no * param.pagesPerBlock + page;
        nand->writePage(bank, device, pageNumber, buf);
    }
    return Result::ok;
}

Result OfficeModelNexys3Driver::checkBad(uint32_t block_no){
    for (size_t page = 0; page < 2; ++page){
        size_t pageNumber = block_no * param.pagesPerBlock + page;
        nand->readPage(bank, device, pageNumber, buf);
        if (buf[4096] != 0xFF)
            return Result::badflash;
    }
	return Result::ok;
}


bool OfficeModelNexys3Driver::initBoard(){
	SevenSegment::clear();
	SevenSegment::write(0, 'P');
	SevenSegment::write(1, 'A');
	SevenSegment::write(2, 'F');
	SevenSegment::write(3, 'S');

	uint8_t leds = 0;
	for (uint_fast8_t i = 0; i < 12; ++i)
	{
		leds <<= 1;

		if (i <= 3)
		{
			leds |= 1;
		}
		Gpio::set(leds);
		rtems_task_wake_after(50);
	}
}

bool
initSpaceWire(cobc::hal::SpaceWire * spacewire){
    if (!spacewire->open()){
    	printf("Spacewire connection opening .. ");
        printf("failed\n");
        return false;
    }

    spacewire->up(cobc::time::Milliseconds(0));


    if (!spacewire->isUp()){
    	printf("check link .. ");
        printf("down\n");
        return false;
    }
    return true;
}
}
