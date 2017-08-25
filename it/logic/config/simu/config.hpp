/*
 * types.hpp
 *
 *  Created on: 05.07.2016
 *      Author: Pascal Pieper
 */

#pragma once


#define DATA_TYPE unsigned char
#define PAGE_DATA 512 //1024
#define PAGE_AUX (PAGE_DATA / 32)
#define PAGE_SIZE (PAGE_DATA + PAGE_AUX)
#define BLOCK_SIZE 64	//128
#define PLANE_SIZE 8	//8
//#define CELL_SIZE 1024
#define CELL_SIZE 8	//4

#define TID_FLIP_START_PERCENT 0.85
