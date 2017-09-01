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

#ifdef __CDT_PARSER__
#	define TEST void test
#endif

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
