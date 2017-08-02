/*
 * fc_driver.h
 *
 *  Created on: 21.06.2016
 *      Author: urinator
 */

#pragma once

#include <stddef.h>
#include <stdio.h>
#include "driver.hpp"


#include <amap.h>
#include <nand.h>
#include <outpost/hal/spacewire.h>
#include <spacewire_light.h>

namespace paffs{

using namespace outpost::hal;
using namespace outpost::iff;
using namespace outpost::leon3;

class OfficeModelNexys3Driver : public Driver{
	unsigned char buf[totalBytesPerPage];
	unsigned int bank, device;
	SpaceWireLight spacewire;
	Nand *nand;
	Amap *if_fpga;
public:
	OfficeModelNexys3Driver(unsigned int _bank, unsigned int _device)
			: bank(_bank), device(_device), spacewire(0){

		if(!initSpaceWire()){
			printf("Spacewire connection failed!");
		}
		if_fpga = new Amap(spacewire);
		nand = new Nand(*if_fpga, 0x00200000);
	}

	~OfficeModelNexys3Driver(){
		delete nand;
		delete if_fpga;
	}

	virtual Result initializeNand();
	virtual Result deInitializeNand();
	Result writePage(uint64_t page_no, void* data, unsigned int data_len);
	Result readPage(uint64_t page_no, void* data, unsigned int data_len);
	Result eraseBlock(uint32_t block_no);
	Result markBad(uint32_t block_no);
	Result checkBad(uint32_t block_no);
private:
	bool initSpaceWire();
};

}
