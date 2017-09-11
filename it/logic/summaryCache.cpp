/*
 * superblock.cpp
 *
 *  Created on: 24 Feb 2017
 *      Author: rooot
 */

#include "commonTest.hpp"
#include <paffs.hpp>
#include "../../src/driver/simu.hpp"
#include <stdlib.h>
#include <stdio.h>

using namespace testing;

class SummaryCache : public InitFs{};

class InitWithBadBlocks : public testing::Test{
	static constexpr unsigned int numberOfBadBlocks = 3;
	static std::vector<paffs::Driver*> &collectDrivers(){
		static std::vector<paffs::Driver*> drv;
		drv.clear();
		drv.push_back(paffs::getDriver(0));
		return drv;
	}
public:
	//automatically loads default driver "0" with default flash
	paffs::Paffs fs;
	paffs::BadBlockList bbl[paffs::maxNumberOfDevices];
	InitWithBadBlocks() : fs(collectDrivers()){
		for(unsigned int i = 0; i < paffs::maxNumberOfDevices; i++){
			paffs::BlockAbs a,b,c;
			a = (rand() % ((paffs::areasNo - 1) * paffs::blocksPerArea)) + paffs::blocksPerArea;
			b = (rand() % ((paffs::areasNo - 1) * paffs::blocksPerArea)) + paffs::blocksPerArea;
			c = (rand() % ((paffs::areasNo - 1) * paffs::blocksPerArea)) + paffs::blocksPerArea;
			paffs::BlockAbs blocks[] = {a, b, c};
			printf("Device %u, marked bad blocks at %u, %u and %u\n", i, a, b, c);
			bbl[i] = paffs::BadBlockList(blocks, numberOfBadBlocks);
		}
	};

	void assertBadBlocksAsUnused(){
		for(unsigned i = 0; i < paffs::maxNumberOfDevices; i++){
			paffs::Device* dev = fs.getDevice(i);
			paffs::SimuDriver* drv = static_cast<paffs::SimuDriver*>(dev->driver);
			DebugInterface* dbg = drv->getDebugInterface();
			for(unsigned block = 0; block < bbl[i].mSize; block++){
				AccessValues v = dbg->getAccessValues(
						bbl[i].mList[block] / dbg->getPlaneSize(),
						bbl[i].mList[block] % dbg->getPlaneSize(), 0, 0);
				ASSERT_EQ(v.times_read, 0u);
				ASSERT_EQ(v.times_reset, 0u);
				ASSERT_EQ(v.times_written, 0u);
			}
		}
	}

	virtual void SetUp(){
		fs.setTraceMask(
			PAFFS_TRACE_VERIFY_TC |
			PAFFS_TRACE_VERIFY_AS |
			PAFFS_TRACE_ERROR |
			PAFFS_TRACE_BUG
		);
		paffs::Result r = fs.format(bbl, true);
		if(r != paffs::Result::ok){
			std::cerr << "Could not format device!" << std::endl;
		}
		ASSERT_EQ(r, paffs::Result::ok);
		r = fs.mount();
		if(r != paffs::Result::ok){
			std::cerr << "Could not mount device!" << std::endl;
		}
		ASSERT_EQ(r, paffs::Result::ok);
		assertBadBlocksAsUnused();
	}

	virtual void TearDown(){
		paffs::Result r = fs.unmount();
		ASSERT_THAT(r, testing::AnyOf(testing::Eq(paffs::Result::ok), testing::Eq(paffs::Result::notMounted)));
		assertBadBlocksAsUnused();
	}

	virtual ~InitWithBadBlocks(){};
};

void fillFlashAndVerify(paffs::Paffs &fs);

TEST_F(SummaryCache, fillFlashAndVerify){
	fillFlashAndVerify(fs);
}

TEST_F(InitWithBadBlocks, fillFlashAndVerifyWithBadBlocks){
	fillFlashAndVerify(fs);
}

void fillFlashAndVerify(paffs::Paffs &fs){
	paffs::Obj *fil;
	paffs::Result r;
	char txt[] = "Hallo";
	char buf[6];
	unsigned int bw;
	int i = 0, j = 0;
	char filename[50];
	bool full = false;

	fs.setTraceMask(fs.getTraceMask()
			| PAFFS_WRITE_VERIFY_AS
			| PAFFS_TRACE_VERIFY_AS
		);

	//fill areas
	for(i = 0; !full; i++){
		sprintf(filename, "/%05d", i);
		r = fs.mkDir(filename, paffs::FC);
		if(r != paffs::Result::ok){
			ASSERT_THAT(r, AnyOf(Eq(paffs::Result::nospace), Eq(paffs::Result::toobig)));
			break;
		}
		for(j = 0; j < 100; j++){
			sprintf(filename, "/%05d/%02d", i, j);
			fil = fs.open(filename, paffs::FC);
			if(fil == nullptr){
				std::cout << filename << ": " << paffs::err_msg(fs.getLastErr()) << std::endl;
				ASSERT_THAT(fs.getLastErr(), AnyOf(Eq(paffs::Result::nospace), Eq(paffs::Result::toobig)));
				full = true;
				break;
			}
			ASSERT_EQ(fs.getLastErr(), paffs::Result::ok);

			r = fs.write(fil, txt, strlen(txt), &bw);
			if(r == paffs::Result::nospace){
				full = true;
				break;
			}
			ASSERT_EQ(r, paffs::Result::ok);
			EXPECT_EQ(bw, strlen(txt));

			r = fs.close(fil);
			ASSERT_EQ(r, paffs::Result::ok);
			ASSERT_EQ(fs.getDevice(0)->tree.cache.isTreeCacheValid(), true);
		}
	}
	int ic = i, jc = j;
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

/*	fs.setTraceMask(fs.getTraceMask()
			| PAFFS_WRITE_VERIFY_AS
			| PAFFS_TRACE_ASCACHE
			| PAFFS_TRACE_AREA
			| PAFFS_TRACE_SUPERBLOCK
			| PAFFS_TRACE_VERBOSE);*/

	ASSERT_EQ(r, paffs::Result::ok);
	r = fs.unmount();
	ASSERT_EQ(r, paffs::Result::ok);
	r = fs.mount();
	ASSERT_EQ(r, paffs::Result::ok);

	//second check, after remount
	i = ic;
	j = jc;

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
