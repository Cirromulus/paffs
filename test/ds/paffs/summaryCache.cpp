/*
 * superblock.cpp
 *
 *  Created on: 24 Feb 2017
 *      Author: rooot
 */

#define TEST_FRIENDS friend class SummaryCache_packedStatusIntegrity_Test;

#include "commonTest.hpp"
#include <paffs.hpp>
#include <stdio.h>

class SummaryCache : public InitFs{};

TEST_F(SummaryCache, fillFlashAndVerify){
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
			std::cout << paffs::err_msg(fs.getLastErr()) << std::endl;
			ASSERT_EQ(fs.getLastErr(), paffs::Result::nosp);
			break;
		}
		ASSERT_EQ(fs.getLastErr(), paffs::Result::ok);

		r = fs.write(fil, txt, strlen(txt), &bw);
		EXPECT_EQ(bw, strlen(txt));
		ASSERT_EQ(r, paffs::Result::ok);

		r = fs.close(fil);
		ASSERT_EQ(r, paffs::Result::ok);
		ASSERT_EQ(fs.getDevice()->tree.cache.isTreeCacheValid(), true);
	}

	//check if valid
	for(i--;i >= 0; i--){
		sprintf(filename, "/%09d", i);
		fil = fs.open(filename, paffs::FC);
		if(fil == nullptr){
			std::cout << paffs::err_msg(fs.getLastErr()) << std::endl;
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

/*
TEST_F(SummaryCache, packedStatusIntegrity){
	paffs::SummaryCache* c = &fs.getDevice()->sumCache;
	constexpr unsigned int size = paffs::dataPagesPerArea / 4 + 1;
	unsigned char buf[size] = {};
	paffs::SummaryEntry buf2[paffs::dataPagesPerArea] = {};

	for(paffs::AreaPos i = 0; i < paffs::areaSummaryCacheSize; i++){
		memset(c->summaryCache[i], 0, size);
	}
	c->setDirty(0);
	ASSERT_EQ(c->isDirty(0), true);
	ASSERT_TRUE(ArraysMatch(c->summaryCache[1], buf, size));

	c->setDirty(0, false);
	ASSERT_EQ(c->isDirty(0), false);

	for(unsigned int i = 0; i < paffs::dataPagesPerArea; i++){
		c->setPackedStatus(0, i, paffs::SummaryEntry::error);
	}
	for(unsigned int i = 0; i < paffs::dataPagesPerArea; i++){
		ASSERT_EQ(c->getPackedStatus(0, i), paffs::SummaryEntry::error);
	}
	c->unpackStatusArray(0, buf2);
	for(unsigned int i = 0; i < paffs::dataPagesPerArea; i++){
		ASSERT_EQ(buf2[i], paffs::SummaryEntry::error);
	}
	ASSERT_EQ(c->isDirty(0), false);

	c->setDirty(0);
	ASSERT_EQ(c->isDirty(0), true);
	ASSERT_TRUE(ArraysMatch(c->summaryCache[1], buf, size));
}*/
