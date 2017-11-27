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

	Result
	initializeNand() override;
	Result
	deInitializeNand() override;
	Result
	writePage(PageAbs pageNo, void* data, uint16_t dataLen) override;
	Result
	readPage(PageAbs pageNo, void* data, uint16_t dataLen) override;
	Result
	eraseBlock(BlockAbs blockNo) override;
	Result
	markBad(BlockAbs blockNo) override;
	Result
	checkBad(BlockAbs blockNo) override;
	Result
	writeMRAM(PageAbs startByte,
	          const void* data, uint32_t dataLen) override;
	Result
	readMRAM(PageAbs startByte,
	         void* data, uint32_t dataLen) override;
private:
	Nandaddress
	translatePageToAddress(PageAbs sector);

	Nandaddress
	translateBlockToAddress(BlockAbs block);
};

}
