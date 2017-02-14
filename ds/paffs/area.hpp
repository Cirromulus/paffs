/*
 * area.hpp
 *
 *  Created on: 27 Jan 2017
 *      Author: Pascal Pieper
 */
#pragma once
#include "paffs.hpp"
namespace paffs {

extern const char* area_names[];
extern const char* areaStatusNames[];

//Returns same area if there is still writable Space left
unsigned int findWritableArea(AreaType areaType, Dev* dev);

Result findFirstFreePage(unsigned int* p_out, Dev* dev, unsigned int area);

uint64_t getPageNumber(Addr addr, Dev *dev);	//Translates Addr to physical page number in respect to the Area mapping

Result manageActiveAreaFull(Dev *dev, AreaPos *area, AreaType areaType);

Result writeAreasummary(Dev *dev, AreaPos area, SummaryEntry* summary);

Result readAreasummary(Dev *dev, AreaPos area, SummaryEntry* out_summary, bool complete);

void initArea(Dev* dev, AreaPos area);
//Result loadArea(Dev *dev, AreaPos area);
Result closeArea(Dev *dev, AreaPos area);
void retireArea(Dev *dev, AreaPos area);
}  // namespace paffs
