/*
 * fc_driver.h
 *
 *  Created on: 21.06.2016
 *      Author: urinator
 */

#pragma once

#include <stddef.h>
#include "driver.hpp"
#include "../../../simu/flashCell.h"

namespace paffs{

class SimuDriver : public Driver{
	FlashCell *cell;
	bool selfLoaded = false;
	void* buf;
public:
	SimuDriver() : Driver(){
		selfLoaded = true;
		cell = new FlashCell();
		init();
	}
	SimuDriver(void *c) : Driver(){
		cell = (FlashCell*) c;
		init();
	};

	~SimuDriver(){
		if(selfLoaded)
			delete cell;
		free(buf);
	}

	//See paffs.h struct p_drv
	Result writePage(uint64_t page_no, void* data, unsigned int data_len);
	Result readPage(uint64_t page_no, void* data, unsigned int data_len);
	Result eraseBlock(uint32_t block_no);
	Result markBad(uint32_t block_no);
	Result checkBad(uint32_t block_no);
private:
	NANDADRESS translatePageToAddress(uint64_t sector, FlashCell* fc);

	NANDADRESS translateBlockToAddress(uint32_t block, FlashCell* fc);

	void init();
};

}
