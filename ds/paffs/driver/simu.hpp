/*
 * fc_driver.h
 *
 *  Created on: 21.06.2016
 *      Author: urinator
 */

#pragma once

#include "../../../simu/flashCell.h"
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
		init();
	}
	SimuDriver(void *c){
		cell = static_cast<FlashCell*>(c);
		init();
	};

	~SimuDriver(){
		if(selfLoaded)
			delete cell;
	}

	//See paffs.h struct p_drv
	Result writePage(uint64_t page_no, void* data, unsigned int data_len);
	Result readPage(uint64_t page_no, void* data, unsigned int data_len);
	Result eraseBlock(uint32_t block_no);
	Result markBad(uint32_t block_no);
	Result checkBad(uint32_t block_no);
private:
	Nandaddress translatePageToAddress(uint64_t sector, FlashCell* fc);

	Nandaddress translateBlockToAddress(uint32_t block, FlashCell* fc);

	void init();
};

}
