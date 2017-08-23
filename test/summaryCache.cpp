/*
 * superblock.cpp
 *
 *  Created on: 24 Feb 2017
 *      Author: rooot
 */

#include "commonTest.hpp"
#include <paffs.hpp>
#include <stdio.h>

using namespace testing;

TEST(SummaryCacheElem, packedStatusIntegrity){
	paffs::AreaSummaryElem asElem;

	//TODO: Test other features like no clear if dirty etc.
	asElem.setArea(0);
	for(unsigned i = 0; i < paffs::dataPagesPerArea; i++){
		ASSERT_EQ(asElem.getStatus(i), paffs::SummaryEntry::free);
	}
	ASSERT_EQ(asElem.isDirty(), false);
	ASSERT_EQ(asElem.isAsWritten(), false);
	ASSERT_EQ(asElem.isLoadedFromSuperPage(), false);

	asElem.setDirty();
	ASSERT_EQ(asElem.isDirty(), true);
	ASSERT_EQ(asElem.isAsWritten(), false);
	ASSERT_EQ(asElem.isLoadedFromSuperPage(), false);

	asElem.setDirty(false);
	ASSERT_EQ(asElem.isDirty(), false);

	asElem.setAsWritten();
	ASSERT_EQ(asElem.isDirty(), false);
	ASSERT_EQ(asElem.isAsWritten(), true);
	ASSERT_EQ(asElem.isLoadedFromSuperPage(), false);

	asElem.setAsWritten(false);
	ASSERT_EQ(asElem.isAsWritten(), false);

	asElem.setLoadedFromSuperPage();
	ASSERT_EQ(asElem.isDirty(), false);
	ASSERT_EQ(asElem.isAsWritten(), false);
	ASSERT_EQ(asElem.isLoadedFromSuperPage(), true);

	for(unsigned int i = 0; i < paffs::dataPagesPerArea; i++){
		asElem.setStatus(i, paffs::SummaryEntry::error);
		for(unsigned int j = 0; j < paffs::dataPagesPerArea; j++){
			if(j == i){
				ASSERT_EQ(asElem.getStatus(j), paffs::SummaryEntry::error);
				continue;
			}
			ASSERT_EQ(asElem.getStatus(j), paffs::SummaryEntry::free);
		}
		asElem.setStatus(i, paffs::SummaryEntry::free);
	}

	//It should set itself Dirty when a setStatus occurs.
	ASSERT_EQ(asElem.isDirty(), true);

	//It should reset |loadedFromSuperPage| when a setStatus occurs.
	ASSERT_EQ(asElem.isLoadedFromSuperPage(), false);
	asElem.setDirty(false);
}
