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
    uint8_t mList[(numberOfElements + 7) / 8];

public:
    static constexpr size_t byteUsage = (numberOfElements + 7) / 8;
    static inline size_t
    getByteUsage()
    {
        return byteUsage;
    }

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
    setBit(size_t n, uint8_t* list)
    {
        if (n < numberOfElements)
        {
            list[n / 8] |= 1 << n % 8;
        }
        else
        {
            PAFFS_DBG(PAFFS_TRACE_BUG, "Tried to set Bit at %zu, but size is %zu", n, numberOfElements);
        }
    }

    inline void
    setBit(size_t n)
    {
        setBit(n, mList);
    }

    static inline void
    resetBit(size_t n, uint8_t* list)
    {
        if (n < numberOfElements)
        {
            list[n / 8] &= ~(1 << n % 8);
        }
        else
        {
            PAFFS_DBG(PAFFS_TRACE_BUG, "Tried to reset Bit at %zu, but size is %zu", n, numberOfElements);
        }
    }

    inline void
    resetBit(size_t n)
    {
        resetBit(n, mList);
    }

    static inline bool
    getBit(size_t n, const uint8_t* list)
    {
        if (n < numberOfElements)
        {
            return list[n / 8] & 1 << n % 8;
        }
        PAFFS_DBG(PAFFS_TRACE_BUG, "Tried to get Bit at %zu, but size is %zu", n, numberOfElements);
        return false;
    }

    inline bool
    getBit(size_t n)
    {
        return getBit(n, mList);
    }

    static inline size_t
    findFirstFree(const uint8_t* list)
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
    isSetSomewhere(const uint8_t* list)
    {
        for (size_t i = 0; i <= numberOfElements / 8; i++)
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
    countSetBits(const uint8_t* list)
    {
        size_t count = 0;
        for (size_t i = 0; i < numberOfElements; i++)
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

    inline uint8_t*
    expose()
    {
        return mList;
    }

    inline void
    printStatus()
    {
        for (size_t i = 0; i < numberOfElements; i++)
        {
            printf("%s", getBit(i) ? "1" : "0");
        }
        printf("\n");
    }

    inline bool
    operator==(const BitList<numberOfElements>& rhs) const
    {
        for (size_t i = 0; i < (numberOfElements + 7) / 8; i++)
        {
            if (mList[i] != rhs.mList[i])
            {
                return false;
            }
        }
        return true;
    }

    inline bool
    operator!=(const BitList<numberOfElements>& rhs) const
    {
        return !(*this == rhs);
    }
};

template <size_t numberOfElements>
class TwoBitList
{
    uint8_t mList[(numberOfElements + 3) / 4];
public:
    static constexpr size_t
    byteUsage = (numberOfElements + 3) / 4;

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
    static inline void
    setValue(size_t pos, uint8_t value, uint8_t list[(numberOfElements + 3) / 4])
    {
        //First mask bitfield byte leaving active bytes to zero, then insert value
        list[pos / 4] = (list[pos / 4] & ~(0b11 << (pos % 4) * 2))
              | (static_cast<uint8_t>(value) << (pos % 4) * 2);
    }
    inline void
    setValue(size_t pos, uint8_t value)
    {
        setValue(pos, value, mList);
    }
    static inline uint8_t
    getValue(const size_t pos, const uint8_t list[(numberOfElements + 3) / 4])
    {
        //Mask bitfield byte leaving inactive bytes to zero, then right shift to bottom
        return (list[pos / 4] & (0b11 << (pos % 4) * 2))
            >> (pos % 4) * 2;
    }
    inline uint8_t
    getValue(size_t pos) const
    {
        return getValue(pos, mList);
    }

    inline uint8_t*
    expose()
    {
        return mList;
    }

    inline bool
    operator==(const TwoBitList<numberOfElements>& rhs) const
    {
        for (size_t i = 0; i < (numberOfElements + 3) / 4; i++)
        {
            if (mList[i] != rhs.mList[i])
            {
                return false;
            }
        }
        return true;
    }

    inline bool
    operator!=(const TwoBitList<numberOfElements>& rhs) const
    {
        return !(*this == rhs);
    }
};
};
