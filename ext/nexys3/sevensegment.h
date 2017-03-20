/*
 * Copyright (c) 2013, German Aerospace Center (DLR)
 *
 * This file is part of libCOBC 0.4.
 *
 * It is distributed under the terms of the GNU General Public License with a
 * linking exception. See the file "LICENSE" for the full license governing
 * this code.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 */
// ----------------------------------------------------------------------------

#ifndef COBC_NEXYS3_SEVENSEGMENT_H
#define COBC_NEXYS3_SEVENSEGMENT_H

#include <stdint.h>
#include <outpost/rtos/mutex.h>
#include <outpost/rtos/mutex_guard.h>

namespace outpost
{
namespace nexys3
{

/**
 * Seven segment display driver.
 *
 * \author  Fabian Greif
 */
class SevenSegment
{
public:
    static void
    clear()
    {
        rtos::MutexGuard guard(mutex);
        buf = 0;
        (*reinterpret_cast<uint32_t *>(0x80000300)) = 0;
    }

    static void
    write(uint8_t index, char c);

    static void
    writeChars(char c1, char c2, char c3, char c4);

    static void
    writeText(const char* s);

    static void
    writeHex(uint8_t index, uint8_t hex);

    static void
    writeHex(uint16_t value);

    static void
    writeDec(int16_t value);

private:
    static void
    writeRaw(uint8_t index, uint8_t value)
    {
        index = 3 - index;

        uint32_t temp = static_cast<uint32_t>(value) << (index*8);
        uint32_t mask = static_cast<uint32_t>(0xff)  << (index*8);

        {
            rtos::MutexGuard guard(mutex);
            buf &= ~mask;
            buf |= temp;

            (*reinterpret_cast<uint32_t *>(0x80000300)) = buf;
        }
    }

    static void
    writeRaw(uint32_t value)
    {
        rtos::MutexGuard guard(mutex);
        buf = value;
        (*reinterpret_cast<uint32_t *>(0x80000300)) = buf;
    }

    static rtos::Mutex mutex;
    static uint32_t buf;
};

}
}
#endif // COBC_NEXYS3_SEVENSEGMENT_H
