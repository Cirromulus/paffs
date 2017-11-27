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

    ~NullDriver(){};

    virtual Result
    initializeNand() override;
    virtual Result
    deInitializeNand() override;
    Result
    writePage(PageAbs page, void* data, uint16_t dataLen) override;
    Result
    readPage(PageAbs page, void* data, uint16_t dataLen) override;
    Result
    eraseBlock(BlockAbs block) override;
    Result
    markBad(BlockAbs block) override;
    Result
    checkBad(BlockAbs block) override;
};

}
