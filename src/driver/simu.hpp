/*
 * Copyright (c) 2017, German Aerospace Center (DLR)
 *
 * This file is part of the development version of OUTPOST.
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 *
 * Authors:
 * - 2017, Pascal Pieper (DLR RY-AVS)
 */
// ----------------------------------------------------------------------------

#pragma once

#include <simu/flashCell.hpp>
#include <simu/mram.hpp>
#include <stddef.h>
#include "driver.hpp"

namespace paffs{

class SimuDriver : public Driver{
	FlashCell *cell;
	Mram *mram;
	bool selfLoadedFlash = false;
	bool selfLoadedMRAM = false;
	unsigned char buf[totalBytesPerPage];
public:
	SimuDriver(){
		selfLoadedFlash = true;
		selfLoadedMRAM = true;
		cell = new FlashCell();
		mram = new Mram(mramSize);
	}
	SimuDriver(void *c){
		selfLoadedMRAM = true;
		cell = static_cast<FlashCell*>(c);
		mram = new Mram(mramSize);
	};
	SimuDriver(void *c, void *m){
		cell = static_cast<FlashCell*>(c);
		mram = static_cast<Mram*>(m);
	};

	~SimuDriver(){
		if(selfLoadedFlash)
			delete cell;
		if(selfLoadedMRAM)
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
