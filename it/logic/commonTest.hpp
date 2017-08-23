/*
 * commonTest.hpp
 *
 *  Created on: 24 Feb 2017
 *      Author: rooot
 */

#ifndef SOURCE_DIRECTORY__DS_PAFFS_TESTS_COMMONTEST_HPP_
#define SOURCE_DIRECTORY__DS_PAFFS_TESTS_COMMONTEST_HPP_

#include <paffs.hpp>
#include <gmock/gmock.h>
#include <gtest/gtest.h>

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
	};

	virtual void SetUp(){
		paffs::Result r = fs.format(true);
		fs.setTraceMask(
			PAFFS_TRACE_VERIFY_TC |
			PAFFS_TRACE_VERIFY_AS |
			PAFFS_TRACE_ERROR |
			PAFFS_TRACE_BUG
		);
		if(r != paffs::Result::ok)
			std::cerr << "Could not format device!" << std::endl;
		ASSERT_EQ(r, paffs::Result::ok);
		r = fs.mount();
		if(r != paffs::Result::ok)
			std::cerr << "Could not mount device!" << std::endl;
		ASSERT_EQ(r, paffs::Result::ok);
	}

	virtual void TearDown(){
		paffs::Result r = fs.unmount();
		ASSERT_THAT(r, testing::AnyOf(testing::Eq(paffs::Result::ok), testing::Eq(paffs::Result::notMounted)));
	}

	virtual ~InitFs(){}
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
