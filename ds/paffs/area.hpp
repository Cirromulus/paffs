/*
 * area.hpp
 *
 *  Created on: 27 Jan 2017
 *      Author: Pascal Pieper
 */
#pragma once
#include "commonTypes.hpp"
#include "garbage_collection.hpp"

namespace paffs {

extern const char* area_names[];
extern const char* areaStatusNames[];

//Helper functions
uint64_t getPageNumber(Addr addr, Device *dev);	//Translates Addr to physical page number in respect to the Area mapping
Addr combineAddress(uint32_t logical_area, uint32_t page);
unsigned int extractLogicalArea(Addr addr);
unsigned int extractPage(Addr addr);

class AreaManagement{

	Device *dev;
public:
	GarbageCollection gc;
	AreaManagement(Device *mdev): dev(mdev), gc(mdev){};

	//Returns same area if there is still writable Space left
	unsigned int findWritableArea(AreaType areaType);

	Result findFirstFreePage(unsigned int* p_out, unsigned int area);

	Result manageActiveAreaFull(AreaPos *area, AreaType areaType);

	void initArea(AreaPos area);
	Result closeArea(AreaPos area);
	void retireArea(AreaPos area);
};

}  // namespace paffs
