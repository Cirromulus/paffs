/*
 * superblock.cpp
 *
 *  Created on: 24 Feb 2017
 *      Author: rooot
 */

#include "commonTest.hpp"
#include <paffs.hpp>
#include <stdio.h>

class SummaryCache : public InitFs{};
using namespace testing;

TEST_F(SummaryCache, fillFlashAndVerify){
	paffs::Obj *fil;
	paffs::Result r;
	char txt[] = "Hallo";
	char buf[6];
	unsigned int bw;
	int i = 0, j = 0;
	char filename[50];
	bool full = false;
	//fill areas
	for(i = 0; !full; i++){
		sprintf(filename, "/%05d", i);
		r = fs.mkDir(filename, paffs::FC);
		if(r != paffs::Result::ok){
			ASSERT_THAT(r, AnyOf(Eq(paffs::Result::nosp), Eq(paffs::Result::toobig)));
			break;
		}
		for(j = 0; j < 100; j++){
			sprintf(filename, "/%05d/%02d", i, j);
			if(i == 4 && j == 97){
				printf("HERE\n");
			}
			fil = fs.open(filename, paffs::FC);
			if(fil == nullptr){
				std::cout << filename << ": " << paffs::err_msg(fs.getLastErr()) << std::endl;
				ASSERT_THAT(fs.getLastErr(), AnyOf(Eq(paffs::Result::nosp), Eq(paffs::Result::toobig)));
				full = true;
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
	}

	//check if valid
	for(i--;i >= 0; i--){
		for(j--; j >= 0; j--){
			sprintf(filename, "/%05d/%02d", i, j);
			fil = fs.open(filename, paffs::FC);
			ASSERT_EQ(fs.getLastErr(), paffs::Result::ok);

			r = fs.read(fil, buf, strlen(txt), &bw);
			EXPECT_EQ(bw, strlen(txt));
			ASSERT_EQ(r, paffs::Result::ok);
			ASSERT_TRUE(ArraysMatch(txt, buf, strlen(txt)));

			r = fs.close(fil);
			ASSERT_EQ(r, paffs::Result::ok);
		}
		j = 100;
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
