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

#ifndef COBC_NEXYS3_GPIO_H
#define COBC_NEXYS3_GPIO_H

#include <stdint.h>

namespace outpost
{
namespace nexys3
{

enum Led
{
    ld0 = (1 << 0),
    ld1 = (1 << 1),
    ld2 = (1 << 2),
    ld3 = (1 << 3),
    ld4 = (1 << 4),
    ld5 = (1 << 5),
    ld6 = (1 << 6),
    ld7 = (1 << 7)
};

enum Button
{
    sw0 = (1 << 8),
    sw1 = (1 << 9),
    sw2 = (1 << 10),
    sw3 = (1 << 11),
    sw4 = (1 << 12),
    sw5 = (1 << 13),
    sw6 = (1 << 14),
    sw7 = (1 << 15),

    left  = (1 << 16),
    down  = (1 << 17),
    right = (1 << 18),
    up    = (1 << 19)
};

enum Status
{
    on = 1,
    off = 0
};

class Gpio
{
public:
    static inline void
    set(Led led, Status status)
    {
        if (status == on)
        {
            leds |= static_cast<uint32_t>(led);
        }
        else
        {
            leds &= ~static_cast<uint32_t>(led);
        }

        setRegister(leds);
    }

    static inline void
    set(uint8_t ledStatus)
    {
        setRegister(ledStatus);
    }

    static inline bool
    get(Button button)
    {
        return (getRegister() & static_cast<uint32_t>(button));
    }

private:
    static inline void
    setRegister(uint32_t value)
    {
        *reinterpret_cast<volatile uint32_t *>(peripheryRegister) = value;
    }

    static inline uint32_t
    getRegister(void)
    {
        uint32_t value = *reinterpret_cast<volatile uint32_t *>(peripheryRegister);
        return value;
    }

    static const uint32_t peripheryRegister = 0x80000304;
    static uint32_t leds;
};

}
}

#endif // COBC_NEXYS3_GPIO_H
