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

using namespace paffs;
using namespace std;

char filename[] = "/a.txt";
char exportFlash[] = "export_flash.bin";
char exportMram[] = "export_mram.bin";

void
exportLog();

void
import();

int
main(int argc, char** argv)
{
    (void)argv;

    if (argc > 1)
    {
        exportLog();
    }
    else
    {
        import();
    }
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
    if (!inf.is_open())
    {
        cout << "Serialized flash could not be opened" << endl;
        return;
    }
    fc->getDebugInterface()->deserialize(inf);
    inf.close();
    ifstream inm(exportMram, ios::in | ios::binary);
    if (!inm.is_open())
    {
        cout << "Serialized flash could not be opened" << endl;
        return;
    }
    mram->deserialize(inm);
    inm.close();

    Paffs fs(drv);
    fs.setTraceMask(fs.getTraceMask() | PAFFS_TRACE_JOURNAL | PAFFS_TRACE_VERBOSE);
    Result r = fs.mount();
    if (r != Result::ok)
    {
        cout << "Could not mount filesystem!" << endl;
        return;
    }

    fs.setTraceMask(fs.getTraceMask() & ~(PAFFS_TRACE_JOURNAL | PAFFS_TRACE_VERBOSE));

    ObjInfo info;
    r = fs.getObjInfo(filename, info);
    if (r != paffs::Result::ok)
    {
        cout << "could not get Info of file!" << endl;
    }
    Obj* fil = fs.open(filename, paffs::FR);
    if (fil == nullptr)
    {
        cout << "File could not be opened" << endl;
        return;
    }
    char text[info.size + 1];
    unsigned int br;
    r = fs.read(*fil, text, info.size, &br);
    if (r != paffs::Result::ok)
    {
        cout << "File could not be read!" << endl;
        return;
    }
    text[info.size] = 0;

    cout << "File contents:" << endl << text << endl;

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
    if (fil == nullptr)
    {
        cout << "File could not be opened" << endl;
        return;
    }
    char text[] = "Das Pferd frisst keinen Gurkensalat";
    unsigned int bw = 0;
    Result r = fs.write(*fil, text, sizeof(text), &bw);
    if (r != Result::ok)
    {
        cout << "File could not be written" << endl;
        return;
    }

    //---- Whoops, power went out! ----//

    ofstream ef(exportFlash, ios::out | ios::binary);
    fc->getDebugInterface()->serialize(ef);
    ef.close();
    ofstream em(exportMram, ios::out | ios::binary);
    mram->serialize(em);
    em.close();

    cout << "done" << endl;

    fs.close(*fil);
    fs.unmount();

    delete fc;
    delete mram;
}
