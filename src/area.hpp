/*
 * area.hpp
 *
 *  Created on: 27 Jan 2017
 *      Author: Pascal Pieper
 */
#pragma once
#include "commonTypes.hpp"
#include "garbage_collection.hpp"
#include "journalTopic.hpp"

namespace paffs {

extern const char* area_names[];
extern const char* areaStatusNames[];

//Helper functions
//Translates indirect Addr to physical page number in respect to the Area mapping
PageAbs getPageNumber(const Addr addr, Device &dev);
//Translates direct Addr to physical page number ignoring AreaMap
PageAbs getPageNumberFromDirect(const Addr addr);
//Returns the absolute page number from *logical* address
BlockAbs getBlockNumber(const Addr addr, Device& dev);
//Translates direct Addr to physical Block number ignoring AreaMap
BlockAbs getBlockNumberFromDirect(const Addr addr);
//combines two values to one type
Addr combineAddress(const AreaPos logical_area, const PageOffs page);
unsigned int extractLogicalArea(const Addr addr);
unsigned int extractPageOffs(const Addr addr);

class AreaManagement{
	Area map[areasNo];
	Device *dev;
public:
	GarbageCollection gc;
	AreaManagement(Device *mdev): dev(mdev), gc(mdev){
		PAFFS_DBG_S(PAFFS_TRACE_VERBOSE, "Cleared %zu Byte in Areamap", sizeof(Area) * areasNo);
		memset(map, 0, areasNo * sizeof(Area));
	};

	AreaType   getType(AreaPos area);
	AreaStatus getStatus(AreaPos area);
	uint32_t   getErasecount(AreaPos area);
	AreaPos    getPos(AreaPos area);

	void setType(AreaPos area, AreaType type);
	void setStatus(AreaPos area, AreaStatus status);
	void increaseErasecount(AreaPos area);
	void setErasecount(AreaPos area, uint32_t erasecount);
	void setPos(AreaPos area, AreaPos pos);

	void swapAreaPosition(AreaPos a, AreaPos b);
	//Only for serializing areMap in Superblock
	Area* getMap();

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
