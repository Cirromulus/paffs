/*
 * driver.hpp
 *
 *  Created on: 17.01.2017
 *      Author: rooot
 */
#include "../paffs.hpp"
#pragma once

namespace paffs{

	class Driver {
	protected:
		Dev dev;
	public:
		Driver(){};
		virtual ~Driver();

		Dev* getDev(){
			return &dev;
		}

		virtual Result writePage (uint64_t page_no,
				void* data, unsigned int data_len) = 0;
		virtual Result readPage (uint64_t page_no,
				void* data, unsigned int data_len) = 0;
		virtual Result eraseBlock (uint32_t block_no) = 0;
		virtual Result markBad (uint32_t block_no) = 0;
		virtual Result checkBad (uint32_t block_no) = 0;
	};

}