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

//This seems to be discontinued in outpost?
//#include <outpost/nexys3/spacewire_light.h>
//#include <outpost/nexys3/sevensegment.h>
//#include <outpost/nexys3/gpio.h>

#include <amap.h>
#include <nand.h>
#include <spacewire_light.h>
#include <sevensegment.h>
#include <gpio.h>

namespace paffs{

using namespace outpost::hal;
using namespace outpost::iff;
using namespace outpost::leon3;

class OfficeModelNexys3Driver : public Driver{
	unsigned char *buf;
	unsigned int bank, device;
	SpaceWireLight spacewire;
	Nand *nand;
	Amap *if_fpga;
public:
	OfficeModelNexys3Driver(unsigned int _bank, unsigned int _device)
			: bank(_bank), device(_device), spacewire(0){
		//Configure parameters of flash
	    param.totalBytesPerPage = 4096+128;
	    param.oobBytesPerPage = 128;
	    param.pagesPerBlock = 0;
	    param.blocks = 0;

		buf = new unsigned char[param.totalBytesPerPage];

		initBoard();
		if(!initSpaceWire()){
			printf("Spacewire connection failed!");
		}

		if_fpga = new Amap(spacewire);
		nand = new Nand(*if_fpga, 0x00200000);
		nand->enableLatchUpProtection();
	}

	~OfficeModelNexys3Driver(){
		delete nand;
		delete if_fpga;
		delete[] buf;
	}

	Result writePage(uint64_t page_no, void* data, unsigned int data_len);
	Result readPage(uint64_t page_no, void* data, unsigned int data_len);
	Result eraseBlock(uint32_t block_no);
	Result markBad(uint32_t block_no);
	Result checkBad(uint32_t block_no);
private:
	bool initBoard();
	bool initSpaceWire();
};

}
