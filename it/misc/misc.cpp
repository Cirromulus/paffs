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

#include <fstream>
#include <iostream>
#include <paffs.hpp>
#include <simu/flashCell.hpp>
#include <simu/mram.hpp>
#include "cmd.hpp"


using namespace paffs;
using namespace std;

static const char flashfile[] = "flash.bin";
static const char mramfile[] = "mram.bin";


int
main(int, char**)
{
    FlashCell* fc = new FlashCell();
    Mram* mram = new Mram(mramSize);

    //Read serialized flash and mram
    ifstream inff(flashfile,   ios::in | ios::binary);
    ifstream inmf(mramfile,    ios::in | ios::binary);
    if(inff.is_open() && inmf.is_open())
    {
        fc->getDebugInterface()->deserialize(inff);
        mram->deserialize(inmf);
        inff.close();
        inmf.close();
    }

    //init filesystem
    std::vector<paffs::Driver*> drv;
    drv.push_back(paffs::getDriverSpecial(0, fc, mram));
    BadBlockList bbl[maxNumberOfDevices];
    Paffs* fs = new Paffs(drv);

    CmdHandler cmd(fs, bbl  );
    cmd.prompt();

    ofstream ef(flashfile, ios::out | ios::binary);
    fc->getDebugInterface()->serialize(ef);
    ef.close();
    ofstream em(mramfile, ios::out | ios::binary);
    mram->serialize(em);
    em.close();
}
