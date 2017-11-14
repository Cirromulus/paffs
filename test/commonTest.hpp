/*
 * Copyright (c) 2017, German Aerospace Center (DLR)
 *
 * This file is part of the development version of OUTPOST.
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 *
 * Authors:
 * - 2017, Pascal Pieper (DLR RY-AVS)
 */
// ----------------------------------------------------------------------------

#ifndef SOURCE_DIRECTORY__DS_PAFFS_TESTS_COMMONTEST_HPP_
#define SOURCE_DIRECTORY__DS_PAFFS_TESTS_COMMONTEST_HPP_

#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include <paffs.hpp>

// Source: stack overflow, Fraser '12
template <typename T, size_t size>
::testing::AssertionResult
ArraysMatch(const T (&expected)[size], const T (&actual)[size])
{
    for (size_t i(0); i < size; ++i)
    {
        if (expected[i] != actual[i])
        {
            return ::testing::AssertionFailure()
                   << "array[" << i << "] (" << actual[i] << ") != expected[" << i << "] ("
                   << expected[i] << ")";
        }
    }

    return ::testing::AssertionSuccess();
}

template <typename T>
::testing::AssertionResult
ArraysMatch(const T* expected, const T* actual, size_t size)
{
    for (size_t i(0); i < size; ++i)
    {
        if (expected[i] != actual[i])
        {
            return ::testing::AssertionFailure()
                   << "array[" << i << "] (" << actual[i] << ") != expected[" << i << "] ("
                   << expected[i] << ")";
        }
    }

    return ::testing::AssertionSuccess();
}

::testing::AssertionResult
StringsMatch(const char* a, const char* b);

#endif /* SOURCE_DIRECTORY__DS_PAFFS_TESTS_COMMONTEST_HPP_ */
