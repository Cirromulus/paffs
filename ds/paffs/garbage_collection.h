/*
 * garbage_collection.h
 *
 *  Created on: 28.12.2016
 *      Author: urinator
 */

#ifndef DS_PAFFS_GARBAGE_COLLECTION_H_
#define DS_PAFFS_GARBAGE_COLLECTION_H_

#include "paffs.h"

PAFFS_RESULT collectGarbage(p_dev* dev, p_areaType target);

#endif /* DS_PAFFS_GARBAGE_COLLECTION_H_ */