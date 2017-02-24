/*
 * commonTest.hpp
 *
 *  Created on: 24 Feb 2017
 *      Author: rooot
 */

#ifndef SOURCE_DIRECTORY__DS_PAFFS_TESTS_COMMONTEST_HPP_
#define SOURCE_DIRECTORY__DS_PAFFS_TESTS_COMMONTEST_HPP_

#include "../paffs.hpp"
#include "googletest/gmock/gmock.h"
#include "googletest/gtest/gtest.h"

class InitFs : public testing::Test{
public:
	//automatically loads default driver "1" with default flash
	paffs::Paffs fs;
	InitFs(){
		fs.setTraceMask(0);
		paffs::Result r = fs.format("1");
				if(r != paffs::Result::ok)
					std::cerr << "Could not format device!" << std::endl;
		r = fs.mount("1");
				if(r != paffs::Result::ok)
					std::cerr << "Could not mount device!" << std::endl;
	}

	virtual ~InitFs(){
		fs.unmount("1");
	}
};

//stack overflow, Fraser '12
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
