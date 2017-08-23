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

	fs.setTraceMask(fs.getTraceMask()
			| PAFFS_WRITE_VERIFY_AS
			| PAFFS_TRACE_VERIFY_AS
		);

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
			fil = fs.open(filename, paffs::FC);
			if(fil == nullptr){
				std::cout << filename << ": " << paffs::err_msg(fs.getLastErr()) << std::endl;
				ASSERT_THAT(fs.getLastErr(), AnyOf(Eq(paffs::Result::nosp), Eq(paffs::Result::toobig)));
				full = true;
				break;
			}
			ASSERT_EQ(fs.getLastErr(), paffs::Result::ok);

			r = fs.write(fil, txt, strlen(txt), &bw);
			if(r == paffs::Result::nosp){
				full = true;
				break;
			}
			ASSERT_EQ(r, paffs::Result::ok);
			EXPECT_EQ(bw, strlen(txt));

			r = fs.close(fil);
			ASSERT_EQ(r, paffs::Result::ok);
			ASSERT_EQ(fs.getDevice()->tree.cache.isTreeCacheValid(), true);
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
