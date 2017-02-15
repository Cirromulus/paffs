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
unsigned int findWritableArea(AreaType areaType, Device* dev);

Result findFirstFreePage(unsigned int* p_out, Device* dev, unsigned int area);

uint64_t getPageNumber(Addr addr, Device *dev);	//Translates Addr to physical page number in respect to the Area mapping

Result manageActiveAreaFull(Device *dev, AreaPos *area, AreaType areaType);

Result writeAreasummary(Device *dev, AreaPos area, SummaryEntry* summary);

Result readAreasummary(Device *dev, AreaPos area, SummaryEntry* out_summary, bool complete);

void initArea(Device* dev, AreaPos area);
//Result loadArea(Device *dev, AreaPos area);
Result closeArea(Device *dev, AreaPos area);
void retireArea(Device *dev, AreaPos area);
}  // namespace paffs
