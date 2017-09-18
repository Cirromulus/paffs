/*
 * fc_driver.h
 *
 *  Created on: 21.06.2016
 *      Author: urinator
 */

#pragma once

#include <simu/flashCell.h>
#include <simu/mram.hpp>
#include <stddef.h>
#include "driver.hpp"

namespace paffs{

class SimuDriver : public Driver{
	FlashCell *cell;
	bool selfLoaded = false;
	unsigned char buf[totalBytesPerPage];
public:
	SimuDriver(){
		selfLoaded = true;
		cell = new FlashCell();
	}
	SimuDriver(void *c){
		cell = static_cast<FlashCell*>(c);
	};

	~SimuDriver(){
		if(selfLoaded)
			delete cell;
	}

	//DEBUG
	DebugInterface* getDebugInterface(){
		return cell->getDebugInterface();
	}

	virtual Result initializeNand();
	virtual Result deInitializeNand();
	Result writePage(uint64_t page_no, void* data, unsigned int data_len);
	Result readPage(uint64_t page_no, void* data, unsigned int data_len);
	Result eraseBlock(uint32_t block_no);
	Result markBad(uint32_t block_no);
	Result checkBad(uint32_t block_no);
private:
	Nandaddress translatePageToAddress(uint64_t sector, FlashCell* fc);

	Nandaddress translateBlockToAddress(uint32_t block, FlashCell* fc);
};

}