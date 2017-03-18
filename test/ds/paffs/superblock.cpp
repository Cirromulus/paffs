/*
 * superblock.cpp
 *
 *  Created on: 24 Feb 2017
 *      Author: rooot
 */

#include "commonTest.hpp"
#include <stdio.h>

class SuperBlock : public InitFs{};

TEST_F(SuperBlock, multipleRemounts){
	paffs::Obj *fil;
	paffs::Dir *dir;
	paffs::Result r;
	char txt[] = "Hallo";
	char buf[6];
	unsigned int bw;

	fil = fs.open("/file", paffs::FC);
	ASSERT_NE(fil, nullptr);
	fs.resetLastErr();

	r = fs.write(fil, txt, strlen(txt), &bw);
	EXPECT_EQ(bw, strlen(txt));
	ASSERT_EQ(r, paffs::Result::ok);

	r = fs.close(fil);
	ASSERT_EQ(r, paffs::Result::ok);

	r = fs.mkDir("/a", paffs::R | paffs::W);
	ASSERT_EQ(r, paffs::Result::ok);

	r = fs.unmount();
	ASSERT_EQ(r, paffs::Result::ok);

	r = fs.touch("/a");
	ASSERT_EQ(r, paffs::Result::notMounted);

	for(unsigned int i = 0; i < 3 * fs.getDevice()->param->totalPagesPerArea; i++){
		r = fs.mount();
		ASSERT_EQ(r, paffs::Result::ok);

		fil = fs.open("/file", paffs::FC);
		ASSERT_NE(fil, nullptr);

		r = fs.read(fil, buf, strlen(txt), &bw);
		ASSERT_EQ(r, paffs::Result::ok);
		ASSERT_EQ(bw, strlen(txt));
		ASSERT_TRUE(ArraysMatch(buf, txt, strlen(txt)));

		r = fs.close(fil);
		ASSERT_EQ(r, paffs::Result::ok);

		dir = fs.openDir("/a");
		ASSERT_NE(dir, nullptr);

		r = fs.closeDir(dir);
		ASSERT_EQ(r, paffs::Result::ok);

		r = fs.unmount();
		ASSERT_EQ(r, paffs::Result::ok);
	}
}
