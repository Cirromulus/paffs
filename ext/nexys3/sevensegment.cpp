/*
 * Copyright (c) 2013, German Aerospace Center (DLR)
 *
 * This file is part of liboutpost 0.4.
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

#include "../nexys3/sevensegment.h"

outpost::rtos::Mutex outpost::nexys3::SevenSegment::mutex;
uint32_t outpost::nexys3::SevenSegment::buf = 0;

enum SevenSegment
{
    A = (1 << 0),
    B = (1 << 1),
    C = (1 << 2),
    D = (1 << 3),
    E = (1 << 4),
    F = (1 << 5),
    G = (1 << 6),
    H = (1 << 7)
};

static const uint8_t font[10+27] =
{
    A | B | C | D | E | F,        // 0
        B | C,                    // 1
    A | B |     D | E |     G,    // 2
    A | B | C | D |         G,    // 3
        B | C |         F | G,    // 4
    A |     C | D |     F | G,    // 5
    A |     C | D | E | F | G,    // 6
    A | B | C,                    // 7
    A | B | C | D | E | F | G,    // 8
    A | B | C | D |     F | G,    // 9

    A | B | C |     E | F | G,    // A
            C | D | E | F | G,    // B
                D | E |     G,    // C
        B | C | D | E |     G,    // D
    A |         D | E | F | G,    // E
    A |             E | F | G,    // F
    A |     C | D | E | F,        // G
        B | C |     E | F | G,    // H
                    E | F,        // I
        B | C | D | E,            // J
        B | C |     E | F | G,    // K
                D | E | F,        // L
    A |     C |     E,            // M
            C |     E |     G,    // N
            C | D | E |     G,    // O
    A | B |         E | F | G,    // P
    A | B | C |         F | G,    // Q
                    E |     G,    // R
    A |     C | D |     F | G,    // S
                D | E | F | G,    // T
        B | C | D | E | F,        // U
            C | D | E,            // V
        B |     D |     F,        // W
        B | C |     E | F | G,    // X
        B | C | D |     F | G,    // Y
    A | B |     D | E |     G,    // Z
                            G,    // -
};

// ----------------------------------------------------------------------------
void
outpost::nexys3::SevenSegment::write(uint8_t index, char c)
{
    if (index > 3) {
        return;
    }

    uint8_t value = 0;

    // Digits
    if ((c >= '0') && (c <= '9'))
    {
        value = font[c - '0'];
    }
    // Uppercase characters
    else if ((c >= 'A') && (c <= 'Z'))
    {
        value = font[c - '7'];
    }
    // Lowercase characters
    else if ((c >= 'a') && (c <= 'z'))
    {
        value = font[c - 'W'];
    }
    // Minus sign
    else if (c == '-')
    {
        value = font[36];
    }

    writeRaw(index, value);
}

void
outpost::nexys3::SevenSegment::writeChars(char c1, char c2, char c3, char c4)
{
    write(0, c1);
    write(1, c2);
    write(2, c3);
    write(3, c4);
}

void
outpost::nexys3::SevenSegment::writeText(const char* s)
{
    uint8_t index = 0;
    char c = *s++;

    while (c != '\0' && index < 4)
    {
        write(index, c);

        index++;
        c = *s++;
    }

    // Fill the remaining space with spaces
    while (index < 4)
    {
        write(index, ' ');
        index++;
    }
}

void
outpost::nexys3::SevenSegment::writeHex(uint8_t index, uint8_t hex)
{
    if (index > 3 || hex >= 16) {
        return;
    }

    writeRaw(index, font[hex]);
}

void
outpost::nexys3::SevenSegment::writeHex(uint16_t value)
{
    uint32_t segments = 0;

    for (uint_fast8_t i = 0; i < 4; ++i) {
        segments |= font[value & 0x000F] << 24;
        segments >>= 8;
        value >>= 8;
    }

    writeRaw(segments);
}

void
outpost::nexys3::SevenSegment::writeDec(int16_t value)
{
    // Positive value [0..9999]
    if ((value >= 0) && (value <= 9999))
    {
        uint32_t segments = 0;
        for (uint_fast8_t i = 0; i < 4; i++)
        {
            // Get the nth digit
            int16_t tmp = value;
            for (uint_fast8_t n = i; n > 0; n--)
            {
                tmp /= 10;
            }
            segments |= (font[tmp % 10] << (i * 8));
        }
        writeRaw(segments);
    }
    // Negative value [-999..-1]
    else if ((value >= -999) && (value < 0))
    {
        uint32_t segments = 0;
        for (uint_fast8_t i = 0; i < 3; i++)
        {
            // Absolute value
            int16_t tmp = value * (-1);

            // Get the nth digit
            for (uint_fast8_t n = i; n > 0; n--)
            {
                tmp /= 10;
            }
            segments |= (font[tmp % 10] << (i * 8));
        }
        // Minus sign
        segments |= (font[36] << 24);
        writeRaw(segments);
    }
    else
    {
        // Value out of range
        writeChars('-', '-', '-', '-');
    }
}
