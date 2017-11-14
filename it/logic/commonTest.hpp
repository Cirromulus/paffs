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

#pragma once

#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include <paffs.hpp>

class InitFs : public testing::Test
{
    static std::vector<paffs::Driver*>&
    collectDrivers()
    {
        static std::vector<paffs::Driver*> drv;
        drv.clear();
        drv.push_back(paffs::getDriver(0));
        return drv;
    }

public:
    // automatically loads default driver "0" with default flash
    paffs::Paffs fs;
    InitFs() : fs(collectDrivers()){};

    virtual void
    SetUp()
    {
        paffs::BadBlockList bbl[paffs::maxNumberOfDevices];  // Empty
        paffs::Result r = fs.format(bbl);
        fs.setTraceMask(PAFFS_TRACE_VERIFY_TC | PAFFS_TRACE_VERIFY_AS | PAFFS_TRACE_BUG
                        | PAFFS_TRACE_ERROR);
        if (r != paffs::Result::ok)
            std::cerr << "Could not format device!" << std::endl;
        ASSERT_EQ(r, paffs::Result::ok);
        r = fs.mount();
        if (r != paffs::Result::ok)
            std::cerr << "Could not mount device!" << std::endl;
        ASSERT_EQ(r, paffs::Result::ok);
    }

    virtual void
    TearDown()
    {
        EXPECT_EQ(fs.getDevice(0)->getNumberOfOpenFiles(), 0u);
        EXPECT_EQ(fs.getDevice(0)->getNumberOfOpenInodes(), 0u);
        paffs::Result r = fs.unmount();
        ASSERT_THAT(r,
                    testing::AnyOf(testing::Eq(paffs::Result::ok),
                                   testing::Eq(paffs::Result::notMounted)));
    }

    virtual ~InitFs()
    {
    }
};

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
