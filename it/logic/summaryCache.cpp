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
	static constexpr unsigned int numberOfBadBlocks = 4;
	static constexpr unsigned int numberOfDoubledBadBlocks = 1;
	static constexpr unsigned int numberOfNotifiedBlocks = 3;


	static std::vector<paffs::Driver*> &collectDrivers(){
		static std::vector<paffs::Driver*> drv;
		drv.clear();
		drv.push_back(paffs::getDriver(0));
		return drv;
	}
	paffs::BlockAbs allBadBlocks[paffs::maxNumberOfDevices][numberOfBadBlocks];
	paffs::BadBlockList bbl[paffs::maxNumberOfDevices];
public:
	paffs::Paffs fs;
	//automatically loads default driver "0" with default flash
	InitWithBadBlocks() : fs(collectDrivers()){
		for(unsigned int device = 0; device < paffs::maxNumberOfDevices; device++){
			for(unsigned block = 0; block < numberOfBadBlocks; block++){
				allBadBlocks[device][block] = (rand() %
						((paffs::areasNo - 1) * paffs::blocksPerArea))
						+ paffs::blocksPerArea;
			}
			for(unsigned i = 0; i < numberOfDoubledBadBlocks; i++){
				unsigned blockToBeDoubled = rand() % numberOfBadBlocks;
				unsigned blockToBeOverwritten = rand() % (numberOfBadBlocks - 1);
				if(blockToBeOverwritten == blockToBeDoubled){
					blockToBeOverwritten++;
				}
				allBadBlocks[device][blockToBeOverwritten] =
						allBadBlocks[device][blockToBeDoubled];
			}
			for(unsigned block = 0; block < numberOfBadBlocks; block++){
				if(block >= numberOfNotifiedBlocks){
					fs.getDevice(device)->driver->markBad(allBadBlocks[device][block]);
				}
			}
			printf("Device %u:\n\tListed, but not marked blocks:", device);
			for(unsigned i = 0; i < numberOfNotifiedBlocks; i++){
				printf(" %u", allBadBlocks[device][i]);
			}
			printf("\n\tMarked, but not listed blocks:");
			for(unsigned i = numberOfNotifiedBlocks; i < numberOfBadBlocks; i++){
				printf(" %u", allBadBlocks[device][i]);
			}
			printf("\n");
			bbl[device] = paffs::BadBlockList(allBadBlocks[device], numberOfNotifiedBlocks);
		}
	};

	void assertBadBlocksAsUnused(){
		for(unsigned device = 0; device < paffs::maxNumberOfDevices; device++){
			paffs::Device* dev = fs.getDevice(device);
			paffs::SimuDriver* drv = static_cast<paffs::SimuDriver*>(dev->driver);
			DebugInterface* dbg = drv->getDebugInterface();
			for(unsigned block = 0; block < numberOfBadBlocks; block++){
				AccessValues v = dbg->getAccessValues(
						allBadBlocks[device][block] / dbg->getPlaneSize(),
						allBadBlocks[device][block] % dbg->getPlaneSize(), 0, 0);

				//count how many times the current area was forced bad by list
				unsigned int areaAlreadyForcedBad = 0;
				unsigned int blockAlreadyForcedBad = 0;
				for(unsigned i = 0; i < numberOfBadBlocks; i++){
					if(allBadBlocks[device][i] / paffs::blocksPerArea
						== allBadBlocks[device][block] / paffs::blocksPerArea){
						areaAlreadyForcedBad ++;
					}
					if(allBadBlocks[device][i] == allBadBlocks[device][block]){
						blockAlreadyForcedBad ++;
					}
				}

				//never deleted
				ASSERT_EQ(v.times_reset, 0u);

				if(block < numberOfNotifiedBlocks){
					//notified blocks
					ASSERT_EQ(v.times_read, 0u);

				}else{
					//Self-detectable blocks
					if(areaAlreadyForcedBad > 1){
						//It was already set to bad, so no check for badblock marker.
						ASSERT_EQ(v.times_read, 0u);
					}else{
						ASSERT_EQ(v.times_read, 1u);
					}

				}
				//bad Block marker gets written
				//as many times as it got the list entry plus the tests own bbm
				ASSERT_EQ(v.times_written, blockAlreadyForcedBad);
			}
		}
	}

	virtual void SetUp(){
		fs.setTraceMask(
			PAFFS_TRACE_VERIFY_TC |
			PAFFS_TRACE_VERIFY_AS |
			PAFFS_TRACE_ERROR |
			PAFFS_TRACE_BUG |
			PAFFS_TRACE_BAD_BLOCKS
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
