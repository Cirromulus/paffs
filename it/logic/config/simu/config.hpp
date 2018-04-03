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
#include <inttypes.h>

namespace simu
{
typedef unsigned char FlashByte;
static constexpr uint16_t pageDataSize  = 512;
static constexpr uint16_t pageAuxSize   = (pageDataSize / 32);
static constexpr uint16_t pageTotalSize = (pageDataSize + pageAuxSize);
static constexpr uint16_t pagesPerBlock = 64;
static constexpr uint16_t blocksPerPlane= 8;
static constexpr uint16_t planesPerCell = 8;

static constexpr float tidFlipStartInPercent = 0.85;

static constexpr unsigned long FlashReadUsec  = 25;
static constexpr unsigned long FlashWriteUsec = 200;
static constexpr unsigned long FlashEraseUsec = 1500;

static constexpr unsigned long MramReadNsec  = 35;
static constexpr unsigned long MramWriteNsec = 35;
};
