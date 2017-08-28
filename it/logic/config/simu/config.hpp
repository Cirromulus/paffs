/*
 * types.hpp
 *
 *  Created on: 05.07.2016
 *      Author: Pascal Pieper
 */

#pragma once

#include <inttypes.h>

#define DATA_TYPE uint8_t
#define PAGE_DATA 512
#define PAGE_AUX (PAGE_DATA / 32)
#define PAGE_SIZE (PAGE_DATA + PAGE_AUX)
#define BLOCK_SIZE 64
#define PLANE_SIZE 8
#define CELL_SIZE 8

#define TID_FLIP_START_PERCENT 0.85
