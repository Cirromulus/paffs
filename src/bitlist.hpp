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
template <size_t numberOfElements>
class BitList
{
    char mList[(numberOfElements + 7) / 8];

public:
    inline
    BitList()
    {
        clear();
    }

    inline void
    clear()
    {
        memset(mList, 0, sizeof(mList));
    }

    static inline void
    setBit(unsigned n, char* list)
    {
        if (n < numberOfElements)
        {
            list[n / 8] |= 1 << n % 8;
        }
        else
        {
            PAFFS_DBG(PAFFS_TRACE_BUG, "Tried to set Bit at %" PRIu16 ", but size is %zu", n, numberOfElements);
        }
    }

    inline void
    setBit(unsigned n)
    {
        setBit(n, mList);
    }

    static inline void
    resetBit(unsigned n, char* list)
    {
        if (n < numberOfElements)
        {
            list[n / 8] &= ~(1 << n % 8);
        }
        else
        {
            PAFFS_DBG(PAFFS_TRACE_BUG, "Tried to reset Bit at %" PRIu16 ", but size is %zu", n, numberOfElements);
        }
    }

    inline void
    resetBit(unsigned n)
    {
        resetBit(n, mList);
    }

    static inline bool
    getBit(unsigned n, const char* list)
    {
        if (n < numberOfElements)
        {
            return list[n / 8] & 1 << n % 8;
        }
        PAFFS_DBG(PAFFS_TRACE_BUG, "Tried to get Bit at %" PRIu16 ", but size is %zu", n, numberOfElements);
        return false;
    }

    inline bool
    getBit(unsigned n)
    {
        return getBit(n, mList);
    }

    static inline size_t
    findFirstFree(const char* list)
    {
        for (unsigned i = 0; i <= numberOfElements / 8; i++)
        {
            if (list[i] != 0xFF)
            {
                for (unsigned int j = i * 8; j < (i + 1) * 8 && j < numberOfElements; j++)
                {
                    if (!getBit(j, list))
                    {
                        return j;
                    }
                }
            }
        }
        return numberOfElements;
    }

    inline size_t
    findFirstFree()
    {
        return findFirstFree(mList);
    }

    static inline bool
    isSetSomewhere(const char* list)
    {
        for (unsigned i = 0; i <= numberOfElements / 8; i++)
        {
            if (list[i])
            {
                return true;
            }
        }
        return false;
    }

    inline bool
    isSetSomewhere()
    {
        return isSetSomewhere(mList);
    }

    static inline size_t
    countSetBits(const char* list)
    {
        size_t count = 0;
        for (unsigned i = 0; i < numberOfElements; i++)
        {
            if (getBit(i, list))
            {
                ++count;
            }
        }
        return count;
    }

    inline size_t
    countSetBits()
    {
        return countSetBits(mList);
    }

    static inline size_t
    getByteUsage()
    {
        return (numberOfElements + 7) / 8;
    }

    inline char*
    expose()
    {
        return mList;
    }

    inline void
    printStatus()
    {
        for (unsigned i = 0; i < numberOfElements; i++)
        {
            printf("%s", getBit(i) ? "1" : "0");
        }
        printf("\n");
    }

    inline bool
    operator==(BitList<numberOfElements>& rhs) const
    {
        for (unsigned i = 0; i <= numberOfElements / 8; i++)
        {
            if (mList[i] != rhs.mList[i])
            {
                return false;
            }
        }
        return true;
    }

    inline bool
    operator!=(BitList<numberOfElements>& rhs) const
    {
        return !(*this == rhs);
    }
};

template <size_t numberOfElements>
class TwoBitList
{
    char mList[(numberOfElements + 3) / 4];
public:
    inline
    TwoBitList()
    {
        clear();
    }

    inline void
    clear()
    {
        memset(mList, 0, sizeof(mList));
    }
    inline void
    setValue(size_t pos, uint8_t value)
    {
        //First mask bitfield byte leaving active bytes to zero, then insert value
        mList[pos / 4] = (mList[pos / 4] & ~(0b11 << (pos % 4) * 2))
              | (static_cast<uint8_t>(value) << (pos % 4) * 2);
    }
    inline uint8_t
    getValue(size_t pos)
    {
        //Mask bitfield byte leaving inactive bytes to zero, then right shift to bottom
        return (mList[pos / 4] & (0b11 << (pos % 4) * 2))
            >> (pos % 4) * 2;
    }
};
};
