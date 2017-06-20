/*
 * superblock.cpp
 *
 *  Created on: 24 Feb 2017
 *      Author: rooot
 */

#include "commonTest.hpp"
#include <stdio.h>
#include <config.hpp>

class SuperBlock : public InitFs{};

TEST_F(SuperBlock, multipleRemounts){
	paffs::Obj *fil;
	paffs::Dir *dir;
	paffs::Result r;
	char txt[] = "Hallo";
	char buf[6];
	unsigned int bw;

	fs.getDevice()->superblock.setTestmode(true);

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

	for(unsigned int i = 0; i < paffs::totalPagesPerArea + pow(paffs::totalPagesPerArea, paffs::superChainElems-1); i++){
		r = fs.mount();
		if(r != paffs::Result::ok){
			printf("Error during round %d!\n", i);
		}
		ASSERT_EQ(r, paffs::Result::ok);

		fil = fs.open("/file", paffs::FC);
		ASSERT_NE(fil, nullptr);

		r = fs.read(fil, buf, strlen(txt), &bw);
		ASSERT_EQ(r, paffs::Result::ok);
		ASSERT_EQ(bw, strlen(txt));
		ASSERT_TRUE(ArraysMatch(buf, txt, strlen(txt)));

		//TODO: Activate
		//r = fs.touch("/a");
		//ASSERT_EQ(r, paffs::Result::ok);

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
