/*
 * driver.hpp
 *
 *  Created on: 17.01.2017
 *      Author: Pascal Pieper
 */

#include <commonTypes.hpp>
#include <outpost/rtos/timer.h>
#include <outpost/rtos/clock.h>
#pragma once

namespace paffs{

	class Driver {
	public:
		Driver(){};
		virtual ~Driver(){};

		virtual Result initializeNand() = 0;
		virtual Result deInitializeNand() = 0;
		virtual Result writePage (PageAbs pageNo,
				void* data, unsigned int dataLen) = 0;
		virtual Result readPage (PageAbs pageNo,
				void* data, unsigned int dataLen) = 0;
		virtual Result eraseBlock (BlockAbs blockNo) = 0;
		virtual Result markBad (BlockAbs blockNo) = 0;
		virtual Result checkBad (BlockAbs blockNo) = 0;

		virtual Result writeMRAM(PageAbs startByte,
				void* const data, unsigned int dataLen){
			(void) startByte;
			(void) data;
			(void) dataLen;
			return Result::nimpl;
		}
		virtual Result readMRAM(PageAbs startByte,
				void* data, unsigned int dataLen){
			(void) startByte;
			(void) data;
			(void) dataLen;
			return Result::nimpl;
		}
	};

	Driver* getDriver(const uint8_t deviceId);

	Driver* getDriverSpecial(const uint8_t deviceId, void* fc);
}
