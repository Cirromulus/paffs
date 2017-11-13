/*
 * fc_driver.h
 *
 *  Created on: 21.06.2016
 *      Author: Pascal Pieper
 */

#pragma once

#include "driver.hpp"

namespace paffs{

class NullDriver : public Driver{
public:
	NullDriver(){};

	~NullDriver(){}

	virtual Result initializeNand();
	virtual Result deInitializeNand();
	Result writePage(uint64_t page_no, void* data, unsigned int data_len);
	Result readPage(uint64_t page_no, void* data, unsigned int data_len);
	Result eraseBlock(uint32_t block_no);
	Result markBad(uint32_t block_no);
	Result checkBad(uint32_t block_no);
};

}
