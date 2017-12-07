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
 * - 2017, Fabian Greif (DLR RY-AVS)
 */
// ----------------------------------------------------------------------------


#include "commonTest.hpp"

#include <iostream>
#include <stdlib.h> /* srand, rand */
#include <time.h>   /* time */
#include <memory>

class PageAddressCacheTest : public InitFs
{
};

using namespace paffs;

void seekAndWriteTo(Paffs& fs, Obj* fil, FileSize pos, const char* buf, FileSize len);

void seekAndReadCompare(Paffs& fs, Obj* fil, FileSize pos, char* buf, FileSize len, const char* cmp);

TEST_F(PageAddressCacheTest, seekReadWrite)
{
    /**
     * This test uses the behavior of Paffs to
     * jump over unused pages without writing them.
     * We can write a file at higher positions than the flash size would allow it.
     */
    Obj* fil;
    Result r;
    char txt[] = "This test uses the behavior of Paffs to "
                 "jump over unused pages without writing them.\n"
                 "We can write a file at higher positions than the flash size would allow it.";
    char buf[sizeof(txt)];


    //TODO: Jump to different indirections
    uint64_t direct     = 11 * dataBytesPerPage;
    uint64_t singlIndir = direct + static_cast<uint64_t>(addrsPerPage) * dataBytesPerPage;
    uint64_t doublIndir = singlIndir + (static_cast<uint64_t>(addrsPerPage) * addrsPerPage) * dataBytesPerPage;
    uint64_t triplIndir = doublIndir + (static_cast<uint64_t>(addrsPerPage) * addrsPerPage * addrsPerPage) * dataBytesPerPage;

    uint64_t maxAddressableBytes = static_cast<FileSize>(~static_cast<FileSize>(0));

    bool canTestSingl = singlIndir <= maxAddressableBytes;
    bool canTestDoubl = doublIndir <= maxAddressableBytes;
    bool canTestTripl = triplIndir <= maxAddressableBytes;

    if(!canTestSingl || !canTestDoubl || !canTestTripl)
    {
        fprintf(stderr, "Warning, can not test every indirection.\n");
        fprintf(stderr, "Addressable:  %" PRIu64 " Byte\n", maxAddressableBytes);
        fprintf(stderr, "First  Indir: %" PRIu64 " Byte\n", singlIndir);
        fprintf(stderr, "Second Indir: %" PRIu64 " Byte\n", doublIndir);
        fprintf(stderr, "Third  Indir: %" PRIu64 " Byte\n", triplIndir);
    }


    fil = fs.open("/file", paffs::FW | paffs::FC);
    if (fs.getLastErr() != Result::ok)
        printf("%s!\n", err_msg(fs.getLastErr()));
    ASSERT_NE(fil, nullptr);

    seekAndWriteTo(fs, fil, direct-strlen(txt), txt, strlen(txt));

    if(canTestSingl)
    {
        seekAndWriteTo(fs, fil, singlIndir-strlen(txt), txt, strlen(txt));
    }
    if(canTestDoubl)
    {
        seekAndWriteTo(fs, fil, doublIndir-strlen(txt), txt, strlen(txt));
    }
    if(canTestTripl)
    {
        seekAndWriteTo(fs, fil, triplIndir-strlen(txt), txt, strlen(txt));
    }

    r = fs.close(*fil);
    ASSERT_EQ(r, Result::ok);

    r = fs.unmount();
    ASSERT_EQ(r, paffs::Result::ok);
    r = fs.mount();
    ASSERT_EQ(r, paffs::Result::ok);

    fil = fs.open("/file", paffs::FW | paffs::FC);
    if (fs.getLastErr() != Result::ok)
        printf("%s!\n", err_msg(fs.getLastErr()));
    ASSERT_NE(fil, nullptr);

    seekAndReadCompare(fs, fil, direct-strlen(txt), buf, strlen(txt), txt);

    if(canTestSingl)
    {
        seekAndReadCompare(fs, fil, singlIndir-strlen(txt), buf, strlen(txt), txt);
    }
    if(canTestDoubl)
    {
        seekAndReadCompare(fs, fil, doublIndir-strlen(txt), buf, strlen(txt), txt);
    }
    if(canTestTripl)
    {
        seekAndReadCompare(fs, fil, triplIndir-strlen(txt), buf, strlen(txt), txt);
    }

    r = fs.close(*fil);
    ASSERT_EQ(r, Result::ok);

}

void seekAndWriteTo(Paffs& fs, Obj* fil, FileSize pos, const char* buf, FileSize len)
{
    Result r;
    FileSize bw;
    r = fs.seek(*fil, pos, Seekmode::set);
    ASSERT_EQ(r, Result::ok);

    r = fs.write(*fil, buf, len, &bw);
    EXPECT_EQ(bw, len);
    ASSERT_EQ(r, Result::ok);
}

void seekAndReadCompare(Paffs& fs, Obj* fil, FileSize pos, char* buf, FileSize len, const char* cmp)
{
    Result r;
    FileSize bw;
    r = fs.seek(*fil, pos, Seekmode::set);
    ASSERT_EQ(r, Result::ok);

    r = fs.read(*fil, buf, len, &bw);
    EXPECT_EQ(bw, len);
    ASSERT_EQ(r, Result::ok);
    ASSERT_TRUE(ArraysMatch(buf, cmp, len));
}
