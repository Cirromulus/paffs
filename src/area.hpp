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
	AreaPos activeArea[AreaType::no];
	AreaPos usedAreas;
	uint64_t overallDeletions;
	Device *dev;
public:
	GarbageCollection gc;
	AreaManagement(Device *mdev): dev(mdev), gc(mdev){
		clear();
	};

	void clear();

	AreaType   getType(AreaPos area);
	AreaStatus getStatus(AreaPos area);
	uint32_t   getErasecount(AreaPos area);
	AreaPos    getPos(AreaPos area);

	void setType(AreaPos area, AreaType type);
	void setStatus(AreaPos area, AreaStatus status);
	void increaseErasecount(AreaPos area);
	void setPos(AreaPos area, AreaPos pos);

	AreaPos getActiveArea(AreaType type);
	void setActiveArea(AreaType type, AreaPos pos);

	AreaPos getUsedAreas();
	void setUsedAreas(AreaPos num);
	void increaseUsedAreas();
	void decreaseUsedAreas();

	void swapAreaPosition(AreaPos a, AreaPos b);

	void setOverallDeletions(uint64_t& deletions);
	uint64_t getOverallDeletions();

	//Only for serializing areMap in Superblock
	Area* getMap();
	AreaPos* getActiveAreas();


	/**
	 * May call garbage collection
	 * May return an empty or active area
	 * Returns same area if there is still writable space left
	 *\warn modifies active area if area was inited
	 */
	unsigned int findWritableArea(AreaType areaType);

	Result findFirstFreePage(unsigned int* p_out, unsigned int area);

	Result manageActiveAreaFull(AreaType areaType);

	void initArea(AreaPos area);
	void initAreaAs(AreaPos area, AreaType type);
	Result closeArea(AreaPos area);
	void retireArea(AreaPos area);
	Result deleteAreaContents(AreaPos area);
	Result deleteArea(AreaPos area);
};

}  // namespace paffs
