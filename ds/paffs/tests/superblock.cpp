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

	r = fs.unmount("1");
	ASSERT_EQ(r, paffs::Result::ok);

	r = fs.touch("/a");
	ASSERT_EQ(r, paffs::Result::notMounted);

	for(unsigned int i = 0; i < 3 * fs.getDevice()->param->totalPagesPerArea; i++){
		r = fs.mount("1");
		ASSERT_EQ(r, paffs::Result::ok);

		fil = fs.open("/file", paffs::FC);
		ASSERT_NE(fil, nullptr);
		fs.resetLastErr();
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

		r = fs.unmount("1");
		ASSERT_EQ(r, paffs::Result::ok);
	}
}

TEST_F(SuperBlock, fillAreas){
	paffs::Obj *fil;
	paffs::Result r;
	char txt[] = "Hallo";
	char buf[6];
	unsigned int bw;
	int i;
	char filename[11];

	//fill areas
	for(i = 0; ; i++){
		sprintf(filename, "/%09d", i);
		fil = fs.open(filename, paffs::FC);
		if(fil == nullptr){
			std::cout << paffs::err_msg(r) << std::endl;
			ASSERT_EQ(fs.getLastErr(), paffs::Result::nosp);
			break;
		}
		ASSERT_EQ(fs.getLastErr(), paffs::Result::ok);

		r = fs.write(fil, txt, strlen(txt), &bw);
		EXPECT_EQ(bw, strlen(txt));
		ASSERT_EQ(r, paffs::Result::ok);

		r = fs.close(fil);
		ASSERT_EQ(r, paffs::Result::ok);
	}

	//check if valid
	for(i--;i >= 0; i--){
		sprintf(filename, "/%09d", i);
		fil = fs.open(filename, paffs::FC);
		if(fil == nullptr){
			std::cout << paffs::err_msg(r) << std::endl;
			ASSERT_EQ(fs.getLastErr(), paffs::Result::nosp);
			break;
		}
		ASSERT_EQ(fs.getLastErr(), paffs::Result::ok);

		r = fs.read(fil, buf, strlen(txt), &bw);
		EXPECT_EQ(bw, strlen(txt));
		ASSERT_EQ(r, paffs::Result::ok);
		ASSERT_TRUE(ArraysMatch(txt, buf, strlen(txt)));

		r = fs.close(fil);
		ASSERT_EQ(r, paffs::Result::ok);
	}
}
