/*
 * commonTest.hpp
 *
 *  Created on: 24 Feb 2017
 *      Author: rooot
 */

#ifndef SOURCE_DIRECTORY__DS_PAFFS_TESTS_COMMONTEST_HPP_
#define SOURCE_DIRECTORY__DS_PAFFS_TESTS_COMMONTEST_HPP_

#include <paffs.hpp>
#include "../../../ext/outpost-core/modules/utils/ext/googletest-1.8.0-fused/gmock/gmock.h"
#include "../../../ext/outpost-core/modules/utils/ext/googletest-1.8.0-fused/gtest/gtest.h"

class InitFs : public testing::Test{
	static std::vector<paffs::Driver*> &collectDrivers(){
		static std::vector<paffs::Driver*> drv;
		drv.clear();
		drv.push_back(paffs::getDriver(0));
		return drv;
	}
public:
	//automatically loads default driver "0" with default flash
	paffs::Paffs fs;
	InitFs() : fs(collectDrivers()){
		paffs::Result r = fs.format(true);
		fs.setTraceMask(
			PAFFS_TRACE_VERIFY_TC |
			PAFFS_TRACE_VERIFY_AS |
			PAFFS_TRACE_ERROR |
			PAFFS_TRACE_BUG
			//| PAFFS_TRACE_SUPERBLOCK | PAFFS_TRACE_VERBOSE
		);
		if(r != paffs::Result::ok)
			std::cerr << "Could not format device!" << std::endl;
		r = fs.mount();
		if(r != paffs::Result::ok)
			std::cerr << "Could not mount device!" << std::endl;
	}

	virtual ~InitFs(){
		fs.unmount();
	}
};

//Source: stack overflow, Fraser '12
template<typename T, size_t size>
::testing::AssertionResult ArraysMatch(const T (&expected)[size],
                                       const T (&actual)[size]){
    for (size_t i(0); i < size; ++i){
        if (expected[i] != actual[i]){
            return ::testing::AssertionFailure() << "array[" << i
                << "] (" << actual[i] << ") != expected[" << i
                << "] (" << expected[i] << ")";
        }
    }

    return ::testing::AssertionSuccess();
}

template<typename T>
::testing::AssertionResult ArraysMatch(const T* expected,
                                       const T* actual, size_t size){
    for (size_t i(0); i < size; ++i){
        if (expected[i] != actual[i]){
            return ::testing::AssertionFailure() << "array[" << i
                << "] (" << actual[i] << ") != expected[" << i
                << "] (" << expected[i] << ")";
        }
    }

    return ::testing::AssertionSuccess();
}

::testing::AssertionResult StringsMatch(const char *a, const char*b);

#endif /* SOURCE_DIRECTORY__DS_PAFFS_TESTS_COMMONTEST_HPP_ */
