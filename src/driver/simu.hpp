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
	Mram *mram;
	bool selfLoaded = false;
	unsigned char buf[totalBytesPerPage];
public:
	SimuDriver(){
		selfLoaded = true;
		cell = new FlashCell();
		mram = new Mram(4096 * 512);
	}
	SimuDriver(void *c){
		cell = static_cast<FlashCell*>(c);
		mram = new Mram(4096 * 512);
	};

	~SimuDriver(){
		if(selfLoaded)
			delete cell;
		delete mram;
	}

	//DEBUG
	DebugInterface* getDebugInterface(){
		return cell->getDebugInterface();
	}

	Result initializeNand();
	Result deInitializeNand();
	Result writePage(uint64_t page_no, void* data, unsigned int data_len) override;
	Result readPage(uint64_t page_no, void* data, unsigned int data_len) override;
	Result eraseBlock(uint32_t block_no) override;
	Result markBad(uint32_t block_no) override;
	Result checkBad(uint32_t block_no) override;
	Result writeMRAM(PageAbs startByte,
	                 void* const data, unsigned int dataLen) override;
	Result readMRAM(PageAbs startByte,
	                void* data, unsigned int dataLen) override;
private:
	Nandaddress translatePageToAddress(uint64_t sector, FlashCell* fc);

	Nandaddress translateBlockToAddress(uint32_t block, FlashCell* fc);
};

}
