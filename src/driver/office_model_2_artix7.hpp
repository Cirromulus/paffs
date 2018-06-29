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

#include <stddef.h>
#include <stdio.h>
#include "driver.hpp"


#include <spacewirelight.h>
#include <spacewire_light.h>
#include <amap.h>
#include <nand.h>
#include "../journalEntry.hpp"


namespace paffs{

using namespace outpost::hal;
using namespace outpost::iff;
using namespace outpost::leon3;

class OfficeModel2Artix7Driver : public Driver{
	uint8_t mBank, mDevice;
	SpaceWireLight mSpacewire;
	uint8_t mNandRaw[sizeof(Nand)];
	uint8_t mAmapRaw[sizeof(Amap)];
	Nand *mNand;
	Amap *mIfFpga;

	static constexpr volatile uint32_t* MCFG1 =
	        reinterpret_cast<uint32_t*>(0x80000000);  //Memory config register 1
	static constexpr uint8_t PROMwEnable = 11;  //PROM write enable bit
	static constexpr uint8_t* MRAMStartAddr =
	        reinterpret_cast<uint8_t*>(0x00000010);  //Reserve some Space for bootloader to fail

public:
	inline
	OfficeModel2Artix7Driver(uint8_t _bank, uint8_t _device)
			: mBank(_bank), mDevice(_device), mSpacewire(1){

		if(!initSpaceWire()){
			printf("Spacewire connection failed!");
		}
		mIfFpga = new(mNandRaw) Amap(mSpacewire);
		mNand = new(mAmapRaw) Nand(*mIfFpga, 0x00200000);

		//Enable MRAM write
		*MCFG1 |= 1 << PROMwEnable;
	}

	inline
	~OfficeModel2Artix7Driver(){};

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
	writeMRAM(PageAbs startByte, const void* data, uint32_t dataLen);
	Result
	readMRAM(PageAbs startByte, void* data, uint32_t dataLen);
private:
	bool initSpaceWire();
};

}
