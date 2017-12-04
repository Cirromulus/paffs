/*
 * Config.hpp
 *
 *  Created on: 05.07.2016
 *      Author: Pascal Pieper
 */

#pragma once
#include <inttypes.h>

namespace simu
{
typedef unsigned char FlashByte;
static constexpr uint16_t pageDataSize  = 1024*4;
static constexpr uint16_t pageAuxSize   = (pageDataSize / 32);
static constexpr uint16_t pageTotalSize = (pageDataSize + pageAuxSize);
static constexpr uint16_t pagesPerBlock = 64;
static constexpr uint16_t blocksPerPlane= 8;
static constexpr uint16_t planesPerCell = 8;

static constexpr float tidFlipStartInPercent = 0.85;

static constexpr unsigned long usecRead  = 25;
static constexpr unsigned long usecWrite = 200;
static constexpr unsigned long usecErase = 1500;
};
