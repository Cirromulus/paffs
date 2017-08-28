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
		virtual Result writePage (PageAbs page_no,
				void* data, unsigned int data_len) = 0;
		virtual Result readPage (PageAbs page_no,
				void* data, unsigned int data_len) = 0;
		virtual Result eraseBlock (BlockAbs block_no) = 0;
		virtual Result markBad (BlockAbs block_no) = 0;
		virtual Result checkBad (BlockAbs block_no) = 0;
	};

	Driver* getDriver(const uint8_t deviceId);

	Driver* getDriverSpecial(const uint8_t deviceId, void* fc);
}
