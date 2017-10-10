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
//Translates indirect Addr to physical page number in respect to the Area mapping
PageAbs getPageNumber(Addr addr, Device &dev);
//Translates direct Addr to physical page number ignoring AreaMap
PageAbs getPageNumberFromDirect(Addr addr);
//Returns the absolute page number from *logical* address
BlockAbs getBlockNumber(Addr addr, Device& dev);
//Translates direct Addr to physical Block number ignoring AreaMap
BlockAbs getBlockNumberFromDirect(Addr addr);
//combines two values to one type
Addr combineAddress(AreaPos logical_area, PageOffs page);
unsigned int extractLogicalArea(Addr addr);
unsigned int extractPageOffs(Addr addr);

class AreaManagement{

	Device *dev;
public:
	GarbageCollection gc;
	AreaManagement(Device *mdev): dev(mdev), gc(mdev){};

	/**
	 * May call garbage collection
	 * May return an empty or active area
	 * Returns same area if there is still writable space left
	 */
	unsigned int findWritableArea(AreaType areaType);

	Result findFirstFreePage(unsigned int* p_out, unsigned int area);

	Result manageActiveAreaFull(AreaPos *area, AreaType areaType);

	void initArea(AreaPos area);
	Result closeArea(AreaPos area);
	void retireArea(AreaPos area);
	Result deleteAreaContents(AreaPos area);
	Result deleteArea(AreaPos area);
};

}  // namespace paffs
