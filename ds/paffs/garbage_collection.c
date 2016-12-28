/*
 * garbage_collection.c
 *
 *  Created on: 28.12.2016
 *      Author: urinator
 */

#include "garbage_collection.h"

uint32_t countDirtyPages(p_summaryEntry* summary){
	return -1;
}

PAFFS_RESULT collectGarbage(p_dev* dev){
	uint32_t favourite = 0;
	for(uint32_t i = 0; i < dev->param.areas_no; i++){
		if(dev->areaMap[favourite].status == CLOSED && dev->areaMap[favourite].type != SUPERBLOCKAREA){
			if(dev->areaMap[favourite].has_areaSummary){
				//TODO: 'has_areaSummary' means just not cached, should get it from Flash
			}
		}
	}
	return PAFFS_NIMPL;
}
