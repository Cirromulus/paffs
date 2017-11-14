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
