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

#include "paffs_trace.hpp"

#pragma once

namespace paffs
{
template <size_t size>
class BitList
{
    char list[size / 8 + 1];

public:
    BitList()
    {
        clear();
    }

    void
    clear()
    {
        memset(list, 0, sizeof(list));
    }

    void
    setBit(unsigned n)
    {
        if (n < size)
        {
            list[n / 8] |= 1 << n % 8;
        }
        else
        {
            PAFFS_DBG(PAFFS_TRACE_BUG, "Tried to set Bit at %u, but size is %zu", n, size);
        }
    }

    void
    resetBit(unsigned n)
    {
        if (n < size)
        {
            list[n / 8] &= ~(1 << n % 8);
        }
        else
        {
            PAFFS_DBG(PAFFS_TRACE_BUG, "Tried to reset Bit at %u, but size is %zu", n, size);
        }
    }

    bool
    getBit(unsigned n)
    {
        if (n < size)
        {
            return list[n / 8] & 1 << n % 8;
        }
        PAFFS_DBG(PAFFS_TRACE_BUG, "Tried to get Bit at %u, but size is %zu", n, size);
        return false;
    }

    size_t
    findFirstFree()
    {
        for (unsigned i = 0; i <= size / 8; i++)
        {
            if (list[i] != 0xFF)
            {
                for (unsigned int j = i * 8; j < (i + 1) * 8 && j < size; j++)
                {
                    if (!getBit(j))
                    {
                        return j;
                    }
                }
            }
        }
        return size;
    }

    bool
    isSetSomewhere()
    {
        for (unsigned i = 0; i <= size / 8; i++)
        {
            if (list[i])
            {
                return true;
            }
        }
        return false;
    }

    void
    printStatus()
    {
        for (unsigned i = 0; i < size; i++)
        {
            printf("%s", getBit(i) ? "1" : "0");
        }
        printf("\n");
    }

    bool
    operator==(BitList<size>& rhs) const
    {
        for (unsigned i = 0; i <= size / 8; i++)
        {
            if (list[i] != rhs.list[i])
            {
                return false;
            }
        }
        return true;
    }

    bool
    operator!=(BitList<size>& rhs) const
    {
        return !(*this == rhs);
    }
};
};
