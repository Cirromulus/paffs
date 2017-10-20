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
		mram = new Mram(mramSize);
	}
	SimuDriver(void *c){
		cell = static_cast<FlashCell*>(c);
		mram = new Mram(mramSize);
	};
	SimuDriver(void *c, void *m){
		cell = static_cast<FlashCell*>(c);
		mram = static_cast<Mram*>(m);
	};

	~SimuDriver(){
		if(selfLoaded)
			delete cell;
		delete mram;
	}

	//DEBUG
	FlashDebugInterface* getDebugInterface(){
		return cell->getDebugInterface();
	}

	Result initializeNand() override;
	Result deInitializeNand() override;
	Result writePage(uint64_t page_no, void* data, unsigned int data_len) override;
	Result readPage(uint64_t page_no, void* data, unsigned int data_len) override;
	Result eraseBlock(uint32_t block_no) override;
	Result markBad(uint32_t block_no) override;
	Result checkBad(uint32_t block_no) override;
	Result writeMRAM(PageAbs startByte,
	                 const void* data, unsigned int dataLen) override;
	Result readMRAM(PageAbs startByte,
	                void* data, unsigned int dataLen) override;
private:
	Nandaddress translatePageToAddress(uint64_t sector, FlashCell* fc);

	Nandaddress translateBlockToAddress(uint32_t block, FlashCell* fc);
};

}
