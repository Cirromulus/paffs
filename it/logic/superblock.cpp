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
#include <stdio.h>
#include <paffs/config.hpp>

class SuperBlock : public InitFs{};

TEST_F(SuperBlock, multipleRemounts){
	paffs::Obj *fil;
	paffs::Dir *dir;
	paffs::Result r;
	char txt[] = "Hallo";
	char buf[6];
	unsigned int bw;

	fs.getDevice(0)->superblock.setTestmode(true);

	fil = fs.open("/file", paffs::FC);
	ASSERT_NE(fil, nullptr);
	fs.resetLastErr();

	r = fs.write(*fil, txt, strlen(txt), &bw);
	EXPECT_EQ(bw, strlen(txt));
	ASSERT_EQ(r, paffs::Result::ok);

	r = fs.close(*fil);
	ASSERT_EQ(r, paffs::Result::ok);

	r = fs.mkDir("/a", paffs::R | paffs::W);
	ASSERT_EQ(r, paffs::Result::ok);

	r = fs.unmount();
	ASSERT_EQ(r, paffs::Result::ok);

	r = fs.touch("/a");
	ASSERT_EQ(r, paffs::Result::notMounted);

	for(unsigned int i = 0; i < paffs::totalPagesPerArea + pow(paffs::totalPagesPerArea, paffs::superChainElems-1); i++){
		r = fs.mount();
		if(r != paffs::Result::ok){
			printf("Error during round %d!\n", i);
			printf("mount: %s\n", paffs::err_msg(r));
		}
		ASSERT_EQ(r, paffs::Result::ok);

		fil = fs.open("/file", paffs::FC);
		if(fil == nullptr){
			printf("Error in Run %u\n", i);
			printf("open: %s\n", paffs::err_msg(fs.getLastErr()));
		}
		ASSERT_NE(fil, nullptr);

		r = fs.read(*fil, buf, strlen(txt), &bw);
		ASSERT_EQ(r, paffs::Result::ok);
		ASSERT_EQ(bw, strlen(txt));
		ASSERT_TRUE(ArraysMatch(buf, txt, strlen(txt)));

		r = fs.touch("/a");
		if(r != paffs::Result::ok)
		{
			printf("(%d) Touch: %s!\n", i, paffs::err_msg(r));
		}
		ASSERT_EQ(r, paffs::Result::ok);

		r = fs.close(*fil);
		if(r != paffs::Result::ok)
		{
			printf("Close: %s!\n", paffs::err_msg(r));
		}
		ASSERT_EQ(r, paffs::Result::ok);

		dir = fs.openDir("/a");
		ASSERT_NE(dir, nullptr);

		r = fs.closeDir(dir);
		if(r != paffs::Result::ok)
		{
			printf("Close Dir: %s!\n", paffs::err_msg(r));
		}
		ASSERT_EQ(r, paffs::Result::ok);
		r = fs.unmount();
		if(r != paffs::Result::ok)
		{
			printf("Unmount: %s!\n", paffs::err_msg(r));
		}
		ASSERT_EQ(r, paffs::Result::ok);
	}
}
