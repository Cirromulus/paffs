/*
 * treeCache.cpp
 *
 *  Created on: Feb 22, 2017
 *      Author: user
 */

#include <math.h>
#include <iostream>
#include "googletest/gtest/gtest.h"

#include "../paffs.hpp"

class TreeTest : public testing::Test{
public:
	//automatically loads default driver "1" with default flash
	paffs::Paffs fs;
	TreeTest(){
		paffs::Result r = fs.format("1");
		if(r != paffs::Result::ok)
			std::cerr << "Could not format device!" << std::endl;
	}
};

TEST_F (TreeTest, Sizes) {
	EXPECT_LE(sizeof(paffs::TreeNode), paffs::dataBytesPerPage);
	EXPECT_LE(paffs::leafOrder * sizeof(paffs::Inode), sizeof(paffs::TreeNode));
	EXPECT_LE(paffs::branchOrder * sizeof(paffs::Addr), sizeof(paffs::TreeNode));
}
