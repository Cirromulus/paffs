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

#include "commonTest.hpp"
#include <paffs/config.hpp>
#include <stdio.h>
#include <fstream>
#include <iostream>
#include <simu/flashCell.hpp>
#include <simu/mram.hpp>

using namespace paffs;
using namespace std;

class JournalTest : public InitFs
{
};


static char filename[] = "/a.txt";
static char exportFlash[] = "export_flash.bin";
static char exportMram[] = "export_mram.bin";
static char text[] = "Das Pferd frisst keinen Gurkensalat";

void
exportLog();
void
import();

TEST_F(JournalTest, BreakAfterFileWrite)
{
    exportLog();
    import();
}

void
import()
{
    std::vector<paffs::Driver*> drv;
    FlashCell* fc = new FlashCell();
    Mram* mram = new Mram(mramSize);
    drv.push_back(paffs::getDriverSpecial(0, fc, mram));

    // Deserialize
    ifstream inf(exportFlash, ios::in | ios::binary);
    ASSERT_TRUE(inf.is_open());
    fc->getDebugInterface()->deserialize(inf);
    inf.close();
    ifstream inm(exportMram, ios::in | ios::binary);
    ASSERT_TRUE(inm.is_open());
    mram->deserialize(inm);
    inm.close();

    Paffs fs(drv);
    Result r = fs.mount();
    ASSERT_EQ(r, Result::ok);

    ObjInfo info;
    r = fs.getObjInfo(filename, info);
    ASSERT_EQ(r, Result::ok);

    Obj* fil = fs.open(filename, paffs::FR);
    ASSERT_NE(fil, nullptr);
    ASSERT_EQ(info.size, sizeof(text));

    char input[info.size + 1];
    unsigned int br;
    r = fs.read(*fil, input, info.size, &br);
    ASSERT_EQ(r, Result::ok);

    input[info.size] = 0;

    ASSERT_EQ(memcmp(input, text, sizeof(text)), 0);

    fs.close(*fil);
    fs.unmount();
}

void
exportLog()
{
    std::vector<paffs::Driver*> drv;
    FlashCell* fc = new FlashCell();
    Mram* mram = new Mram(mramSize);
    drv.push_back(paffs::getDriverSpecial(0, fc, mram));

    Paffs fs(drv);

    BadBlockList bbl[maxNumberOfDevices];
    fs.format(bbl);
    fs.setTraceMask(fs.getTraceMask()
                    | PAFFS_TRACE_ERROR
                    | PAFFS_TRACE_BUG
                    | PAFFS_TRACE_INFO
                    | PAFFS_TRACE_JOURNAL);

    fs.mount();

    Obj* fil = fs.open(filename, paffs::FW | paffs::FC);
    ASSERT_NE(fil, nullptr);
    unsigned int bw = 0;
    Result r = fs.write(*fil, text, sizeof(text), &bw);
    ASSERT_EQ(r, Result::ok);

    //---- Whoops, power went out! ----//

    ofstream ef(exportFlash, ios::out | ios::binary);
    fc->getDebugInterface()->serialize(ef);
    ef.close();
    ofstream em(exportMram, ios::out | ios::binary);
    mram->serialize(em);
    em.close();

    cout << endl << "OK" << endl;

    fs.close(*fil);
    fs.unmount();

    delete fc;
    delete mram;
}
