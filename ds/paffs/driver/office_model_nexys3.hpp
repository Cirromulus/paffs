/*
 * fc_driver.h
 *
 *  Created on: 21.06.2016
 *      Author: urinator
 */

#pragma once

#include <stddef.h>
#include "driver.hpp"

//This seems to be discontinued in outpost?
//#include <outpost/nexys3/spacewire_light.h>
//#include <outpost/nexys3/sevensegment.h>
//#include <outpost/nexys3/gpio.h>

#include <amap.h>
#include <nand.h>
#include <outpost/hal/spacewire.h>

namespace paffs{

using namespace outpost::hal;
using namespace outpost::iff;


class OfficeModelNexys3Driver : public Driver{
	unsigned char *buf;
	SpaceWire spacewire;
	Nand *nand;
	Amap *if_fpga;
	unsigned int bank, device;
public:
	OfficeModelNexys3Driver(unsigned int _bank, unsigned int _device)
			: bank(_bank), device(_device){
		//Configure parameters of flash
	    param.totalBytesPerPage = cell->pageSize;
	    param.oobBytesPerPage = cell->pageSize - cell->pageDataSize;
	    param.pagesPerBlock = cell->blockSize;
	    param.blocks = cell->planeSize * cell->cellSize;

		buf = new unsigned char[param.totalBytesPerPage];

		initBoard();
		if(!initSpaceWire(&spacewire)){
			printf("Spacewire connection failed!");
		}

		if_fpga = new Amap(spacewire);
		pingAmap(if_fpga);
		nand = new Nand(if_fpga, 0x00200000);
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
	bool initSpaceWire(SpaceWire * spacewire);
};

}
