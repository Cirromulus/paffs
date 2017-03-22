/*
 * driver.hpp
 *
 *  Created on: 17.01.2017
 *      Author: Pascal Pieper
 */
#include "../commonTypes.hpp"
#include <outpost/rtos/timer.h>
#include <outpost/rtos/clock.h>
#pragma once

namespace paffs{

	class Driver {
	public:
		Driver(){};
		virtual ~Driver(){};

		virtual Result writePage (uint64_t page_no,
				void* data, unsigned int data_len) = 0;
		virtual Result readPage (uint64_t page_no,
				void* data, unsigned int data_len) = 0;
		virtual Result eraseBlock (uint32_t block_no) = 0;
		virtual Result markBad (uint32_t block_no) = 0;
		virtual Result checkBad (uint32_t block_no) = 0;
	};

	Driver* getDriver(const uint8_t deviceId);

	Driver* getDriverSpecial(const uint8_t deviceId, void* fc);
}
